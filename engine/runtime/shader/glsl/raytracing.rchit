#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

/**
 * @file raytracing.rchit
 * @brief 光线追踪最近命中着色器（Closest Hit Shader）
 * @details 当光线命中几何体时执行此着色器，负责计算最终的着色结果
 *          包括材质属性、光照计算、阴影等
 */

// 光线负载结构
layout(location = 0) rayPayloadInNV vec3 hitValue;
layout(location = 1) rayPayloadNV bool isShadowed;

// 命中属性（重心坐标）
hitAttributeNV vec3 attribs;

// 加速结构
layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;

// 顶点缓冲区
layout(binding = 4, set = 0) buffer VertexBuffer {
    float vertices[];
};

// 索引缓冲区
layout(binding = 5, set = 0) buffer IndexBuffer {
    uint indices[];
};

// 材质缓冲区
layout(binding = 6, set = 0) buffer MaterialBuffer {
    vec4 materials[];  // xyz: albedo, w: metallic
};

// 相机和光照参数
layout(binding = 2, set = 0) uniform CameraBuffer {
    mat4 viewInverse;
    mat4 projInverse;
    vec4 lightPos;
    vec4 lightColor;
} cam;

// 顶点结构体
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec3 color;
    vec2 texCoord;
};

// 获取顶点数据
Vertex getVertex(uint index) {
    Vertex v;
    uint offset = index * 11; // 每个顶点11个float
    v.pos = vec3(vertices[offset], vertices[offset + 1], vertices[offset + 2]);
    v.normal = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
    v.color = vec3(vertices[offset + 6], vertices[offset + 7], vertices[offset + 8]);
    v.texCoord = vec2(vertices[offset + 9], vertices[offset + 10]);
    return v;
}

void main()
{
    // 获取三角形索引
    uint primitiveID = gl_PrimitiveID;
    uint i0 = indices[primitiveID * 3 + 0];
    uint i1 = indices[primitiveID * 3 + 1];
    uint i2 = indices[primitiveID * 3 + 2];
    
    // 获取顶点数据
    Vertex v0 = getVertex(i0);
    Vertex v1 = getVertex(i1);
    Vertex v2 = getVertex(i2);
    
    // 重心坐标插值
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // 插值顶点属性
    vec3 worldPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec3 worldNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    vec3 albedo = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;
    
    // 光照计算
    vec3 lightDir = normalize(cam.lightPos.xyz - worldPos);
    float NdotL = max(dot(worldNormal, lightDir), 0.0);
    
    // 阴影光线
    vec3 shadowRayOrigin = worldPos + worldNormal * 0.001; // 避免自相交
    vec3 shadowRayDir = lightDir;
    float shadowRayLength = length(cam.lightPos.xyz - worldPos);
    
    // 发射阴影光线
    isShadowed = true;
    traceNV(topLevelAS,
            gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsOpaqueNV | gl_RayFlagsSkipClosestHitShaderNV,
            0xff,
            1,  // 使用阴影命中组
            0,
            1,  // 使用阴影miss着色器
            shadowRayOrigin,
            0.001,
            shadowRayDir,
            shadowRayLength,
            1   // 阴影负载位置
    );
    
    // 计算最终颜色
    float shadowFactor = isShadowed ? 0.3 : 1.0;
    vec3 ambient = vec3(0.1) * albedo;
    vec3 diffuse = albedo * cam.lightColor.rgb * NdotL * shadowFactor;
    
    hitValue = ambient + diffuse;
}