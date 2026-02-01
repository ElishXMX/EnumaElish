#pragma once

#include "../render_pass.h"
#include "../render_resource.h"
#include <glm/glm.hpp>
#include <memory>
#include <chrono>
#include <mutex>
#include <string>
#include <algorithm>

namespace Elish
{
    /**
     * @brief 光线追踪渲染通道类
     * @details 负责执行光线追踪渲染，包括加速结构更新、光线追踪管线绑定和光线追踪调度
     */
    class RayTracingPass : public RenderPass
    {
    public:
        /**
         * @brief 构造函数
         */
        RayTracingPass();

        /**
         * @brief 析构函数
         */
        ~RayTracingPass();

        /**
         * @brief 初始化光线追踪渲染通道
         * 设置光线追踪管线、描述符集布局和加速结构
         */
        void initialize() override;

        /**
         * @brief 准备光线追踪渲染通道的数据
         * @param render_resource 渲染资源管理器
         */
        void preparePassData(std::shared_ptr<RenderResource> render_resource) override;

        /**
         * @brief 执行光线追踪渲染
         * @param command_buffer 当前的命令缓冲区
         */
        void draw(RHICommandBuffer* command_buffer);

        /**
         * @brief 执行光线追踪渲染（带交换链图像索引）
         * @param swapchain_image_index 当前交换链图像索引
         */
        void drawRayTracing(uint32_t swapchain_image_index);

        /**
         * @brief 更新加速结构
         * 在场景发生变化时调用，重新构建或更新BLAS和TLAS
         */
        void updateAccelerationStructures();

        /**
         * @brief 设置光线追踪输出图像
         * @param output_image 输出图像
         */
        void setOutputImage(RHIImage* output_image);

        /**
         * @brief 获取光线追踪输出图像
         * @return 输出图像指针
         */
        RHIImage* getOutputImage() const { return m_output_image; }

        /**
         * @brief 设置光线追踪启用状态
         * @param enabled 是否启用光线追踪
         */
        void setRayTracingEnabled(bool enabled) { m_ray_tracing_enabled = enabled; }

        /**
         * @brief 获取光线追踪启用状态
         * @return 是否启用光线追踪
         */
        bool isRayTracingEnabled() const { return m_ray_tracing_enabled; }

        /**
         * @brief 设置光线追踪参数
         * @param max_depth 最大反射深度
         * @param samples_per_pixel 每像素采样数
         */
        void setRayTracingParams(uint32_t max_depth, uint32_t samples_per_pixel);

        /**
         * @brief 在交换链重建后更新资源
         */
        void updateAfterFramebufferRecreate();

        /**
         * @brief 动态调整光线追踪参数
         * @param frame_number 当前帧号
         * @param adaptive_samples 自适应采样数
         * @param noise_threshold 噪声阈值
         * @param quality_factor 质量因子
         */
        void adjustRayTracingParameters(uint32_t frame_number, uint32_t adaptive_samples, float noise_threshold, float quality_factor);
        
        /**
         * @brief 设置渲染缩放比例
         * @param scale 渲染缩放比例（0.5-1.0）
         */
        void setRenderScale(float scale) { m_render_scale = std::clamp(scale, 0.5f, 1.0f); }
        
        float getRenderScale() const { return m_render_scale; }
        uint32_t getMaxRayDepth() const { return m_max_ray_depth; }
        uint32_t getSamplesPerPixel() const { return m_samples_per_pixel; }
        
        /**
         * @brief 光线追踪诊断信息结构体
         */
        struct RayTracingDiagnostics {
            uint64_t total_rays_traced = 0;          // 总光线数
            uint64_t rays_per_second = 0;            // 每秒光线数
            float average_frame_time = 0.0f;         // 平均帧时间
            float last_frame_time = 0.0f;           // 上一帧时间
            uint32_t failed_ray_count = 0;          // 失败光线数
            uint32_t total_frame_count = 0;         // 总帧数
            bool performance_warning = false;        // 性能警告标志
            std::chrono::high_resolution_clock::time_point last_update_time;
        };
        
        /**
         * @brief 获取光线追踪诊断信息
         * @return 诊断信息结构体的副本
         */
        RayTracingDiagnostics getDiagnostics() const;
        
        /**
         * @brief 重置诊断统计信息
         */
        void resetDiagnostics();
        
        /**
         * @brief 启用或禁用性能监控
         * @param enable 是否启用监控
         */
        void setPerformanceMonitoring(bool enable);
        
        /**
         * @brief 获取当前性能状态
         * @return 性能状态字符串
         */
        std::string getPerformanceStatus() const;

    private:
        /**
         * @brief 设置光线追踪描述符集布局
         */
        void setupDescriptorSetLayout();

        /**
         * @brief 设置光线追踪管线
         */
        void setupRayTracingPipeline();

        /**
         * @brief 设置光线追踪描述符集
         */
        void setupDescriptorSet();

        /**
         * @brief 创建光线追踪输出图像
         */
        void createOutputImage();

        /**
         * @brief 更新描述符集
         */
        void updateDescriptorSet();

        /**
         * @brief 创建着色器绑定表
         */
        void createShaderBindingTable();

        /**
         * @brief 执行初始化清理操作
         * @param failed_step 失败的初始化步骤
         * @details 根据失败的步骤执行相应的资源清理，确保不会出现资源泄漏
         */
        void performInitializationCleanup(uint32_t failed_step);
        
        /**
         * @brief 根据硬件能力调整光线追踪参数
         * @details 自动检测硬件性能并调整光线追踪质量设置
         */
        void adjustRayTracingParameters();

    private:
        // 光线追踪状态
        bool m_ray_tracing_enabled = false;  // 默认关闭光线追踪功能，避免影响UI与主渲染
        bool m_is_initialized = false;

        // 光线追踪参数
        uint32_t m_max_ray_depth = 10;
        uint32_t m_samples_per_pixel = 1;

        // 光线追踪资源
        RHIImage* m_output_image = nullptr;
        RHIImageView* m_output_image_view = nullptr;
        RHIDeviceMemory* m_output_image_memory = nullptr;

        // 着色器绑定表
        RHIBuffer* m_raygen_shader_binding_table = nullptr;
        RHIBuffer* m_miss_shader_binding_table = nullptr;
        RHIBuffer* m_hit_shader_binding_table = nullptr;
        RHIDeviceMemory* m_raygen_sbt_memory = nullptr;
        RHIDeviceMemory* m_miss_sbt_memory = nullptr;
        RHIDeviceMemory* m_hit_sbt_memory = nullptr;

        // 光线追踪管线
        RHIPipeline* m_ray_tracing_pipeline = nullptr;
        RHIPipelineLayout* m_ray_tracing_pipeline_layout = nullptr;

        // 渲染资源引用
        std::shared_ptr<RenderResource> m_render_resource;

        /**
         * @brief 光线追踪uniform数据结构
         * @details 包含光线追踪着色器所需的所有uniform数据
         */
        struct RayTracingUniformData {
            glm::mat4 view_inverse;
            glm::mat4 proj_inverse;
            glm::vec4 light_position;
            glm::vec4 light_color;
            uint32_t max_depth;
            uint32_t samples_per_pixel;
            float time;
            float _padding;
        };
        
        /**
         * @brief 光线追踪推送常量结构
         * @details 用于动态调整光线追踪参数的推送常量
         */
        struct RayTracingPushConstants {
            uint32_t frame_number;          // 当前帧号，用于随机数生成
            uint32_t adaptive_samples;      // 自适应采样数
            float noise_threshold;          // 噪声阈值
            float quality_factor;           // 质量因子 (0.0-1.0)
        };

        // Uniform缓冲区
        std::vector<RHIBuffer*> m_uniform_buffers;
        std::vector<RHIDeviceMemory*> m_uniform_buffers_memory;
        std::vector<void*> m_uniform_buffers_mapped;
        
        // 调试和诊断相关成员变量
        
        mutable RayTracingDiagnostics m_diagnostics;
        mutable std::mutex m_diagnostics_mutex;      // 诊断数据互斥锁
        bool m_performance_monitoring_enabled = true; // 性能监控开关
        
        // 输出图像分辨率与缩放
        uint32_t m_output_width = 0;
        uint32_t m_output_height = 0;
        float m_render_scale = 1.0f;

        // 跟踪上一帧是否执行了光线追踪
        bool m_traced_last_frame = false;

    public:
        /**
         * @brief 查询上一帧是否执行了光线追踪
         */
        bool didLastFrameTrace() const { return m_traced_last_frame; }
        
        // 私有诊断方法
        /**
         * @brief 更新性能统计信息
         * @param frame_time 当前帧时间
         */
        void updatePerformanceStats(float frame_time) const;
        
        /**
         * @brief 检查性能警告条件
         */
        void checkPerformanceWarnings() const;
    };

} // namespace Elish