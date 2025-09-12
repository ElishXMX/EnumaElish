# PBR（基于物理的渲染）和阴影计算技术文档

## 目录
1. [概述](#概述)
2. [PBR理论基础](#pbr理论基础)
3. [阴影映射技术](#阴影映射技术)
4. [Shader实现详解](#shader实现详解)
5. [性能优化建议](#性能优化建议)
6. [参数调整指南](#参数调整指南)
7. [实现效果对比](#实现效果对比)

---

## 概述

本文档详细介绍了EnumaElish引擎中基于物理的渲染（PBR）和阴影计算的完整实现。PBR是现代实时渲染的核心技术，它基于物理学原理模拟光线与材质的交互，提供更加真实和一致的渲染效果。

### 核心特性
- **微表面理论**：基于统计学的表面粗糙度建模
- **能量守恒**：确保反射光线不超过入射光线
- **物理准确性**：遵循菲涅尔定律和光学原理
- **软阴影技术**：PCF（百分比渐进过滤）实现平滑阴影边缘
- **多光源支持**：点光源、方向光源和环境光照

---

## PBR理论基础

### 1. 渲染方程（Rendering Equation）

PBR的核心是渲染方程，描述了光线在表面的散射行为：

```
L_o(p,ω_o) = ∫_Ω f_r(p,ω_i,ω_o) L_i(p,ω_i) n·ω_i dω_i
```

**符号说明：**
- `L_o(p,ω_o)`：从点p沿方向ω_o的出射辐射度
- `L_i(p,ω_i)`：从方向ω_i到达点p的入射辐射度
- `f_r(p,ω_i,ω_o)`：双向反射分布函数（BRDF）
- `n·ω_i`：入射角余弦值
- `Ω`：半球积分域

### 2. 微表面理论（Microfacet Theory）

微表面理论将表面建模为无数微小镜面的集合，每个微面都有自己的法向量。

#### 2.1 法线分布函数（Normal Distribution Function, NDF）

**GGX/Trowbridge-Reitz分布：**

```
D(h) = α² / (π * ((n·h)² * (α² - 1) + 1)²)
```

**参数说明：**
- `α = roughness²`：粗糙度参数
- `h`：半向量（光线方向和视线方向的中间向量）
- `n·h`：法向量与半向量的点积

**物理意义：**
- 描述微表面法向量的统计分布
- α越小，表面越光滑，高光越集中
- α越大，表面越粗糙，高光越分散

#### 2.2 几何函数（Geometry Function）

**Smith几何函数：**

```
G(l,v,h) = G₁(l) * G₁(v)
```

其中单向遮蔽函数：

```
G₁(v) = 2 / (1 + √(1 + α² * tan²θ_v))
```

**Schlick-GGX近似：**

```
G₁(v) = (n·v) / ((n·v) * (1 - k) + k)
```

其中：
```
k = (roughness + 1)² / 8  // 直接光照
k = roughness² / 2        // IBL
```

**物理意义：**
- 描述微表面间的相互遮挡和阴影
- 考虑光线被微表面遮挡的概率
- 粗糙表面遮挡效应更明显

#### 2.3 菲涅尔方程（Fresnel Equation）

**Schlick菲涅尔近似：**

```
F(h,v) = F₀ + (1 - F₀) * (1 - (h·v))⁵
```

**粗糙度修正版本：**

```
F_roughness(n,v) = F₀ + (max(1 - roughness, F₀) - F₀) * (1 - (n·v))⁵
```

**参数说明：**
- `F₀`：垂直入射时的菲涅尔反射率
- 金属：F₀ = albedo
- 非金属：F₀ ≈ 0.04

**物理意义：**
- 描述不同角度下的反射强度
- 掠射角反射更强（如水面反射）
- 材质类型决定基础反射率

### 3. BRDF组合

**Cook-Torrance BRDF：**

```
f_r = k_d * f_lambert + k_s * f_cook-torrance
```

**漫反射部分：**
```
f_lambert = albedo / π
```

**镜面反射部分：**
```
f_cook-torrance = D(h) * G(l,v,h) * F(h,v) / (4 * (n·l) * (n·v))
```

**能量守恒：**
```
k_s = F(h,v)
k_d = (1 - k_s) * (1 - metallic)
```

---

## 阴影映射技术

### 1. 基础阴影映射

#### 1.1 算法原理

阴影映射是一种两遍渲染技术：

1. **第一遍（阴影贴图生成）：**
   - 从光源视角渲染场景
   - 只记录深度信息到阴影贴图
   - 深度值表示光源到最近表面的距离

2. **第二遍（阴影测试）：**
   - 从相机视角渲染场景
   - 将片段坐标变换到光源空间
   - 比较片段深度与阴影贴图深度

#### 1.2 坐标变换

**光源空间变换矩阵：**
```
M_light = P_light * V_light * M_model
```

**NDC到纹理坐标变换：**
```
uv = (ndc.xy + 1.0) * 0.5
depth = ndc.z
```

#### 1.3 深度比较

```glsl
float shadowTest = currentDepth > shadowMapDepth + bias ? 1.0 : 0.0;
```

### 2. PCF软阴影（Percentage Closer Filtering）

#### 2.1 算法原理

PCF通过多点采样平滑阴影边缘：

1. 在阴影贴图上进行多点采样
2. 对每个采样点执行深度比较
3. 计算被遮挡采样点的百分比
4. 得到0-1之间的软阴影值

#### 2.2 实现细节

**3×3 PCF核心：**
```glsl
float shadow = 0.0;
vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

for(int x = -1; x <= 1; ++x) {
    for(int y = -1; y <= 1; ++y) {
        vec2 offset = vec2(x, y) * texelSize;
        float pcfDepth = texture(shadowMap, uv + offset).r;
        shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }
}
shadow /= 9.0;
```

#### 2.3 高级PCF技术

**泊松盘采样：**
- 使用预定义的泊松分布采样点
- 减少采样规律性，避免摩尔纹
- 提供更自然的软阴影效果

**自适应PCF：**
- 根据距离光源的远近调整采样半径
- 近处阴影锐利，远处阴影柔和
- 模拟真实世界的阴影特性

### 3. 阴影失真处理

#### 3.1 阴影粉刺（Shadow Acne）

**产生原因：**
- 深度缓冲区精度限制
- 表面法向量与光线方向夹角
- 阴影贴图分辨率不足

**解决方案：**
```glsl
// 固定偏置
float bias = 0.005;

// 自适应偏置
float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);

// 法向量偏移
vec3 offsetPos = worldPos + normal * bias;
```

#### 3.2 彼得潘效应（Peter Panning）

**产生原因：**
- 偏置过大导致阴影与物体分离

**解决方案：**
- 使用更高精度的深度格式
- 优化光源视锥体设置
- 采用级联阴影贴图

---

## Shader实现详解

### 1. PBR片段着色器核心

#### 1.1 材质参数采样

```glsl
// 基础材质属性
vec3 albedo = texture(albedoMap, texCoord).rgb;
float metallic = texture(metallicMap, texCoord).r;
float roughness = texture(roughnessMap, texCoord).r;
float ao = texture(aoMap, texCoord).r;
vec3 normal = getNormalFromMap(normalMap, texCoord);

// 基础反射率计算
vec3 F0 = mix(vec3(0.04), albedo, metallic);
```

#### 1.2 光照计算循环

```glsl
vec3 Lo = vec3(0.0);

// 直接光照计算
for(int i = 0; i < lightCount; ++i) {
    // 光线方向和衰减
    vec3 L = normalize(lights[i].position - worldPos);
    vec3 H = normalize(V + L);
    float distance = length(lights[i].position - worldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = lights[i].color * attenuation;
    
    // BRDF计算
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    // Cook-Torrance BRDF
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    // 能量守恒
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    // 最终贡献
    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;
}
```

#### 1.3 环境光照（IBL）

```glsl
// 环境漫反射
vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
vec3 kS = F;
vec3 kD = 1.0 - kS;
kD *= 1.0 - metallic;

vec3 irradiance = texture(irradianceMap, N).rgb;
vec3 diffuse = irradiance * albedo;

// 环境镜面反射
vec3 R = reflect(-V, N);
const float MAX_REFLECTION_LOD = 4.0;
vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

// 环境光照合成
vec3 ambient = (kD * diffuse + specular) * ao;
vec3 color = ambient + Lo;
```

### 2. 阴影计算实现

#### 2.1 阴影贴图生成

**顶点着色器（shadow.vert）：**
```glsl
// 光源空间变换
gl_Position = lightProjection * lightView * model * vec4(position, 1.0);
```

**片段着色器（shadow.frag）：**
```glsl
// 空着色器，深度自动写入
void main() {
    // GPU自动写入gl_FragDepth
}
```

#### 2.2 阴影采样和PCF

```glsl
float calculateShadow(vec4 fragPosLightSpace) {
    // 透视除法
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // 边界检查
    if(projCoords.z > 1.0) return 1.0;
    
    // PCF软阴影
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    
    return 1.0 - shadow / 9.0;
}
```

---

## 性能优化建议

### 1. PBR优化

#### 1.1 预计算优化

**BRDF查找表（LUT）：**
- 预计算环境BRDF积分
- 避免实时复杂积分计算
- 使用2D纹理存储结果

**环境贴图预过滤：**
- 预计算不同粗糙度的环境反射
- 使用mipmap存储不同模糊级别
- 实时只需简单纹理采样

#### 1.2 着色器优化

**分支优化：**
```glsl
// 避免动态分支
float metallic = texture(metallicMap, texCoord).r;
vec3 F0 = mix(vec3(0.04), albedo, metallic);

// 而不是
if(metallic > 0.5) {
    F0 = albedo;
} else {
    F0 = vec3(0.04);
}
```

**数学优化：**
```glsl
// 使用快速平方根倒数
float invLength = inversesqrt(dot(v, v));
vec3 normalized = v * invLength;

// 避免重复计算
float NdotV = max(dot(N, V), 0.0);
float NdotL = max(dot(N, L), 0.0);
```

### 2. 阴影优化

#### 2.1 级联阴影贴图（CSM）

**距离分割：**
```cpp
// 对数分割
for(int i = 0; i < cascadeCount; ++i) {
    float p = (i + 1) / float(cascadeCount);
    float log = nearPlane * pow(farPlane / nearPlane, p);
    float uniform = nearPlane + (farPlane - nearPlane) * p;
    cascadeDistances[i] = mix(uniform, log, lambda);
}
```

**级联选择：**
```glsl
int cascadeIndex = 0;
for(int i = 0; i < CASCADE_COUNT - 1; ++i) {
    if(viewDepth < cascadeDistances[i]) {
        cascadeIndex = i;
        break;
    }
}
```

#### 2.2 阴影贴图优化

**分辨率管理：**
- 近距离使用高分辨率
- 远距离使用低分辨率
- 动态调整阴影质量

**剔除优化：**
```cpp
// 视锥体剔除
if(!frustumCull(lightFrustum, objectBounds)) {
    renderToShadowMap(object);
}

// 距离剔除
if(distance(lightPos, objectPos) < lightRadius) {
    renderToShadowMap(object);
}
```

### 3. 内存和带宽优化

#### 3.1 纹理压缩

**BC压缩格式：**
- BC1：RGB漫反射贴图
- BC3：RGBA法线贴图
- BC4：单通道粗糙度/金属度
- BC5：双通道法线贴图

#### 3.2 LOD系统

**距离LOD：**
```cpp
float distance = length(cameraPos - objectPos);
int lodLevel = min(int(distance / lodDistance), maxLodLevel);
renderWithLOD(object, lodLevel);
```

**角度LOD：**
```cpp
float angle = dot(normalize(cameraPos - objectPos), cameraForward);
if(angle < lodAngleThreshold) {
    skipRendering(object);
}
```

---

## 参数调整指南

### 1. PBR材质参数

#### 1.1 金属度（Metallic）

**取值范围：** [0.0, 1.0]

**典型值：**
- 非金属材质：0.0
- 金属材质：1.0
- 混合材质：0.0-1.0之间

**调整建议：**
- 避免0.2-0.8之间的中间值
- 使用遮罩纹理定义金属区域
- 考虑氧化和腐蚀效果

#### 1.2 粗糙度（Roughness）

**取值范围：** [0.0, 1.0]

**典型值：**
- 镜面：0.0-0.1
- 抛光金属：0.1-0.3
- 普通金属：0.3-0.7
- 粗糙表面：0.7-1.0

**调整建议：**
- 避免完全光滑（0.0）
- 使用细节纹理增加变化
- 考虑磨损和老化效果

#### 1.3 基础反射率（F0）

**非金属材质：**
- 水：0.02
- 塑料：0.03-0.05
- 玻璃：0.04-0.05
- 钻石：0.17

**金属材质：**
- 铁：(0.56, 0.57, 0.58)
- 铜：(0.95, 0.64, 0.54)
- 金：(1.00, 0.71, 0.29)
- 银：(0.95, 0.93, 0.88)

### 2. 光照参数

#### 2.1 光源强度

**方向光：**
- 太阳光：3.0-5.0 lux
- 室内光：0.5-2.0 lux
- 月光：0.1-0.3 lux

**点光源：**
- 灯泡：100-1000 lumens
- 蜡烛：10-15 lumens
- 手电筒：50-200 lumens

#### 2.2 环境光照

**环境光强度：**
- 室外：0.3-0.5
- 室内：0.1-0.3
- 地下：0.05-0.1

### 3. 阴影参数

#### 3.1 阴影贴图分辨率

**推荐设置：**
- 低质量：512×512
- 中等质量：1024×1024
- 高质量：2048×2048
- 极高质量：4096×4096

#### 3.2 PCF采样数

**性能vs质量权衡：**
- 1×1：硬阴影，最快
- 3×3：基础软阴影
- 5×5：中等质量软阴影
- 7×7：高质量软阴影，较慢

#### 3.3 阴影偏置

**自适应偏置：**
```glsl
float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
```

**固定偏置范围：**
- 0.001-0.01：适用于大多数场景
- 0.01-0.1：粗糙表面或低精度深度缓冲

---

## 实现效果对比

### 1. PBR vs 传统光照

#### 1.1 视觉差异

**传统Phong光照：**
- 简单的漫反射+镜面反射
- 不符合物理规律
- 材质表现不一致
- 缺乏真实感

**PBR光照：**
- 基于物理的材质响应
- 能量守恒保证
- 一致的材质表现
- 更真实的视觉效果

#### 1.2 性能对比

| 特性 | 传统光照 | PBR光照 | 性能影响 |
|------|----------|---------|----------|
| 计算复杂度 | 低 | 中等 | +30-50% |
| 纹理使用 | 2-3张 | 4-6张 | +50-100% |
| 内存占用 | 低 | 中等 | +40-60% |
| 视觉质量 | 基础 | 高 | 显著提升 |

### 2. 阴影技术对比

#### 2.1 硬阴影 vs 软阴影

**硬阴影（基础阴影映射）：**
- 锐利的阴影边缘
- 单点采样，性能最佳
- 不真实的视觉效果
- 明显的锯齿和失真

**软阴影（PCF）：**
- 平滑的阴影边缘
- 多点采样，性能开销
- 更真实的视觉效果
- 减少锯齿和失真

#### 2.2 性能数据

| 阴影技术 | GPU时间(ms) | 内存使用(MB) | 视觉质量 |
|----------|-------------|--------------|----------|
| 无阴影 | 0.0 | 0 | 差 |
| 硬阴影 | 0.5-1.0 | 4-16 | 中等 |
| 3×3 PCF | 1.0-2.0 | 4-16 | 良好 |
| 5×5 PCF | 2.0-4.0 | 4-16 | 优秀 |
| CSM | 3.0-6.0 | 16-64 | 极佳 |

### 3. 质量设置建议

#### 3.1 低端设备

```cpp
// PBR设置
config.pbrQuality = PBR_QUALITY_LOW;
config.iblResolution = 64;  // 64×64 环境贴图
config.brdfLutSize = 128;   // 128×128 BRDF LUT

// 阴影设置
config.shadowMapSize = 512;
config.pcfSamples = 1;      // 硬阴影
config.cascadeCount = 2;    // 2级级联
```

#### 3.2 中端设备

```cpp
// PBR设置
config.pbrQuality = PBR_QUALITY_MEDIUM;
config.iblResolution = 128; // 128×128 环境贴图
config.brdfLutSize = 256;   // 256×256 BRDF LUT

// 阴影设置
config.shadowMapSize = 1024;
config.pcfSamples = 9;      // 3×3 PCF
config.cascadeCount = 3;    // 3级级联
```

#### 3.3 高端设备

```cpp
// PBR设置
config.pbrQuality = PBR_QUALITY_HIGH;
config.iblResolution = 256; // 256×256 环境贴图
config.brdfLutSize = 512;   // 512×512 BRDF LUT

// 阴影设置
config.shadowMapSize = 2048;
config.pcfSamples = 25;     // 5×5 PCF
config.cascadeCount = 4;    // 4级级联
```

---

## 总结

本文档详细介绍了EnumaElish引擎中PBR和阴影计算的完整实现。通过理论基础、数学推导、代码实现和优化建议的全面覆盖，为开发者提供了完整的技术参考。

**关键要点：**
1. **PBR基于坚实的物理理论**，提供一致和真实的渲染效果
2. **微表面理论**是PBR的核心，通过NDF、几何函数和菲涅尔方程建模
3. **阴影映射结合PCF**提供高质量的软阴影效果
4. **性能优化**需要在质量和效率间找到平衡
5. **参数调整**需要基于真实世界的物理值

**未来发展方向：**
- 实时光线追踪阴影
- 更高级的BRDF模型
- 体积光照和散射
- 机器学习辅助的光照优化

通过持续的技术改进和优化，EnumaElish引擎将继续提供业界领先的渲染质量和性能。