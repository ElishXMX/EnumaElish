#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

/**
 * @file raytracing.rchit
 * @details 当光线命中几何体时执行此着色器，负责计算最终的着色结果
 *          包括材质属性、光照计算、阴影等
 */

// 光线负载
layout(location = 0) rayPayloadInEXT vec3 hitValue;

// 主函数
void main() {
    // 调试：纯绿色
    hitValue = vec3(0.0, 1.0, 0.0);
}