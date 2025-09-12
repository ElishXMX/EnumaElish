#pragma once

#include "../render_pass.h"
#include "../render_resource.h"
#include <glm/glm.hpp>
#include <memory>

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
         * @brief 计算着色器绑定表
         */
        void createShaderBindingTable();

    private:
        // 光线追踪状态
        bool m_ray_tracing_enabled = false;
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

        // 光线追踪uniform数据结构
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

        // Uniform缓冲区
        std::vector<RHIBuffer*> m_uniform_buffers;
        std::vector<RHIDeviceMemory*> m_uniform_buffers_memory;
        std::vector<void*> m_uniform_buffers_mapped;
    };

} // namespace Elish