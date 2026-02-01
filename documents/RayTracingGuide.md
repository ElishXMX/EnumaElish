# 光线追踪系统使用指南

## 概述

EnumaElish引擎的光线追踪系统基于Vulkan Ray Tracing扩展实现，提供了高质量的实时光线追踪渲染能力。本文档将详细介绍如何使用和配置光线追踪功能。

## 系统要求

### 硬件要求
- 支持Vulkan Ray Tracing扩展的GPU（RTX 20系列及以上，或AMD RDNA2及以上）
- 至少8GB显存（推荐16GB以上）
- 支持Vulkan 1.2或更高版本

### 软件要求
- Vulkan SDK 1.3.0或更高版本
- 支持VK_KHR_acceleration_structure扩展
- 支持VK_KHR_ray_tracing_pipeline扩展
- 支持VK_KHR_buffer_device_address扩展

## 快速开始

### 1. 初始化光线追踪系统

```cpp
#include "render/passes/raytracing_pass.h"

// 创建光线追踪渲染通道
auto raytracing_pass = std::make_shared<Elish::RayTracingPass>();

// 初始化光线追踪系统
raytracing_pass->initialize();

// 检查初始化状态
if (!raytracing_pass->isInitialized()) {
    LOG_ERROR("光线追踪系统初始化失败");
    return false;
}
```

### 2. 配置光线追踪参数

```cpp
// 设置光线追踪质量参数
raytracing_pass->setRayTracingParams(
    10,  // 最大光线深度
    4    // 每像素采样数
);

// 启用性能监控
raytracing_pass->setPerformanceMonitoring(true);

// 启用光线追踪渲染
raytracing_pass->setRayTracingEnabled(true);
```

### 3. 渲染循环集成

```cpp
// 在渲染循环中
void renderFrame() {
    // 更新加速结构（如果场景发生变化）
    raytracing_pass->updateAccelerationStructures();
    
    // 执行光线追踪渲染
    raytracing_pass->drawRayTracing(current_swapchain_image_index);
    
    // 获取性能统计信息
    auto diagnostics = raytracing_pass->getDiagnostics();
    LOG_INFO("光线追踪渲染时间: {}ms", diagnostics.last_render_time);
}
```

## 详细配置

### 质量设置

光线追踪系统提供了多种质量级别的配置：

#### 高质量设置（适用于高端GPU）
```cpp
raytracing_pass->setRayTracingParams(10, 4);  // 深度10，采样4
```

#### 中等质量设置（适用于中端GPU）
```cpp
raytracing_pass->setRayTracingParams(5, 2);   // 深度5，采样2
```

#### 低质量设置（适用于入门级GPU）
```cpp
raytracing_pass->setRayTracingParams(3, 1);   // 深度3，采样1
```

### 渲染缩放（保持帧率）

为在1080p分辨率下保持≥30FPS，系统支持按比例缩放输出图像分辨率：

```cpp
// 设置渲染缩放比例（0.5-1.0），默认1.0
raytracing_pass->setRenderScale(0.75f);
```
运行时还会根据帧时间自动调整缩放与采样数，以尽量维持目标帧率。

### 自适应质量调整

系统支持基于性能的自适应质量调整：

```cpp
// 动态调整参数
raytracing_pass->adjustRayTracingParameters(
    frame_number,      // 当前帧号
    adaptive_samples,  // 自适应采样数
    0.1f,             // 噪声阈值
    1.0f              // 质量因子
);
```

### 性能监控

```cpp
// 启用性能监控
raytracing_pass->setPerformanceMonitoring(true);

// 获取诊断信息
auto diagnostics = raytracing_pass->getDiagnostics();
std::cout << "渲染时间: " << diagnostics.last_render_time << "ms" << std::endl;
std::cout << "平均帧率: " << diagnostics.average_fps << "fps" << std::endl;
std::cout << "内存使用: " << diagnostics.memory_usage_mb << "MB" << std::endl;

// 获取性能状态报告
std::string status = raytracing_pass->getPerformanceStatus();
LOG_INFO("性能状态: {}", status);
```

### 性能基准与压力测试

启用环境变量进行自动性能记录，每120帧写入CSV到`bin/rt_benchmark.csv`：

```bash
# Windows PowerShell
$env:ELISH_BENCH="1"
```

CSV字段：`avg_ms,fps,scale,spp,depth`

## 故障排除

### 常见问题

#### 1. 初始化失败
**问题**: 光线追踪系统初始化失败
**解决方案**:
- 检查GPU是否支持光线追踪扩展
- 确认Vulkan驱动程序版本
- 检查系统内存是否充足

#### 2. 性能问题
**问题**: 光线追踪渲染性能低下
**解决方案**:
- 降低光线深度和采样数
- 启用自适应质量调整
- 检查GPU温度和功耗限制
- 使用渲染缩放（0.5-1.0）以保持稳定帧率

#### 3. 渲染错误
**问题**: 光线追踪渲染结果异常
**解决方案**:
- 检查加速结构是否正确构建
- 验证着色器绑定表配置
- 确认场景几何数据完整性

### 调试技巧

```cpp
// 启用详细日志
#define RAY_TRACING_DEBUG 1

// 检查系统状态
if (!raytracing_pass->isRayTracingSupported()) {
    LOG_ERROR("当前系统不支持光线追踪");
}

// 重置诊断计数器
raytracing_pass->resetDiagnostics();

// 检查性能警告
raytracing_pass->checkPerformanceWarnings();
```

## 最佳实践

### 1. 性能优化
- 根据目标帧率动态调整质量参数
- 使用LOD系统减少复杂场景的计算负担
- 合理配置加速结构更新频率

### 2. 内存管理
- 监控GPU内存使用情况
- 及时释放不需要的资源
- 使用内存池减少分配开销

### 3. 兼容性
- 提供光栅化渲染作为后备方案
- 检测硬件能力并自动选择合适的渲染路径
- 为不同硬件配置提供预设选项

## API参考

### RayTracingPass类主要方法

```cpp
class RayTracingPass {
public:
    // 初始化和清理
    void initialize();
    bool isInitialized() const;
    
    // 渲染控制
    void setRayTracingEnabled(bool enabled);
    bool isRayTracingEnabled() const;
    void drawRayTracing(uint32_t swapchain_image_index);
    
    // 参数配置
    void setRayTracingParams(uint32_t max_depth, uint32_t samples_per_pixel);
    void adjustRayTracingParameters();
    void adjustRayTracingParameters(uint32_t frame_number, uint32_t adaptive_samples, 
                                   float noise_threshold, float quality_factor);
    
    // 性能监控
    void setPerformanceMonitoring(bool enable);
    RayTracingDiagnostics getDiagnostics() const;
    void resetDiagnostics();
    std::string getPerformanceStatus() const;
    
    // 资源管理
    void updateAccelerationStructures();
    void setOutputImage(RHIImage* output_image);
    void updateAfterFramebufferRecreate();
};
```

### RayTracingDiagnostics结构体

```cpp
struct RayTracingDiagnostics {
    float last_render_time;     // 上次渲染时间（毫秒）
    float average_fps;          // 平均帧率
    uint32_t memory_usage_mb;   // 内存使用量（MB）
    uint32_t total_rays_cast;   // 总发射光线数
    bool performance_warning;   // 性能警告标志
};
```

## 示例项目

完整的示例项目可以在 `examples/raytracing_demo` 目录中找到，包含：
- 基础光线追踪场景设置
- 材质和光照配置
- 性能优化示例
- 调试和故障排除代码

## 更新日志

### v1.0.0 (当前版本)
- 实现基础光线追踪渲染管线
- 添加自适应质量调整功能
- 集成性能监控和诊断系统
- 提供完整的错误处理和回退机制
- 支持渲染缩放与性能基准记录

## 技术支持

如果在使用过程中遇到问题，请：
1. 查阅本文档的故障排除部分
2. 检查系统日志获取详细错误信息
3. 参考示例项目中的实现
4. 提交Issue到项目仓库

---

*本文档将随着引擎功能的更新而持续维护和改进。*