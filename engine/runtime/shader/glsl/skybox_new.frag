#version 450

/**
 * 天空盒片段着色器 - 重新设计版本
 * 功能：从立方体贴图中采样颜色
 * 优化：简化输入输出，提高性能
 */

// 立方体贴图纹理采样器
layout(set = 0, binding = 0) uniform samplerCube skyboxTexture;

// 从顶点着色器接收的纹理坐标
layout(location = 0) in vec3 texCoords;

// 输出颜色
layout(location = 0) out vec4 fragColor;

void main()
{
    // 对纹理坐标进行标准化，确保正确的立方体贴图采样
    vec3 normalizedCoords = normalize(texCoords);
    
    // 从立方体贴图中采样颜色
    // 使用mipmap level 0获得最清晰的效果
    vec3 skyboxColor = textureLod(skyboxTexture, normalizedCoords, 0.0).rgb;
    
    // 可选：应用简单的色调映射以增强视觉效果
    // skyboxColor = skyboxColor / (skyboxColor + vec3(1.0)); // Reinhard色调映射
    
    // 输出最终颜色，alpha设为1.0（完全不透明）
    fragColor = vec4(skyboxColor, 1.0);
}