// ============================================================================
// PBR光照计算 - 基于物理的渲染管线
// ============================================================================

// ========================================================================
// 1. 视角和反射向量计算
// ========================================================================
/**
 * 视角向量V：从表面点指向观察者（相机）的单位向量
 * 反射向量R：视角向量关于表面法线的镜面反射向量
 * 
 * 用途：
 * - V：用于BRDF计算中的视角相关项
 * - R：用于环境反射贴图采样
 */
highp vec3 V = normalize(camera_position - in_world_position);  // 视角方向
highp vec3 R = reflect(-V, N);                                 // 反射方向（用于IBL）

// ========================================================================
// 2. 立方体贴图采样坐标变换
// ========================================================================
/**
 * 将法线和反射向量从世界空间变换到立方体贴图采样空间
 * 坐标系变换：Y-up世界空间 -> Z-up立方体贴图空间
 */
highp vec3 origin_samplecube_N = vec3(N.x, N.z, N.y);  // 法线向量坐标变换
highp vec3 origin_samplecube_R = vec3(R.x, R.z, R.y);  // 反射向量坐标变换

// ========================================================================
// 3. 材质基础反射率计算（F0）
// ========================================================================
/**
 * 计算材质在垂直入射时的反射率（F0值）
 * 
 * 物理背景：
 * - 电介质材料（塑料、玻璃等）：F0 = dielectric_specular（通常为0.04）
 * - 金属材料：F0 = 基础颜色（高反射率）
 * 
 * 金属度工作流：
 * - metallic = 0：纯电介质，F0 = dielectric_specular
 * - metallic = 1：纯金属，F0 = basecolor
 * - 0 < metallic < 1：混合材质，线性插值
 */
highp vec3 F0 = mix(vec3(dielectric_specular, dielectric_specular, dielectric_specular), basecolor, metallic);

// ========================================================================
// 4. 直接光照积分 - 点光源计算
// ========================================================================
/**
 * 渲染方程：Lo(p,ωo) = ∫Ω fr(p,ωi,ωo) * Li(p,ωi) * (n·ωi) dωi
 * 
 * 离散化为：Lo = Σ fr(p,ωi,ωo) * Li(p,ωi) * (n·ωi)
 * 
 * 其中：
 * - fr：BRDF函数
 * - Li：入射光辐射度
 * - (n·ωi)：Lambert余弦定律
 */
// direct light specular and diffuse BRDF contribution
highp vec3 Lo = vec3(0.0, 0.0, 0.0);  // 累积的出射辐射度
for (highp int light_index = 0; light_index < int(point_light_num) && light_index < m_max_point_light_count;
     ++light_index)
{
    // 获取点光源参数
    highp vec3  point_light_position = scene_point_lights[light_index].position;
    highp float point_light_radius   = scene_point_lights[light_index].radius;

    // 计算光源方向和Lambert余弦项
    highp vec3  L   = normalize(point_light_position - in_world_position);  // 光源方向
    highp float NoL = min(dot(N, L), 1.0);                                 // Lambert余弦项

    // ====================================================================
    // 点光源衰减计算
    // ====================================================================
    /**
     * 点光源特性：
     * - 从一个点向所有方向均匀发光
     * - 光强随距离平方衰减（物理准确）
     * - 额外的半径衰减用于控制光源影响范围
     */
    // point light
    highp float distance             = length(point_light_position - in_world_position);
    highp float distance_attenuation = 1.0 / (distance * distance + 1.0);  // 平方反比定律（+1避免除零）
    highp float radius_attenuation   = 1.0 - ((distance * distance) / (point_light_radius * point_light_radius));  // 半径衰减

    // 综合衰减系数
    highp float light_attenuation = radius_attenuation * distance_attenuation * NoL;
    if (light_attenuation > 0.0)
    {
        // ================================================================
        // 点光源阴影计算 - 立方体阴影贴图
        // ================================================================
        /**
         * 立方体阴影贴图技术：
         * 1. 从点光源位置向6个方向渲染深度贴图
         * 2. 使用球面投影将3D方向映射到2D纹理坐标
         * 3. 比较片段深度与阴影贴图深度值
         */
        highp float shadow;
        {
            // world space to light view space
            // identity rotation
            // Z - Up
            // Y - Forward
            // X - Right
            highp vec3 position_view_space = in_world_position - point_light_position;

            // 归一化得到球面坐标
            highp vec3 position_spherical_function_domain = normalize(position_view_space);

            // ============================================================
            // 球面到平面投影（双抛物面投影）
            // ============================================================
            // use abs to avoid divergence
            // z > 0
            // (x_2d, y_2d, 0) + (0, 0, 1) = λ ((x_sph, y_sph, z_sph) + (0, 0, 1))
            // (x_2d, y_2d) = (x_sph, y_sph) / (z_sph + 1)
            // z < 0
            // (x_2d, y_2d, 0) + (0, 0, -1) = λ ((x_sph, y_sph, z_sph) + (0, 0, -1))
            // (x_2d, y_2d) = (x_sph, y_sph) / (-z_sph + 1)
            highp vec2 position_ndcxy =
                position_spherical_function_domain.xy / (abs(position_spherical_function_domain.z) + 1.0);

            // 转换为纹理坐标并计算层索引
            // use sign to avoid divergence
            // -1.0 to 0
            // 1.0 to 1
            highp vec2  uv = ndcxy_to_uv(position_ndcxy);
            highp float layer_index =
                (0.5 + 0.5 * sign(position_spherical_function_domain.z)) + 2.0 * float(light_index);

            // 阴影测试
            highp float depth          = texture(point_lights_shadow, vec3(uv, layer_index)).r + 0.000075;  // 深度偏移避免阴影痤疮
            highp float closest_length = (depth)*point_light_radius;

            highp float current_length = length(position_view_space);

            shadow = (closest_length >= current_length) ? 1.0f : -1.0f;  // 阴影测试结果
        }

        // 应用阴影和BRDF计算
        if (shadow > 0.0f)
        {
            highp vec3 En = scene_point_lights[light_index].intensity * light_attenuation;  // 入射辐射度
            Lo += BRDF(L, V, N, F0, basecolor, metallic, roughness) * En;                  // 渲染方程
        }
    }
};

// ========================================================================
// 5. 环境光照贡献
// ========================================================================
/**
 * 简单环境光模型：
 * - 提供基础的全局照明
 * - 防止完全黑暗的区域
 * - 通常用作更复杂IBL的补充
 */
// direct ambient contribution
highp vec3 La = vec3(0.0f, 0.0f, 0.0f);
La            = basecolor * ambient_light;  // 环境光 = 基础颜色 × 环境光强度

// ========================================================================
// 6. 基于图像的光照（IBL）- 间接环境光照
// ========================================================================
/**
 * IBL技术包含两个主要部分：
 * 1. 漫反射IBL：使用辐照度贴图计算漫反射环境光
 * 2. 镜面反射IBL：使用预过滤环境贴图和BRDF LUT计算镜面反射
 */

// ====================================================================
// 6.1 漫反射IBL计算
// ====================================================================
/**
 * 漫反射IBL原理：
 * - 对半球范围内的环境光进行积分
 * - 使用预计算的辐照度贴图加速计算
 * - 辐照度贴图 = 卷积后的环境贴图
 */
// indirect environment
highp vec3 irradiance = texture(irradiance_sampler, origin_samplecube_N).rgb;  // 采样辐照度贴图
highp vec3 diffuse    = irradiance * basecolor;                               // 漫反射IBL

// ====================================================================
// 6.2 镜面反射IBL计算
// ====================================================================
/**
 * 镜面反射IBL使用分离求和近似：
 * ∫ Li(l) * fr(l,v) * cos(θl) dl ≈ (∫ Li(l) * cos(θl) dl) * (∫ fr(l,v) * cos(θl) dl)
 * 
 * 第一项：预过滤环境贴图（基于粗糙度的不同LOD）
 * 第二项：BRDF积分贴图（2D LUT纹理）
 */

// 计算Fresnel反射率（考虑粗糙度修正）
highp vec3 F       = F_SchlickR(clamp(dot(N, V), 0.0, 1.0), F0, roughness);

// 采样BRDF积分LUT（x=NdotV, y=roughness）
highp vec2 brdfLUT = texture(brdfLUT_sampler, vec2(clamp(dot(N, V), 0.0, 1.0), roughness)).rg;

// 根据粗糙度选择合适的mipmap级别采样预过滤环境贴图
highp float lod        = roughness * MAX_REFLECTION_LOD;                           // LOD级别
highp vec3  reflection = textureLod(specular_sampler, origin_samplecube_R, lod).rgb;  // 预过滤反射
highp vec3  specular   = reflection * (F * brdfLUT.x + brdfLUT.y);                // 镜面反射IBL

// ====================================================================
// 6.3 能量守恒处理
// ====================================================================
/**
 * 能量守恒原理：
 * - 反射能量 + 折射能量 = 入射能量
 * - kD（漫反射系数）= 1 - kS（镜面反射系数）
 * - 金属材质不产生漫反射，只有镜面反射
 */
highp vec3 kD = 1.0 - F;        // 漫反射系数 = 1 - 镜面反射系数
kD *= 1.0 - metallic;           // 金属度越高，漫反射越少
highp vec3 Libl = (kD * diffuse + specular);  // 总IBL贡献 = 漫反射IBL + 镜面反射IBL

// ========================================================================
// 7. 方向光源光照计算（含阴影）
// ========================================================================
/**
 * 方向光源特性：
 * - 模拟无限远光源（如太阳）
 * - 所有光线平行，无衰减
 * - 支持阴影贴图技术
 */
// directional light
{
    // 光源方向（所有点的光线方向相同）
    highp vec3  L   = normalize(scene_directional_light.direction);
    highp float NoL = min(dot(N, L), 1.0);  // Lambert余弦项

    // 只处理朝向光源的表面
    if (NoL > 0.0)
    {
        // ================================================================
        // 方向光阴影计算 - 标准阴影贴图
        // ================================================================
        /**
         * 阴影贴图原理：
         * 1. 从光源视角渲染场景，记录深度值到阴影贴图
         * 2. 从相机视角渲染时，将片段位置变换到光源空间
         * 3. 比较片段深度与阴影贴图中的深度值
         * 4. 如果片段更远，则处于阴影中
         */
        highp float shadow;
        {
            // 将世界坐标变换到光源的裁剪空间
            highp vec4 position_clip = directional_light_proj_view * vec4(in_world_position, 1.0);
            highp vec3 position_ndc  = position_clip.xyz / position_clip.w;  // 透视除法得到NDC坐标

            // NDC坐标转换为纹理坐标
            highp vec2 uv = ndcxy_to_uv(position_ndc.xy);

            // 阴影测试
            highp float closest_depth = texture(directional_light_shadow, uv).r + 0.000075;  // 深度偏移避免阴影痤疮
            highp float current_depth = position_ndc.z;

            shadow = (closest_depth >= current_depth) ? 1.0f : -1.0f;  // 阴影测试结果
        }

        // 应用阴影和BRDF计算
        if (shadow > 0.0f)
        {
            highp vec3 En = scene_directional_light.color * NoL;                        // 入射辐射度
            Lo += BRDF(L, V, N, F0, basecolor, metallic, roughness) * En;              // 渲染方程
        }
    }
}

// ========================================================================
// 8. 最终颜色合成
// ========================================================================
/**
 * 最终颜色 = 直接光照 + 环境光 + 间接光照（IBL）
 * 
 * 组成部分：
 * - Lo：直接光照贡献（点光源 + 方向光源）
 * - La：简单环境光贡献
 * - Libl：基于图像的间接光照贡献（IBL）
 */
// result
result_color = Lo + La + Libl;  // 最终PBR光照结果
