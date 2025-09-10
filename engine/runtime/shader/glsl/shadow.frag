#version 450

// 阴影渲染通道只需要深度信息，不需要颜色输出
void main()
{
	// 空的片段着色器，只写入深度值
	// gl_FragDepth 会自动由硬件处理
}