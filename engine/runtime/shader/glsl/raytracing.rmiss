#version 460
#extension GL_EXT_ray_tracing : require

/**
 * @file raytracing.rmiss
 * @brief 光线追踪未命中着色器（Miss Shader）
 * @details 当光线未命中任何几何体时执行此着色器
 *          通常用于渲染天空盒或环境光照
 */

// 光线负载结构
layout(location = 0) rayPayloadInEXT vec3 hitValue;

// 环境贴图（可选）- 暂时注释掉以避免验证层错误
// layout(binding = 3, set = 0) uniform samplerCube environmentMap;

void main()
{
    // 获取光线方向
    vec3 rayDirection = gl_WorldRayDirectionEXT;
    
    // 方案1：使用环境贴图
    // hitValue = texture(environmentMap, rayDirection).rgb;
    
    // 方案2：简单的渐变天空
    float t = 0.5 * (normalize(rayDirection).y + 1.0);
    vec3 skyColor = mix(vec3(1.0, 1.0, 1.0), vec3(0.5, 0.7, 1.0), t);
    
    // 方案3：纯色背景
    // vec3 skyColor = vec3(0.2, 0.3, 0.5);
    
    hitValue = skyColor;
}