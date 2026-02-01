#version 460
#extension GL_EXT_ray_tracing : require

/**
 * @file shadow.rmiss
 * @brief 阴影光线未命中着色器（Shadow Miss Shader）
 * @details 当阴影光线未命中任何几何体时执行此着色器
 *          表示该点不在阴影中，应该接受光照
 */

// 阴影光线负载
layout(location = 1) rayPayloadInEXT bool isShadowed;

void main()
{
    // 阴影光线未命中任何几何体，表示不在阴影中
    isShadowed = false;
}