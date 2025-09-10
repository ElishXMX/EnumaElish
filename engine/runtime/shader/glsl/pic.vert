#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() 
{
    // Direct draw approach - define vertices in shader
    vec3 positions[6] = vec3[](
        vec3(-1.0, -1.0, 0.0),  // Bottom left
        vec3( 1.0, -1.0, 0.0),  // Bottom right
        vec3(-1.0,  1.0, 0.0),  // Top left
        vec3( 1.0, -1.0, 0.0),  // Bottom right
        vec3( 1.0,  1.0, 0.0),  // Top right
        vec3(-1.0,  1.0, 0.0)   // Top left
    );
    
    vec3 colors[6] = vec3[](
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0)
    );
    
    vec2 texCoords[6] = vec2[](
        vec2(0.0, 0.0),  // Bottom left
        vec2(1.0, 0.0),  // Bottom right
        vec2(0.0, 1.0),  // Top left
        vec2(1.0, 0.0),  // Bottom right
        vec2(1.0, 1.0),  // Top right
        vec2(0.0, 1.0)   // Top left
    );
    
    gl_Position = vec4(positions[gl_VertexIndex], 1.0);
    fragColor = colors[gl_VertexIndex];
    fragTexCoord = texCoords[gl_VertexIndex];
}
