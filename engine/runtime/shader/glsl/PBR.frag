#version 450  // 指定GLSL版本为4.5，支持现代GPU特性和计算着色器

// ============================================================================
// PBR片段着色器 - 基于物理的渲染实现
// ============================================================================
/**
 * @file PBR.frag
 * @brief 完整的PBR（基于物理的渲染）片段着色器实现
 * @details 实现了完整的Cook-Torrance微表面BRDF模型，包括：
 *          - 直接光照计算（点光源、方向光源、聚光灯）
 *          - 间接光照（基于图像的光照IBL）
 *          - 阴影贴图技术
 *          - 法线贴图
 *          - 多种材质贴图支持
 *          
 *          渲染方程基础：
 *          Lo(p,ωo) = ∫Ω fr(p,ωi,ωo) * Li(p,ωi) * (n·ωi) dωi
 *          
 *          其中：
 *          - Lo：出射辐射度（从表面点p沿方向ωo的光线强度）
 *          - fr：双向反射分布函数（BRDF，描述光线如何在表面反射）
 *          - Li：入射辐射度（从方向ωi入射到点p的光线强度）
 *          - (n·ωi)：Lambert余弦定律（入射角对光照强度的影响）
 *          - Ω：半球积分域（表面法线上方的所有方向）
 */

// ============================================================================
// 光源数据结构定义
// ============================================================================
/**
 * @struct Light
 * @brief 通用光源数据结构
 * @details 支持多种光源类型的统一数据格式，使用紧凑的vec4布局
 *          以提高GPU内存访问效率和减少内存带宽消耗
 *          
 *          数据布局设计原理：
 *          - 使用vec4对齐，符合GPU内存访问模式（128位对齐）
 *          - 复用字段减少内存占用，提高缓存命中率
 *          - 支持多种光源类型的统一处理，简化着色器逻辑
 */
struct Light
{
	// 光源位置信息：xyz分量存储世界坐标系中的光源位置（米为单位）
	// w分量作为光源类型标识：0=方向光，1=点光源，2=聚光灯
	vec4 position;
	
	// 光源颜色和强度：xyz分量存储RGB颜色值（线性空间，范围[0,1]）
	// w分量存储光源强度（流明值，物理单位）
	vec4 color;
	
	// 光源方向信息：xyz分量存储归一化的光源方向向量
	// 对于方向光：表示光线传播方向（从光源指向场景）
	// 对于聚光灯：表示聚光灯的中心轴方向
	// w分量存储光源影响范围（米为单位，用于距离衰减计算）
	vec4 direction;
	
	// 聚光灯专用参数：x=内锥角余弦值，y=外锥角余弦值
	// 使用余弦值避免在着色器中进行三角函数计算，提高性能
	// z和w分量预留用于未来扩展（如阴影贴图索引等）
	vec4 info;
};

// ============================================================================
// Uniform缓冲对象 - 视图和光照数据
// ============================================================================
/**
 * @brief 场景光照和视图参数
 * @details 包含所有光源信息和相机参数，支持多种光源类型
 *          
 *          内存布局优化：
 *          - 使用std140布局标准，确保跨平台兼容性
 *          - 数组元素按16字节对齐，提高GPU访问效率
 *          - 总大小控制在合理范围内，避免超出uniform buffer限制
 */
// 绑定到描述符集合0的第1个绑定点，用于存储场景级别的光照和视图数据
layout(set = 0, binding = 1) uniform UniformBufferObjectView
{
	// 方向光源数组：模拟太阳光等平行光源，光线方向一致且无衰减
	// 最多支持4个方向光源，满足大多数场景需求
	Light directional_lights[4];
	
	// 点光源数组：模拟灯泡等全方向发光的光源，具有距离衰减
	// 最多支持4个点光源，用于局部照明效果
	Light point_lights[4];
	
	// 聚光灯数组：模拟手电筒等锥形光源，具有方向性和角度衰减
	// 最多支持4个聚光灯，用于重点照明和戏剧性效果
	Light spot_lights[4];
	
	// 光源数量统计：x=方向光数量，y=点光源数量，z=聚光灯数量，w=天空盒mip级别
	// 使用ivec4避免浮点数精度问题，提高循环控制的可靠性
    ivec4 lights_count;
    
    // 相机世界坐标位置：用于计算视线方向和镜面反射
    // xyz分量存储相机位置，w分量预留（可用于存储相机参数）
	vec4 camera_position;
} view;  // 命名为view，便于在着色器中引用

// ========================================================================
// 光源数量快速访问宏
// ========================================================================
/**
 * 从uniform buffer中提取各类光源的实际数量
 * 用于循环控制和性能优化，避免处理无效的光源数据
 * 
 * 性能优化原理：
 * - 使用实际光源数量控制循环，减少不必要的计算
 * - 编译时常量化，GPU可以进行循环展开优化
 * - 避免分支预测失败带来的性能损失
 */
uint DIRECTIONAL_LIGHTS = view.lights_count[0];  // 当前场景中活跃的方向光源数量
uint POINT_LIGHTS = view.lights_count[1];        // 当前场景中活跃的点光源数量
uint SPOT_LIGHTS = view.lights_count[2];         // 当前场景中活跃的聚光灯数量
uint SKY_MAXMIPS = view.lights_count[3];         // 天空盒纹理的最大mip级别数（用于IBL计算）

// ============================================================================
// 纹理采样器 - 材质和环境贴图
// ============================================================================
/**
 * PBR材质工作流所需的各种贴图资源
 * 
 * 纹理绑定策略：
 * - 使用描述符集合0，确保所有材质纹理在同一个集合中
 * - 按使用频率和重要性排序绑定点，优化GPU缓存性能
 * - 支持标准PBR工作流的所有必需贴图类型
 */

// 绑定点2：天空盒立方体贴图，用于基于图像的光照（IBL）
// 存储预过滤的环境光照信息，支持不同粗糙度级别的mipmap
layout(set = 0, binding = 2) uniform samplerCube skycube;

// 绑定点3：基础颜色贴图（反照率贴图）
// 存储材质的固有颜色，sRGB色彩空间，需要进行伽马校正
layout(set = 0, binding = 3) uniform sampler2D sampler1;

// 绑定点4：金属度贴图（Metallic Map）
// 单通道纹理，值范围[0,1]，0=电介质材质，1=金属材质
// 控制材质的导电性和反射特性
layout(set = 0, binding = 4) uniform sampler2D sampler2;

// 绑定点5：粗糙度贴图（Roughness Map）
// 单通道纹理，值范围[0,1]，0=完全光滑（镜面），1=完全粗糙（漫反射）
// 控制微表面的粗糙程度，影响镜面反射的锐利度
layout(set = 0, binding = 5) uniform sampler2D sampler3;

// 绑定点6：法线贴图（Normal Map）
// 切线空间法线贴图，RGB通道存储xyz法线分量
// 用于增加表面细节，模拟凹凸效果而不增加几何复杂度
layout(set = 0, binding = 6) uniform sampler2D sampler4;

// 绑定点7：环境遮蔽贴图（Ambient Occlusion Map）
// 单通道纹理，值范围[0,1]，模拟几何体自遮挡产生的阴影
// 增强深度感和真实感，特别是在缝隙和凹陷处
layout(set = 0, binding = 7) uniform sampler2D sampler5;

// 绑定点8：方向光阴影贴图（Shadow Map）
// 深度纹理，存储从光源视角看到的场景深度信息
// 用于实时阴影计算，支持PCF软阴影技术
layout(set = 0, binding = 8) uniform sampler2D directional_light_shadow;

// ============================================================================
// 变换矩阵Uniform缓冲对象
// ============================================================================
/**
 * @brief 渲染管线变换矩阵
 * @details 包含模型、视图、投影变换以及阴影贴图相关的光源变换矩阵
 *          
 *          矩阵变换管线：
 *          顶点坐标 -> 模型矩阵 -> 世界坐标 -> 视图矩阵 -> 相机坐标 -> 投影矩阵 -> 裁剪坐标
 *          
 *          内存布局：
 *          - 使用列主序存储（OpenGL标准）
 *          - 每个mat4占用64字节（4x4个float）
 *          - 按16字节边界对齐，优化GPU访问性能
 */
// 绑定到描述符集合0的第0个绑定点，存储变换矩阵数据
layout(set = 0, binding = 0) uniform UniformBufferObject
{
    // 模型变换矩阵：将顶点从局部坐标系变换到世界坐标系
    // 包含平移、旋转、缩放变换，用于定位物体在世界中的位置和姿态
    mat4 model;
    
    // 视图变换矩阵：将世界坐标变换到相机坐标系
    // 相当于相机的逆变换，用于模拟观察者的视角
    mat4 view;
    
    // 投影变换矩阵：将相机坐标变换到裁剪坐标系
    // 实现透视投影或正交投影，定义视锥体和深度范围
    mat4 proj;
    
    // 方向光源的投影视图矩阵：世界坐标 -> 光源裁剪坐标
    // 用于阴影贴图计算，将片段位置变换到光源视角的屏幕空间
    mat4 directional_light_proj_view;
} ubo;  // 命名为ubo，便于在着色器中引用

// ============================================================================
// 顶点着色器输入数据
// ============================================================================
/**
 * 从顶点着色器传递的插值数据
 * 
 * 插值原理：
 * - GPU在光栅化阶段对三角形内部的片段进行线性插值
 * - 保证片段属性在三角形表面的连续性和平滑过渡
 * - 插值精度受到浮点数精度和三角形大小影响
 */

// 输入位置0：片段的世界坐标位置
// 用于计算光照方向、视线方向和阴影坐标变换
layout(location = 0) in vec3 fragPosition;

// 输入位置1：片段的世界空间法线向量
// 注意：插值后的法线需要重新归一化，因为线性插值不保持向量长度
layout(location = 1) in vec3 fragNormal;

// 输入位置2：顶点颜色（可选）
// 可用于调试、顶点着色或与纹理颜色混合
layout(location = 2) in vec3 fragColor;

// 输入位置3：纹理坐标
// UV坐标，范围通常为[0,1]，用于纹理采样
layout(location = 3) in vec2 fragTexCoord;

// ============================================================================
// 着色器输出
// ============================================================================
// 输出位置0：最终片段颜色
// RGBA格式，RGB存储颜色，A存储透明度（当前实现中A=1.0）
layout(location = 0) out vec4 outColor;

// ============================================================================
// 数学和物理常量
// ============================================================================
/**
 * PBR计算中使用的重要常量
 * 
 * 物理意义：
 * - 这些常量基于真实世界的物理定律和材质属性
 * - 确保渲染结果符合物理直觉和能量守恒定律
 */

// 圆周率：用于BRDF归一化和球面积分计算
// 在Lambert漫反射和GGX分布函数中作为归一化因子
const float PI = 3.14159265359;

// 电介质材质的基础反射率（F0值）
// 大多数非金属材质的F0值约为0.04（4%反射率）
// 这是基于真实世界材质测量得出的经验值
vec3 F0 = vec3(0.04);

// ============================================================================
// Fresnel反射函数 - Schlick近似
// ============================================================================
/**
 * @brief Fresnel反射的Schlick近似实现
 * @param f0 材质的基础反射率（垂直入射时的反射率）
 * @param f90 掠射角时的反射率（通常为1.0）
 * @param u 余弦值（通常是N·V或N·L）
 * @return 计算得到的Fresnel反射系数
 * 
 * 物理原理：
 * - Fresnel效应描述了光线在不同介质界面的反射和折射现象
 * - 当观察角度接近掠射角时，反射率急剧增加
 * - Schlick近似用简单的多项式替代复杂的Fresnel方程
 * 
 * 数学公式：F(u) = f0 + (f90 - f0) * (1 - u)^5
 * 其中u = cos(θ)，θ是入射角或观察角
 */
vec3 F_Schlick(vec3 f0, float f90, float u)
{
	// 计算(1-u)^5，模拟Fresnel效应的非线性特性
	// 当u接近0（掠射角）时，反射率接近f90
	// 当u接近1（垂直入射）时，反射率接近f0
	return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

// ============================================================================
// 考虑粗糙度的Fresnel反射 - 用于IBL计算
// ============================================================================
/**
 * @brief 考虑表面粗糙度的Fresnel反射计算
 * @param F0 材质的基础反射率
 * @param cos_theta 法线与视线的夹角余弦值
 * @param roughness 表面粗糙度[0,1]
 * @return 修正后的Fresnel反射系数
 * 
 * 物理原理：
 * - 粗糙表面会影响Fresnel反射的强度
 * - 粗糙度越高，掠射角反射越弱
 * - 用于基于图像的光照（IBL）中的环境反射计算
 */
vec3 F_Schlick_Roughness(vec3 F0, float cos_theta, float roughness)
{
	// 根据粗糙度调整掠射角反射率
	// 粗糙表面在掠射角的反射率低于光滑表面
	vec3 roughness_factor = max(vec3(1.0 - roughness), F0);
	return F0 + (roughness_factor - F0) * pow(1.0 - cos_theta, 5.0);
}

// ============================================================================
// Disney漫反射BRDF - 能量守恒的漫反射模型
// ============================================================================
/**
 * @brief Disney漫反射BRDF实现
 * @param NdotV 法线与视线夹角的余弦值
 * @param NdotL 法线与光线夹角的余弦值
 * @param LdotH 光线与半角向量夹角的余弦值
 * @param roughness 表面粗糙度[0,1]
 * @return 漫反射BRDF值
 * 
 * 物理原理：
 * - 传统Lambert漫反射模型过于简化，不符合真实材质行为
 * - Disney漫反射考虑了表面粗糙度对漫反射的影响
 * - 在掠射角处增加反射，更符合真实世界观察
 * 
 * 数学模型：
 * - 使用双向散射函数模拟光线在粗糙表面的多次散射
 * - E_bias和E_factor控制能量分布和归一化
 * - fd90参数控制掠射角处的漫反射强度
 */
float Fr_DisneyDiffuse(float NdotV, float NdotL, float LdotH, float roughness)
{
	// 能量偏置项：控制漫反射的基础强度
	// 光滑表面偏置为0，粗糙表面偏置增加
	float E_bias = 0.0 * (1.0 - roughness) + 0.5 * roughness;
	
	// 能量因子：确保能量守恒，归一化漫反射强度
	// 1.51是经验常数，基于真实材质测量数据
	float E_factor = 1.0 * (1.0 - roughness) + (1.0 / 1.51) * roughness;
	
	// 掠射角漫反射参数：基于半角向量和粗糙度
	// LdotH^2项增强掠射角效应
	float fd90 = E_bias + 2.0 * LdotH * LdotH * roughness;
	
	// 使用白色F0值（1.0）计算散射系数
	vec3 f0 = vec3(1.0);
	
	// 计算光线方向和视线方向的散射贡献
	float light_scatter = F_Schlick(f0, fd90, NdotL).r;
	float view_scatter = F_Schlick(f0, fd90, NdotV).r;
	
	// 返回双向散射的乘积，乘以能量因子
	return light_scatter * view_scatter * E_factor;
}

// ============================================================================
// Smith G函数 - 相关遮蔽阴影函数
// ============================================================================
/**
 * @brief Smith G函数的高度相关实现
 * @param NdotV 法线与视线夹角的余弦值
 * @param NdotL 法线与光线夹角的余弦值
 * @param roughness 表面粗糙度[0,1]
 * @return 可见性函数值（几何项除以4(N·L)(N·V)）
 * 
 * 物理原理：
 * - 考虑微表面遮蔽和阴影的相关性
 * - 相比独立Smith G函数，更准确地模拟真实表面行为
 * - 返回值已经包含了BRDF分母中的4(N·L)(N·V)项
 * 
 * 数学模型：
 * - 使用Lambda函数描述遮蔽概率
 * - Λ(ω) = (-1 + √(1 + α²tan²θ)) / 2
 * - G = 1 / (1 + Λ(ωᵢ) + Λ(ωₒ))
 * 
 * 优化特性：
 * - 避免了传统Smith G函数的数值不稳定性
 * - 在掠射角处有更好的能量守恒特性
 * - 计算效率高，适合实时渲染
 */
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	// 计算α²（粗糙度的平方）
	// 这是GGX分布的标准参数化
	float alphaRoughnessSq = roughness * roughness;

	// 计算视线方向的Lambda项
	// GGXV = NdotL * √(NdotV² * (1-α²) + α²)
	// 这表示从视线方向看到的有效投影面积
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
	
	// 计算光线方向的Lambda项
	// GGXL = NdotV * √(NdotL² * (1-α²) + α²)
	// 这表示从光线方向看到的有效投影面积
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

	// 计算相关Smith G函数的分母
	// 这里使用了高度相关的遮蔽模型
	float GGX = GGXV + GGXL;
	
	// 返回可见性函数 V = G / (4 * NdotL * NdotV)
	// 0.5因子来自于Smith G函数的归一化
	if (GGX > 0.0)
	{
		return 0.5 / GGX;
	}
	return 0.0;
}

// ============================================================================
// GGX法线分布函数
// ============================================================================
/**
 * @brief GGX/Trowbridge-Reitz法线分布函数
 * @param NdotH 法线与半角向量夹角的余弦值
 * @param roughness 表面粗糙度[0,1]
 * @return 法线分布概率密度
 * 
 * 物理原理：
 * - 描述微表面法线在半角向量方向的分布概率
 * - GGX分布具有较长的尾部，更符合真实材质特性
 * - 在计算机图形学中被广泛采用的标准分布
 * 
 * 数学公式：
 * D(h) = α² / (π * ((N·h)² * (α² - 1) + 1)²)
 * 其中：
 * - α = roughness²（粗糙度参数）
 * - h = 半角向量 = normalize(L + V)
 * - N·h = 法线与半角向量的点积
 * 
 * 特性分析：
 * - roughness → 0：分布集中在法线方向（镜面反射）
 * - roughness → 1：分布趋于均匀（漫反射）
 * - 在半球面上积分为1（归一化特性）
 */
float D_GGX(float NdotH, float roughness)
{
	// 计算GGX分布的α²参数
	// 使用roughness²使感知线性度更好
	float alphaRoughnessSq = roughness * roughness;
	
	// 计算分母项：(N·h)² * (α² - 1) + 1
	// 这是GGX分布公式的核心部分
	// 当N·h=1且α=0时，f=1，分布最集中
	float f = (NdotH * alphaRoughnessSq - NdotH) * NdotH + 1.0;
	
	// 返回归一化的概率密度
	// π确保在半球面上的积分为1
	// f²项产生GGX分布特有的长尾特性
	return alphaRoughnessSq / (PI * f * f);
}

// ============================================================================
// 数学工具函数
// ============================================================================

/**
 * @brief 将浮点数限制在[0,1]范围内
 * @param t 输入值
 * @return 限制后的值，范围[0,1]
 * 
 * @details 等价于clamp(t, 0.0, 1.0)，常用于：
 *          - 颜色值归一化
 *          - 概率和权重计算
 *          - 防止数值溢出
 */
float saturate(float t)
{
	return clamp(t, 0.0, 1.0);
}

/**
 * @brief 将向量的每个分量限制在[0,1]范围内
 * @param t 输入向量
 * @return 限制后的向量，每个分量范围[0,1]
 * 
 * @details 对向量的每个分量独立执行saturate操作
 *          常用于颜色向量的归一化处理
 */
vec3 saturate(vec3 t)
{
	return clamp(t, 0.0, 1.0);
}

/**
 * @brief 浮点数线性插值函数
 * @param f1 起始值
 * @param f2 结束值
 * @param a 插值参数[0,1]
 * @return 插值结果
 * 
 * @details 线性插值公式：result = (1-a)*f1 + a*f2
 *          - a=0时返回f1
 *          - a=1时返回f2
 *          - a=0.5时返回中点值
 *          常用于材质混合、动画过渡等
 */
float lerp(float f1, float f2, float a)
{
	return ((1.0 - a) * f1 + a * f2);
}

/**
 * @brief 向量线性插值函数
 * @param v1 起始向量
 * @param v2 结束向量
 * @param a 插值参数[0,1]
 * @return 插值结果向量
 * 
 * @details 对向量的每个分量独立执行线性插值
 *          常用于颜色混合、方向插值、位置动画等
 */
vec3 lerp(vec3 v1, vec3 v2, float a)
{
	return ((1.0 - a) * v1 + a * v2);
}

// ============================================================================
// 法线计算函数 - 切线空间变换
// ============================================================================

/**
 * @brief 计算几何法线（不使用法线贴图）
 * @return 世界空间中的归一化法线向量
 * 
 * @details 当没有法线贴图时，直接返回插值后的几何法线
 *          构建TBN矩阵用于调试和一致性检查
 * 
 * TBN矩阵构建过程：
 * 1. 使用屏幕空间导数计算切线向量
 * 2. 通过Gram-Schmidt正交化确保T⊥N
 * 3. 使用叉积计算副切线B = N × T
 * 4. 构建正交变换矩阵TBN
 */
vec3 calcNormal()
{
    // 计算位置的屏幕空间偏导数
    // dFdx/dFdy获取相邻像素间的差值，用于重建切线空间
    vec3 pos_dx = dFdx(fragPosition);  // X方向位置梯度
    vec3 pos_dy = dFdy(fragPosition);  // Y方向位置梯度
    
    // 计算纹理坐标的屏幕空间偏导数
    vec3 st1    = dFdx(vec3(fragTexCoord, 0.0));  // X方向UV梯度
    vec3 st2    = dFdy(vec3(fragTexCoord, 0.0));  // Y方向UV梯度
    
    // 使用偏导数重建切线向量T
    // 基于纹理坐标和世界位置的关系求解切线方向
    vec3 T      = (st2.t * pos_dx - st1.t * pos_dy) / (st1.s * st2.t - st2.s * st1.t);
    
    // 归一化几何法线
    vec3 N      = normalize(fragNormal);
    
    // Gram-Schmidt正交化：确保切线垂直于法线
    // T' = T - (T·N)N，移除T在N方向的分量
    T           = normalize(T - N * dot(N, T));
    
    // 计算副切线：B = N × T（右手坐标系）
    vec3 B      = normalize(cross(N, T));
    
    // 构建TBN变换矩阵（切线空间到世界空间）
    mat3 TBN    = mat3(T, B, N);
    
    // 返回几何法线（TBN矩阵的第三列）
    return normalize(TBN[2].xyz);
}

/**
 * @brief 计算法线贴图修正后的法线
 * @param n 法线贴图采样值（切线空间，范围[0,1]）
 * @return 世界空间中的归一化法线向量
 * 
 * @details 完整的法线贴图处理流程：
 *          1. 重建切线空间基向量（T, B, N）
 *          2. 将法线贴图从[0,1]转换到[-1,1]
 *          3. 使用TBN矩阵变换到世界空间
 * 
 * 法线贴图原理：
 * - 存储切线空间中的法线扰动
 * - RGB通道对应XYZ分量
 * - 提供表面细节而不增加几何复杂度
 * - 需要正确的切线空间变换才能工作
 */
vec3 calcNormal(vec3 n)
{
    // ========================================================================
    // 1. 重建切线空间基向量（与上面函数相同）
    // ========================================================================
    
    vec3 pos_dx = dFdx(fragPosition);
    vec3 pos_dy = dFdy(fragPosition);
    vec3 st1    = dFdx(vec3(fragTexCoord, 0.0));
    vec3 st2    = dFdy(vec3(fragTexCoord, 0.0));
    vec3 T      = (st2.t * pos_dx - st1.t * pos_dy) / (st1.s * st2.t - st2.s * st1.t);
    vec3 N      = normalize(fragNormal);
    T           = normalize(T - N * dot(N, T));
    vec3 B      = normalize(cross(N, T));
    mat3 TBN    = mat3(T, B, N);

    // ========================================================================
    // 2. 处理法线贴图数据并变换到世界空间
    // ========================================================================
    
    // 将法线贴图从[0,1]范围转换到[-1,1]范围
    // 法线贴图存储格式：RGB = (XYZ + 1) / 2
    // 逆变换：XYZ = RGB * 2 - 1
    vec3 tangent_normal = normalize(2.0 * n - 1.0);
    
    // 使用TBN矩阵将切线空间法线变换到世界空间
    // 世界法线 = TBN × 切线法线
    return normalize(TBN * tangent_normal);
}

// ============================================================================
// 方向光源辅助函数
// ============================================================================

/**
 * @brief 获取方向光源的光线方向
 * @param index 光源在数组中的索引
 * @return 从表面指向光源的方向向量（已归一化）
 * 
 * @details 方向光源模拟无限远的平行光（如太阳光）
 *          - direction字段存储光线传播方向（从光源到场景）
 *          - 返回值取反，得到从表面指向光源的方向
 *          - 用于光照计算中的L向量
 */
vec3 get_directional_light_direction(uint index)
{
	// 取反direction得到光照方向（从表面指向光源）
	return -view.directional_lights[index].direction.xyz;
}

/**
 * @brief 计算方向光源的基础光照贡献
 * @param index 光源在数组中的索引
 * @param normal 表面法线向量（世界空间，已归一化）
 * @return 该光源的光照强度和颜色贡献
 * 
 * @details 简化的Lambert漫反射光照模型：
 *          - 使用Lambert余弦定律计算光照强度
 *          - 光照强度 = max(N·L, 0) * 光源强度 * 光源颜色
 *          - 适用于快速预览和调试，不包含完整PBR计算
 * 
 * 物理原理：
 * - Lambert余弦定律：表面接收的光照强度与入射角余弦成正比
 * - 当光线垂直入射时（N·L=1），接收最大光照
 * - 当光线平行于表面时（N·L=0），不接收光照
 * - 当光线从背面入射时（N·L<0），被表面遮挡
 */
vec3 apply_directional_light(uint index, vec3 normal)
{
	// 获取从表面指向光源的方向向量
	vec3 world_to_light = -view.directional_lights[index].direction.xyz;

	// 确保方向向量已归一化（防止数值误差）
	world_to_light = normalize(world_to_light);

	// 计算Lambert余弦项：N·L
	// clamp确保结果在[0,1]范围内，避免负值光照
	float ndotl = clamp(dot(normal, world_to_light), 0.0, 1.0);

    // 返回最终光照贡献：余弦项 × 光源强度 × 光源颜色
    // color.w存储光源强度（流明值）
    // color.rgb存储光源颜色（线性RGB空间）
    return ndotl * view.directional_lights[index].color.w * view.directional_lights[index].color.rgb;
}

// ============================================================================
// 方向光阴影计算 - PCF软阴影实现
// ============================================================================
/**
 * @brief 计算方向光源的阴影因子（支持PCF软阴影）
 * @param fragPosLightSpace 片段在光源空间中的位置（齐次坐标）
 * @param normal 表面法线向量（世界空间，已归一化）
 * @param lightDir 光线方向向量（从表面指向光源，已归一化）
 * @return 阴影因子[0,1]，0=完全光照，1=完全阴影
 * 
 * 阴影映射原理：
 * 1. 从光源视角渲染场景，记录深度信息到阴影贴图
 * 2. 在主渲染中，将片段位置变换到光源空间
 * 3. 比较片段深度与阴影贴图中的深度
 * 4. 使用PCF技术实现软阴影边缘
 * 
 * PCF（Percentage Closer Filtering）：
 * - 对阴影贴图进行多点采样
 * - 计算每个采样点的阴影测试结果
 * - 对结果进行平均，得到软阴影效果
 * - 有效减少阴影边缘的锯齿现象
 */
float calculateDirectionalShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // ========================================================================
    // 1. 透视除法和坐标变换
    // ========================================================================
    
    // 执行透视除法，将齐次坐标转换为标准化设备坐标（NDC）
    // NDC范围：[-1, 1] × [-1, 1] × [0, 1]（Vulkan深度范围）
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // 将NDC坐标变换到纹理坐标范围[0, 1]
    // X和Y用于阴影贴图采样，Z用于深度比较
    projCoords = projCoords * 0.5 + 0.5;
    
    // ========================================================================
    // 2. 边界检查
    // ========================================================================
    
    // 检查片段是否在阴影贴图覆盖范围内
    // 超出范围的片段不受此光源阴影影响
    if (projCoords.z > 1.0 ||          // 超出远平面
        projCoords.x < 0.0 || projCoords.x > 1.0 ||  // 超出X边界
        projCoords.y < 0.0 || projCoords.y > 1.0) {  // 超出Y边界
        return 1.0; // 边界外无阴影，完全照亮
    }
    
    // ========================================================================
    // 3. 深度采样和比较准备
    // ========================================================================
    
    // 获取当前片段在光源空间中的深度值
    // 这是需要与阴影贴图进行比较的深度
    float currentDepth = projCoords.z;
    
    // ========================================================================
    // 4. 自适应阴影偏置计算
    // ========================================================================
    
    // 🌟 优化自适应偏置计算以增强阴影效果
    // 阴影痤疮：由于深度精度限制导致的自阴影伪影
    // 
    // 优化偏置策略：
    // - 基础偏置：0.001（增大基础偏置以获得更清晰阴影）
    // - 角度偏置：0.003 * (1 - N·L)，减小角度偏置保持阴影细节
    // - 物理原理：平衡阴影清晰度与失真控制
    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(0.003 * (1.0 - cosTheta), 0.001);
    
    // ========================================================================
    // 5. PCF软阴影采样
    // ========================================================================
    
    // 初始化阴影累积值
    float shadow = 0.0;
    
    // 计算阴影贴图的纹素大小
    // 用于确定PCF采样的偏移距离
    vec2 texelSize = 1.0 / textureSize(directional_light_shadow, 0);
    
    // 3×3 PCF采样核心
    // 对当前位置周围9个纹素进行采样和比较
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            // 计算采样位置的偏移
            vec2 offset = vec2(x, y) * texelSize;
            
            // 采样偏移位置的深度值
            // 这是从光源视角看到的最近表面深度
            float pcfDepth = texture(directional_light_shadow, projCoords.xy + offset).r;
            
            // 执行深度比较测试
            // 如果当前深度大于采样深度+偏置，则该点在阴影中
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    
    // 计算9个采样点的平均值
    // 得到[0,1]范围的软阴影因子
    shadow /= 9.0;
    
    // ========================================================================
    // 6. 🌟 阴影对比度增强处理
    // ========================================================================
    
    // 使用幂函数增强阴影对比度，使阴影边缘更加清晰
    float enhancedShadow = pow(shadow, 0.75); // 增强阴影对比度
    float shadowIntensity = 0.85; // 阴影强度系数（保留15%环境光照）
    
    // ========================================================================
    // 7. 返回最终阴影因子
    // ========================================================================
    
    // 返回优化后的光照因子
    // 1.0 = 完全光照（无阴影）
    // 0.15 = 最大阴影强度（保留环境光照）
    // 0.15-1.0 = 增强对比度的软阴影边缘
    return 1.0 - (enhancedShadow * shadowIntensity);
}

// ============================================================================
// 环境光照辅助函数
// ============================================================================

// 反射捕获相关常量定义
#define REFLECTION_CAPTURE_ROUGHEST_MIP 1          // 最粗糙mip级别偏移
#define REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE 1.2 // 粗糙度到mip级别的缩放因子

/**
 * @brief 根据粗糙度计算反射捕获立方体贴图的绝对mip级别
 * @param roughness 表面粗糙度[0,1]
 * @param cubemap_max_mip 立方体贴图的最大mip级别数
 * @return 对应的mip级别，用于环境贴图采样
 * 
 * @details 高级mip级别计算策略：
 *          - 使用启发式算法将粗糙度映射到mip级别
 *          - 确保特定mip级别始终对应相同粗糙度，与贴图mip数量无关
 *          - 更多mip级别只是支持更锐利的反射，不改变粗糙度映射
 * 
 * 物理原理：
 * - 粗糙表面会散射光线，产生模糊的反射
 * - 使用对数映射提供更好的感知线性度
 * - 避免极小粗糙度值导致的数值不稳定
 * 
 * 算法特点：
 * - 基于对数函数的非线性映射
 * - 在低粗糙度区域提供更高精度
 * - 适应不同分辨率的环境贴图
 */
float compute_reflection_mip_from_roughness(float roughness, float cubemap_max_mip)
{
	// 使用启发式算法映射粗糙度到mip级别
	// 这确保了特定mip级别始终对应相同的粗糙度值
	// 无论立方体贴图有多少mip级别，映射关系保持一致
	// 更多mip只是允许支持更锐利的反射效果
	
	// 计算从1x1像素级别开始的偏移
	// 使用对数函数提供非线性映射，在低粗糙度区域有更高精度
	// max(roughness, 0.001)防止log2(0)的数值错误
	float level_from_1x1 = REFLECTION_CAPTURE_ROUGHEST_MIP - REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE * log2(max(roughness, 0.001));
	
	// 从最大mip级别减去计算的偏移，得到实际使用的mip级别
	// cubemap_max_mip - 1：最高mip级别索引（从0开始）
	return cubemap_max_mip - 1 - level_from_1x1;
}

/**
 * @brief Lazarov环境BRDF近似的核心计算函数
 * @param Roughness 表面粗糙度[0,1]
 * @param NoV 法线与视线夹角的余弦值
 * @return 环境BRDF积分的AB系数
 * 
 * @details 基于Lazarov 2013年在《使命召唤：黑色行动2》中的研究
 *          "Getting More Physical in Call of Duty: Black Ops II"
 *          经过调整以适配我们的几何项G
 * 
 * 物理背景：
 * - 环境光照的BRDF积分在实时渲染中计算成本过高
 * - 使用Split Sum近似将积分分解为两部分
 * - 该函数计算预积分的BRDF查找表近似值
 * 
 * 数学模型：
 * - 使用多项式拟合大量预计算的BRDF积分结果
 * - c0, c1是基于真实材质测量数据优化的拟合系数
 * - 考虑了粗糙度和视角的复合影响
 */
vec2 EnvBRDFApproxLazarov(float Roughness, float NoV)
{
	// Lazarov 2013年研究成果："Getting More Physical in Call of Duty: Black Ops II"
	// 经过调整以适配我们使用的几何项G函数
	
	// 拟合系数：基于大量材质的BRDF积分数据优化
	// 这些常数通过最小二乘法拟合预计算的积分结果得出
	const vec4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const vec4 c1 = { 1, 0.0425, 1.04, -0.04 };
	
	// 第一步：基于粗糙度的线性插值
	// 在两组系数间根据粗糙度进行插值
	vec4 r = Roughness * c0 + c1;
	
	// 第二步：考虑视角依赖性的非线性修正
	// exp2(-9.28 * NoV)项模拟掠射角处的特殊BRDF行为
	// min操作防止数值溢出，确保计算稳定性
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	
	// 第三步：计算最终的AB积分系数
	// AB.x：Fresnel项的积分贡献
	// AB.y：几何项和分布项的积分贡献
	vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
	
	return AB;
}

/**
 * @brief 完整的环境BRDF近似函数
 * @param SpecularColor 材质的镜面反射颜色（F0值）
 * @param Roughness 表面粗糙度[0,1]
 * @param NoV 法线与视线夹角的余弦值
 * @return 近似的环境BRDF值
 * 
 * @details 这是实时PBR渲染中广泛使用的环境光照近似
 *          结合了Fresnel反射和预积分的BRDF查找表
 * 
 * 物理原理：
 * - 完整的环境光照需要对BRDF进行半球积分
 * - Split Sum近似将积分分解为光照项和BRDF项
 * - 该函数处理BRDF项的快速近似计算
 * 
 * 实现特点：
 * - 考虑材质的镜面反射特性
 * - 处理低反射率材质的特殊情况
 * - 确保物理合理的反射强度范围
 */
vec3 EnvBRDFApprox( vec3 SpecularColor, float Roughness, float NoV )
{
	// 获取Lazarov近似的AB积分系数
	vec2 AB = EnvBRDFApproxLazarov(Roughness, NoV);

	// 处理低反射率材质的特殊情况
	// 任何小于2%的反射率在物理上是不可能的，被视为阴影效应
	// 注意：这对于'specular'显示标志的正常工作是必需的
	// 因为该标志使用SpecularColor为0的情况
	// 
	// 计算F90（掠射角反射率）：
	// - 使用绿色通道作为亮度参考（人眼对绿光最敏感）
	// - 乘以50.0将2%基准映射到合理的F90值
	// - saturate确保结果在[0,1]范围内
	float F90 = saturate( 50.0 * SpecularColor.g );

	// 最终环境BRDF计算：
	// SpecularColor * AB.x：基于材质F0的Fresnel积分贡献
	// F90 * AB.y：掠射角反射的额外贡献
	// 这模拟了Split Sum近似的完整BRDF积分
	return SpecularColor * AB.x + F90 * AB.y;
}

/**
 * @brief 计算镜面反射的环境遮蔽系数
 * @param NoV 法线与视线夹角的余弦值
 * @param RoughnessSq 粗糙度的平方值
 * @param AO 环境遮蔽值[0,1]
 * @return 修正后的镜面遮蔽系数[0,1]
 * 
 * @details 物理原理：
 *          - 环境遮蔽不仅影响漫反射，也显著影响镜面反射
 *          - 粗糙表面的镜面反射更容易被几何细节遮蔽
 *          - 掠射角观察时，遮蔽效应会减弱（Fresnel效应）
 * 
 * 算法特点：
 * - 考虑视角依赖性：掠射角时减少遮蔽强度
 * - 考虑表面粗糙度：粗糙表面遮蔽更明显
 * - 基于经验公式，在视觉上提供合理的遮蔽效果
 * - 确保遮蔽值的连续性和物理合理性
 */
float GetSpecularOcclusion(float NoV, float RoughnessSq, float AO)
{
	// 计算视角和环境遮蔽的复合影响
	// NoV + AO的物理意义：
	// - NoV：掠射角时增加可见性（Fresnel效应）
	// - AO：提供基础的几何遮蔽信息
	// - 两者相加模拟了视角对遮蔽的影响
	float combined_factor = NoV + AO;
	
	// 应用粗糙度权重的幂函数调制
	// pow(combined_factor, RoughnessSq)的效果：
	// - RoughnessSq越大（表面越粗糙），指数越大
	// - 粗糙表面的遮蔽效应更强，镜面反射更容易被阻挡
	// - 光滑表面（RoughnessSq接近0）时，幂函数接近1，遮蔽较弱
	float occlusion_factor = pow(combined_factor, RoughnessSq);
	
	// 最终遮蔽计算：occlusion_factor - 1 + AO
	// 这个公式确保：
	// 1. 当NoV=0, AO=1时，结果为1（无遮蔽）
	// 2. 当NoV=1, AO=0时，结果接近0（最大遮蔽）
	// 3. 结果始终不低于原始AO值，保持遮蔽的连续性
	// saturate确保最终结果在[0,1]范围内
	return saturate( occlusion_factor - 1 + AO );
}

// ============================================================================
// 材质参数计算函数
// ============================================================================

/**
 * @brief 将电介质镜面反射参数转换为F0值
 * @param Specular 镜面反射强度参数[0,1]
 * @return 对应的F0反射率值
 * 
 * @details 电介质材质的F0值计算：
 *          - 大多数电介质的F0约为0.04（4%）
 *          - Specular参数用于艺术控制，调整反射强度
 *          - 公式：F0 = 基础F0 × 2 × Specular
 *          - 允许艺术家在物理合理范围内调整反射率
 */
float DielectricSpecularToF0(float Specular)
{
	// 基于标准电介质F0值（0.04）计算实际F0
	// 乘以2是为了提供更大的调整范围
	return F0.x * 2.0f * Specular;
}

/**
 * @brief 计算材质的基础反射率F0
 * @param Specular 镜面反射强度参数[0,1]
 * @param BaseColor 材质基础颜色（反照率）
 * @param Metallic 金属度参数[0,1]
 * @return 计算得到的F0反射率向量
 * 
 * @details PBR材质的F0计算规则：
 *          - 电介质：F0 = 灰度值（通常0.02-0.05）
 *          - 金属：F0 = 基础颜色（BaseColor）
 *          - 混合材质：在两者间线性插值
 * 
 * 物理原理：
 * - 电介质的反射率与颜色无关，为灰度值
 * - 金属的反射率等于其固有颜色
 * - 现实中不存在部分金属材质，但为了艺术控制允许混合
 */
vec3 ComputeF0(float Specular, vec3 BaseColor, float Metallic)
{
	// 限制基础颜色的最小值，避免纯黑色导致的数值问题
	// 同时为透明涂层等特殊效果预留空间
	BaseColor = clamp(BaseColor, F0, vec3(1.0f));
	
	// 计算电介质的F0值（灰度）
	vec3 dielectric_f0 = vec3(DielectricSpecularToF0(Specular));
	
	// 在电介质F0和金属BaseColor之间线性插值
	// Metallic=0：使用电介质F0
	// Metallic=1：使用BaseColor作为F0
	return lerp(dielectric_f0, BaseColor, Metallic);
}

// ============================================================================
// 主着色器函数 - PBR光照管线入口点
// ============================================================================
/**
 * @brief 片段着色器主函数，实现完整的PBR光照计算
 * 
 * 主要处理流程：
 * 1. 材质参数采样（基础颜色、金属度、粗糙度、法线、AO）
 * 2. 几何参数计算（法线、视线方向、反射率）
 * 3. 直接光照计算（方向光源的漫反射和镜面反射）
 * 4. 阴影计算（PCF软阴影技术）
 * 5. 间接光照计算（环境光和IBL反射）
 * 6. 色调映射和伽马校正
 * 7. 最终颜色输出
 */
void main()
{
	// ========================================================================
	// 1. 材质纹理采样和参数提取
	// ========================================================================
	
	// 调试参数（可用于快速测试材质效果）
    //vec3 base_color = vec3(0.3);      // 固定基础颜色
    //float metallic = 1.0;             // 固定金属度
    //float roughness = 0.1;            // 固定粗糙度
    //vec3 normal = calcNormal();       // 几何法线
    //vec3 ambient_occlution = vec3(1.0); // 无环境遮蔽

    // 从纹理采样获取真实材质参数
    // 基础颜色纹理（反照率）：存储材质的固有颜色，sRGB空间
    vec3 base_color = texture(sampler1, fragTexCoord).rgb;
    
    // 金属度纹理：单通道，0=电介质，1=金属，影响反射特性
    float metallic = saturate(texture(sampler2, fragTexCoord).r);
    
    // 粗糙度纹理：单通道，0=镜面，1=完全粗糙，控制反射锐利度
    float roughness = saturate(texture(sampler3, fragTexCoord).r);
    
    // 法线贴图：切线空间法线，用于表面细节增强
    vec3 normal = calcNormal(texture(sampler4, fragTexCoord).rgb);
    
    // 环境遮蔽贴图：模拟几何自遮挡，增强深度感
    vec3 ambient_occlution = texture(sampler5, fragTexCoord).rgb;

    // 限制粗糙度最小值，避免数值不稳定和过度锐利的反射
    // 完全光滑的表面在现实中不存在，最小值0.01提供合理的物理约束
    roughness = max(0.01, roughness);

	// ========================================================================
	// 2. 几何和光照向量计算
	// ========================================================================

	// 表面法线向量（世界空间，已归一化）
	vec3 N = normal;
	
	// 视线方向向量：从表面点指向观察者（相机）
	// 用于计算Fresnel反射、几何遮蔽等与视角相关的效应
	vec3 V = normalize(view.camera_position.xyz - fragPosition);
	
	// 法线与视线的夹角余弦值，用于各种BRDF计算
	// 掠射角时NdotV接近0，垂直观察时接近1
	float NdotV = saturate(dot(N, V));

	// ========================================================================
	// 3. 直接光照计算 - Disney漫反射 + GGX镜面反射
	// ========================================================================
	
	// 初始化直接光照累积器
	// 这里将计算所有直接光源（方向光、点光源、聚光灯）的贡献
	vec3 direct_lighting = vec3(0.0);
	
	// 计算漫反射颜色：基础颜色 × (1 - 金属度)
	// 金属材质没有漫反射，所有光线都被镜面反射
	// 电介质材质同时具有漫反射和镜面反射
	vec3 diffuse_color = base_color.rgb * (1.0 - metallic);
	
    // 遍历所有方向光源进行光照计算
    for (uint i = 0u; i < DIRECTIONAL_LIGHTS; ++i)
    {
        // 获取光源方向：从表面指向光源的单位向量
        vec3 L = get_directional_light_direction(i);
        
        // 计算半角向量：光线和视线的角平分线
        // 用于镜面反射计算，表示完美反射的微表面法线方向
        vec3 H = normalize(V + L);

        // 计算各种点积，用于BRDF函数
        float LdotH = saturate(dot(L, H));  // 光线与半角向量夹角
        float NdotH = saturate(dot(N, H));  // 法线与半角向量夹角
        float NdotL = saturate(dot(N, L));  // 法线与光线夹角（Lambert项）

        // ====================================================================
        // 镜面反射BRDF计算（Cook-Torrance微表面模型）
        // ====================================================================
        
        // Fresnel反射系数计算
        // F90：掠射角反射率，基于材质的镜面反射强度
        float F90 = saturate(50.0 * F0.r);
        vec3  F   = F_Schlick(F0, F90, LdotH);  // Schlick近似Fresnel项
        
        // 几何遮蔽函数：考虑微表面间的相互遮蔽和阴影
        float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
        
        // 法线分布函数：描述微表面法线在半角向量方向的分布概率
        float D   = D_GGX(NdotH, roughness);
        
        // 镜面反射BRDF：Fr = F × D × Vis
        // 这是Cook-Torrance微表面BRDF的完整形式
        vec3  Fr  = F * D * Vis;

        // ====================================================================
        // 漫反射BRDF计算（Disney漫反射模型）
        // ====================================================================
        
        // Disney漫反射：考虑粗糙度影响的能量守恒漫反射模型
        // 比传统Lambert模型更准确，在掠射角有更好的表现
        float Fd = Fr_DisneyDiffuse(NdotV, NdotL, LdotH, roughness);

		// 计算最终的漫反射和镜面反射贡献
		// 漫反射 = 漫反射颜色 × (1 - Fresnel) × 漫反射BRDF
		// (1-F)项确保能量守恒：反射越强，透射（漫反射）越弱
		vec3 direct_diffuse = diffuse_color * (vec3(1.0) - F) * Fd;
		
		// 镜面反射直接使用计算得到的BRDF值
		vec3 direct_specular = Fr;

		// ====================================================================
        // 阴影计算（使用PCF软阴影技术）
        // ====================================================================
        
        // 将当前片段位置变换到光源空间坐标系
        // 用于在阴影贴图中查找对应的深度值
        vec4 fragPosLightSpace = ubo.directional_light_proj_view * vec4(fragPosition, 1.0);
        
        // 计算阴影因子（0=完全阴影，1=完全光照）
        // 使用PCF（Percentage Closer Filtering）实现软阴影边缘
        float shadow = calculateDirectionalShadow(fragPosLightSpace, N, L);
        
        // 添加最小环境光强度，确保阴影区域仍有基础可见度
        // 这模拟了现实中的天空光、反射光等间接光照效应
        float ambientStrength = 0.15;  // 环境光强度（15%）
        shadow = max(shadow, ambientStrength);

		// 未来改进方向的注释标记
		// TODO : 添加能量守恒（镜面反射层对漫反射分量的衰减）
		// TODO : 添加镜面微表面多重散射项（能量守恒）

        // 累积当前光源的最终贡献到直接光照中
        // 光源强度 × (漫反射 + 镜面反射) × 阴影衰减
        direct_lighting += apply_directional_light(i, N) * (direct_diffuse + direct_specular) * shadow;
    }

	// ========================================================================
	// 4. 间接光照计算 - 简化的Lambert漫反射环境光
	// ========================================================================
	
	// 简化的间接漫反射光照模型
	// 在完整PBR管线中，这里应该使用辐照度贴图（Irradiance Map）
	// 当前使用Lambert模型：漫反射颜色 / π × 环境遮蔽
	vec3 indirect_lighting = diffuse_color.rgb / PI * ambient_occlution;

	// ========================================================================
	// 5. 基于图像的光照（IBL）- 镜面反射环境光
	// ========================================================================
	
	// 计算材质的F0反射率（用于环境反射）
	vec3 specular = ComputeF0(0.5, base_color, metallic);
	
	// 环境BRDF近似：预计算的BRDF积分查找表
	// 避免实时计算复杂的BRDF卷积积分
	vec3 reflection_brdf = EnvBRDFApprox(specular, roughness, NdotV);
	
    // 计算折射向量（这里可能是反射向量的错误实现）
    // 注意：对于PBR镜面反射，应该使用reflect而不是refract
    float ratio = 1.00 / 1.52;  // 折射率比值
    vec3 I = V;                  // 入射方向
	vec3 R = refract(I, normalize(N), ratio);  // 折射向量
	
	// 根据粗糙度计算环境贴图的mip级别
	// 粗糙表面需要更模糊的环境反射
	float mip = compute_reflection_mip_from_roughness(roughness, SKY_MAXMIPS);
	
    // 从天空盒采样环境光照，使用计算得到的mip级别
    // 乘以10.0是HDR环境贴图的强度调整因子
    vec3 reflection_L = textureLod(skycube, R, mip).rgb * 10.0;
	
	// 计算镜面遮蔽：考虑粗糙度和AO对环境反射的影响
	float reflection_V = GetSpecularOcclusion(NdotV, roughness * roughness, ambient_occlution.x);
	
	// 最终环境反射颜色：环境光 × 可见性 × BRDF
	vec3 reflection_color = reflection_L * reflection_V * reflection_brdf;
    
	// ========================================================================
	// 6. 最终颜色合成和后处理
	// ========================================================================

    // 组合所有光照分量：直接光照 + 间接光照 + 环境反射
    // 间接光照乘以0.3是强度调整因子，避免过度明亮
    vec3 final_color = direct_lighting + indirect_lighting * 0.3 + reflection_color;
	
	// 调试选项：仅显示基础颜色和环境遮蔽
	//final_color = base_color * ambient_occlution;
	
    // 伽马校正：将线性空间颜色转换为sRGB显示空间
    // 1/2.2 ≈ 0.4545，标准sRGB伽马值
    // 注意：这里应该先进行色调映射，再进行伽马校正
	final_color = pow(final_color, vec3(0.4545));

	// 输出最终颜色，alpha通道设为1.0（不透明）
	outColor = vec4(final_color, 1.0);
}