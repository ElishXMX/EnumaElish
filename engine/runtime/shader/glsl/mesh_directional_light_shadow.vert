#version 310 es

// 简化的D-shadow顶点着色器
// 移除复杂的顶点混合逻辑，只保留基本的变换功能

// 描述符集 0: 全局帧数据
layout(set = 0, binding = 0) readonly buffer GlobalFrameBuffer
{
    mat4 light_proj_view;  // 光源的投影视图矩阵
};

// 描述符集 0: 实例变换矩阵数组
layout(set = 0, binding = 1) readonly buffer InstanceDataBuffer
{
    mat4 model_matrices[];  // 简化的模型矩阵数组
};

// 顶点输入
layout(location = 0) in highp vec3 in_position;

void main()
{
    // 获取当前实例的变换矩阵
    highp mat4 model_matrix = model_matrices[gl_InstanceIndex];
    
    // 将顶点位置变换到世界空间
    highp vec3 world_position = (model_matrix * vec4(in_position, 1.0)).xyz;
    
    // 应用光源的投影视图矩阵，得到最终的裁剪空间位置
    gl_Position = light_proj_view * vec4(world_position, 1.0);
}
