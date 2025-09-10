#version 310 es

// 启用早期深度测试优化
layout(early_fragment_tests) in;

void main()
{
    // 阴影渲染只需要深度信息，不需要颜色输出
    // 深度值会自动写入深度缓冲区
    // 片段着色器可以为空，或者执行一些深度相关的计算
}