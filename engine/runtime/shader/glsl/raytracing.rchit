#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

/**
 * @file raytracing.rchit
 * @brief 光线追踪最近命中着色器（Closest Hit Shader）
 * @details 当光线命中几何体时执行此着色器，负责计算最终的着色结果
 *          包括材质属性、光照计算、阴影等
 */

// 光线负载结构
layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool isShadowed;

// 命中属性（重心坐标）
hitAttributeEXT vec3 attribs;

// 加速结构
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

// 顶点缓冲区
layout(binding = 4, set = 0) buffer VertexBuffer {
    float vertices[];
};

// 索引缓冲区
layout(binding = 5, set = 0) buffer IndexBuffer {
    uint indices[];
};

// 材质缓冲区 - 暂时注释掉以避免验证层错误
// layout(binding = 6, set = 0) buffer MaterialBuffer {
//     vec4 materials[];  // xyz: albedo, w: metallic
// };

// 临时材质数据
const vec3 defaultAlbedo = vec3(0.8, 0.8, 0.8);
const float defaultMetallic = 0.0;

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

// 获取顶点数据（带边界检查）
Vertex getVertex(uint index) {
    Vertex v;
    uint offset = index * 11; // 每个顶点11个float
    
    // 边界检查：确保不会越界访问
    uint maxOffset = offset + 10; // 最大需要访问的索引
    if (maxOffset >= vertices.length()) {
        // 返回默认顶点数据以避免崩溃
        v.pos = vec3(0.0);
        v.color = vec3(1.0, 0.0, 1.0); // 洋红色表示错误
        v.texCoord = vec2(0.0);
        v.normal = vec3(0.0, 1.0, 0.0);
        return v;
    }
    
    // 严格按照C++中Vertex结构体的内存布局：pos(3) + color(3) + texCoord(2) + normal(3)
    v.pos = vec3(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
    v.color = vec3(vertices[offset + 3], vertices[offset + 4], vertices[offset + 5]);
    v.texCoord = vec2(vertices[offset + 6], vertices[offset + 7]);
    v.normal = vec3(vertices[offset + 8], vertices[offset + 9], vertices[offset + 10]);
    return v;
}

void main()
{
    // 暂时屏蔽复杂逻辑，直接输出绿色用于调试 VK_ERROR_DEVICE_LOST
    /*
    // 获取三角形索引（带边界检查）
    uint primitiveID = gl_PrimitiveID;
    uint indexOffset = gl_InstanceCustomIndexEXT;
    
    // 检查索引是否越界
    uint baseIndex = indexOffset + primitiveID * 3;
    if (baseIndex + 2 >= indices.length()) {
        // 输出错误颜色并返回
        hitValue = vec3(1.0, 0.0, 0.0); // 红色表示索引错误
        return;
    }
    
    uint i0 = indices[baseIndex + 0];
    uint i1 = indices[baseIndex + 1];
    uint i2 = indices[baseIndex + 2];
    
    // 获取顶点数据
    Vertex v0 = getVertex(i0);
    Vertex v1 = getVertex(i1);
    Vertex v2 = getVertex(i2);
    
    // 重心坐标插值
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    
    // 插值顶点属性
    vec3 worldPos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec3 worldNormal = normalize(v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z);
    // 如果顶点颜色接近白色（默认值），则使用默认材质颜色
    vec3 vertexColor = v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z;
    vec3 albedo = vertexColor;
    
    // 简单的 Lambertian 光照模型
    // 光源方向
    vec3 lightDir = normalize(cam.lightPos.xyz - worldPos);
    
    // 漫反射强度
    float diff = max(dot(worldNormal, lightDir), 0.0);
    
    // 环境光
    vec3 ambient = 0.2 * albedo;
    
    // 漫反射
    vec3 diffuse = diff * albedo * cam.lightColor.rgb;
    
    // 最终颜色
    hitValue = ambient + diffuse;
    */
    hitValue = vec3(0.0, 1.0, 0.0); // 调试：纯绿色
}