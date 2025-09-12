#version 450

// Push constants block for per-model transformation
layout( push_constant ) uniform ModelConstants
{
    mat4 model;  // Per-model transformation matrix
} modelConst;

layout(set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
} ubo;

// 添加光源投影视图矩阵用于阴影计算
layout(set = 0, binding = 9) uniform LightSpaceMatrix
{
    mat4 light_proj_view;
} lightMatrix;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragPosLightSpace; // 光空间坐标

void main()
{
    // 计算世界空间坐标
    vec4 worldPos = modelConst.model * vec4(inPosition, 1.0);
    
    // 计算最终的屏幕空间坐标
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    // 计算光空间坐标用于阴影映射
    fragPosLightSpace = lightMatrix.light_proj_view * worldPos;
    
    // 传递颜色和纹理坐标
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}