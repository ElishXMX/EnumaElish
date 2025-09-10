#version 450

layout(binding =1) uniform sampler2D texSampler;
layout(set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = vec4(fragColor * texture(texSampler, fragTexCoord).rgb, 1.0);
}
