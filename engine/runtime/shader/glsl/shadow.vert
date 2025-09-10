#version 450

// Push constants block for per-model transformation
layout( push_constant ) uniform ModelConstants
{
    mat4 model;  // Per-model transformation matrix
} modelConst;

layout(set = 0, binding = 0) uniform uniformbuffer
{
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

void main()
{
	gl_Position = ubo.proj * ubo.view * modelConst.model * vec4(inPosition, 1.0);
}