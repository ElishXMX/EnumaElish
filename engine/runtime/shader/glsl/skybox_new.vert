#version 450

/**
 * 天空盒顶点着色器 - 重新设计版本
 * 功能：生成天空盒立方体的顶点，并传递纹理坐标给片段着色器
 * 优化：移除不必要的UBO依赖，简化数据传递
 */

// 推送常量，用于传递必要的变换矩阵
layout(push_constant) uniform PushConstants {
    mat4 viewProjectionMatrix;  // 视图投影矩阵
    vec3 cameraPosition;        // 相机位置
} pushConstants;

// 输出到片段着色器的纹理坐标
layout(location = 0) out vec3 texCoords;

void main()
{
    // 定义立方体的8个顶点
    const vec3 cubeVertices[8] = vec3[8](
        vec3( 1.0,  1.0,  1.0),  // 0: 右上前
        vec3( 1.0,  1.0, -1.0),  // 1: 右上后
        vec3( 1.0, -1.0, -1.0),  // 2: 右下后
        vec3( 1.0, -1.0,  1.0),  // 3: 右下前
        vec3(-1.0,  1.0,  1.0),  // 4: 左上前
        vec3(-1.0,  1.0, -1.0),  // 5: 左上后
        vec3(-1.0, -1.0, -1.0),  // 6: 左下后
        vec3(-1.0, -1.0,  1.0)   // 7: 左下前
    );
    
    // 定义立方体的36个三角形索引（12个面 * 3个顶点）
    const int cubeIndices[36] = int[36](
        // 正X面 (右面)
        0, 1, 2,  2, 3, 0,
        // 正Y面 (上面)
        4, 5, 1,  1, 0, 4,
        // 负X面 (左面)
        7, 6, 5,  5, 4, 7,
        // 负Y面 (下面)
        3, 2, 6,  6, 7, 3,
        // 正Z面 (前面)
        4, 0, 3,  3, 7, 4,
        // 负Z面 (后面)
        1, 5, 6,  6, 2, 1
    );
    
    // 获取当前顶点的位置
    vec3 localPosition = cubeVertices[cubeIndices[gl_VertexIndex]];
    
    // 计算世界空间位置（以相机为中心的大立方体）
    vec3 worldPosition = pushConstants.cameraPosition + localPosition * 1000.0;
    
    // 变换到裁剪空间
    vec4 clipPosition = pushConstants.viewProjectionMatrix * vec4(worldPosition, 1.0);
    
    // 确保天空盒始终在最远处（深度值设为最大）
    clipPosition.z = clipPosition.w * 0.99999;
    
    gl_Position = clipPosition;
    
    // 传递纹理坐标（使用本地位置作为立方体贴图坐标）
    texCoords = localPosition;
}