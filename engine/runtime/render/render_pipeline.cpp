#include "render_pipeline.h"
#include "interface/vulkan/vulkan_rhi.h"
#include "interface/vulkan/vulkan_rhi_resource.h"
#include "passes/main_camera_pass.h"
#include "passes/ui_pass.h"
#include "passes/directional_light_pass.h"
#include "passes/raytracing_pass.h"
#include "render_pass_base.h"
#include <iostream>

namespace Elish
{
    // RT任务监控辅助方法实现
    void RenderPipeline::RTTaskMonitor::begin()
    {
        running = true;
        completed = false;
        auto now = std::chrono::high_resolution_clock::now();
        start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    void RenderPipeline::RTTaskMonitor::finish(bool success)
    {
        auto now = std::chrono::high_resolution_clock::now();
        end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        last_duration_ms = end_ms - start_ms;
        running = false;
        completed = true;
        if (success) {
            success_count++;
        } else {
            failure_count++;
            last_error_timestamp_ms = end_ms;
            if (window_start_ms == 0 || (end_ms - window_start_ms) > 60000) {
                window_start_ms = end_ms;
                error_count_window = 1;
            } else {
                error_count_window++;
            }
        }
    }

    bool RenderPipeline::RTTaskMonitor::isTimeout(uint64_t now_ms) const
    {
        return running && (now_ms - start_ms) > timeout_threshold_ms;
    }

    /**
     * @brief 初始化渲染管线
     */
    void RenderPipeline::initialize()
    {
        RenderPassCommonInfo pass_common_info;
        pass_common_info.rhi = m_rhi;

        // 初始化方向光阴影渲染通道
        auto shadow_pass = std::make_shared<DirectionalLightShadowPass>();
        shadow_pass->setCommonInfo(pass_common_info);
        shadow_pass->initialize();
        shadow_pass->postInitialize();
        m_directional_light_shadow_pass = shadow_pass;

        // 初始化主相机渲染通道
        m_main_camera_pass = std::make_shared<MainCameraPass>();
        m_main_camera_pass->setCommonInfo(pass_common_info);
        m_main_camera_pass->initialize();
        
        auto main_camera_pass = std::static_pointer_cast<MainCameraPass>(m_main_camera_pass);
        main_camera_pass->setDirectionalLightShadowPass(std::static_pointer_cast<DirectionalLightShadowPass>(m_directional_light_shadow_pass));

        // 初始化UI渲染通道
        m_ui_pass = std::make_shared<UIPass>();
        m_ui_pass->setCommonInfo(pass_common_info);
        m_ui_pass->initialize();
        
        LOG_DEBUG("[RenderPipeline] Creating ray tracing pass");
        m_raytracing_pass = std::make_shared<RayTracingPass>();
        LOG_DEBUG("[RenderPipeline] Setting common info for ray tracing pass");
        m_raytracing_pass->setCommonInfo(pass_common_info);
        
        // 光线追踪暂时禁用
        m_raytracing_pass->setRayTracingEnabled(false);

        LOG_DEBUG("[RenderPipeline] Initializing ray tracing pass");
        m_raytracing_pass->initialize();
        LOG_INFO("[RenderPipeline] Ray tracing pass initialization completed, enabled={}", m_raytracing_pass->isRayTracingEnabled());
    }

    /**
     * @brief 前向渲染
     */
    void RenderPipeline::forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResource> render_resource)
    {
        VulkanRHI* vulkan_rhi = static_cast<VulkanRHI*>(rhi.get());
        
        vulkan_rhi->waitForFences();
        vulkan_rhi->resetCommandPool();
        
        // 🔧 确保场景中至少有一个方向光源（在所有Pass准备之前）
        if (render_resource && render_resource->getDirectionalLightCount() == 0) {
            // 创建默认方向光源
            DirectionalLightData default_light;
            default_light.direction = glm::vec3(-0.2f, -1.0f, -0.3f);
            default_light.intensity = 3.0f;
            default_light.color = glm::vec3(1.0f, 1.0f, 1.0f);
            default_light.enabled = true;
            default_light.distance = 20.0f;
            
            render_resource->addDirectionalLight(default_light);
            LOG_INFO("[RenderPipeline] Added default directional light to RenderResource");
        }
        
        // 准备Pass数据
        m_directional_light_shadow_pass->preparePassData(render_resource);
        m_main_camera_pass->preparePassData(render_resource);
        m_ui_pass->preparePassData(render_resource);
        
        // 准备渲染前的上下文
        bool prepare_success = vulkan_rhi->prepareBeforePass(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (!prepare_success)
        {
            return;
        }
        
        // 1. 首先执行方向光阴影渲染通道
        static_cast<DirectionalLightShadowPass*>(m_directional_light_shadow_pass.get())->draw();
        
        // 2. 执行光线追踪渲染
        if (m_raytracing_pass && m_raytracing_pass->isRayTracingEnabled())
        {
            m_rt_monitor.begin();
            bool rt_success = true;
            try {
                RHICommandBuffer* command_buffer = vulkan_rhi->getCurrentCommandBuffer();
                RHIMemoryBarrier memory_barrier{};
                memory_barrier.sType = RHI_STRUCTURE_TYPE_MEMORY_BARRIER;
                memory_barrier.srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                memory_barrier.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                vulkan_rhi->cmdPipelineBarrier(
                    command_buffer,
                    RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    0,
                    1, &memory_barrier,
                    0, nullptr,
                    0, nullptr
                );

                m_raytracing_pass->preparePassData(render_resource);
                m_raytracing_pass->drawRayTracing(vulkan_rhi->m_current_swapchain_image_index);
            } catch (const std::exception& e) {
                rt_success = false;
                LOG_ERROR("[RTTask] Exception during ray tracing: {}", e.what());
            } catch (...) {
                rt_success = false;
                LOG_ERROR("[RTTask] Unknown exception during ray tracing");
            }
            m_rt_monitor.finish(rt_success);

            // 超时与频繁异常告警
            auto now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            if (m_rt_monitor.isTimeout(now_ms)) {
                LOG_ERROR("[RTTask] Timeout detected! duration={}ms, threshold={}ms", m_rt_monitor.last_duration_ms, m_rt_monitor.timeout_threshold_ms);
            }
            if (m_rt_monitor.error_count_window >= 3) {
                LOG_WARN("[RTTask] Frequent RT errors: {} times within 1 minute", m_rt_monitor.error_count_window);
            }
        }

        // 3. 执行主相机渲染
        MainCameraPass& main_camera_pass = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        main_camera_pass.drawForward(vulkan_rhi->m_current_swapchain_image_index);

        // 提交渲染命令
        vulkan_rhi->submitRendering([](){});
    }
    
    /**
     * @brief 将光线追踪输出图像复制到交换链图像
     */
    void RenderPipeline::copyRayTracingOutputToSwapchain(std::shared_ptr<RHI> rhi, uint32_t swapchain_image_index)
    {
        // 若当前帧未执行光线追踪或禁用，则不进行覆盖复制
        if (!m_raytracing_pass || !m_raytracing_pass->isRayTracingEnabled() || !m_raytracing_pass->didLastFrameTrace())
        {
            LOG_DEBUG("[RenderPipeline] Skip RT copy: enabled={}, traced_last_frame={}", 
                     m_raytracing_pass ? m_raytracing_pass->isRayTracingEnabled() : false, 
                     m_raytracing_pass ? m_raytracing_pass->didLastFrameTrace() : false);
            return;
        }

        // 获取光线追踪输出图像
        RHIImage* rt_image = m_raytracing_pass->getOutputImage();
        if (!rt_image)
        {
            LOG_WARN("[RenderPipeline] Ray tracing output image is null; skip copy");
            return;
        }

        RHISwapChainDesc swap_desc = rhi->getSwapchainInfo();

        RHICommandBuffer* cmd = rhi->getCurrentCommandBuffer();
        if (!cmd)
        {
            LOG_ERROR("[RenderPipeline] Current command buffer is null; skip copy");
            return;
        }

        // 准备图像布局转换
        RHIImageMemoryBarrier src_barrier{};
        src_barrier.sType = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        src_barrier.oldLayout = RHI_IMAGE_LAYOUT_GENERAL;
        src_barrier.newLayout = RHI_IMAGE_LAYOUT_GENERAL;
        src_barrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        src_barrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        src_barrier.image = rt_image;
        src_barrier.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        src_barrier.subresourceRange.baseMipLevel = 0;
        src_barrier.subresourceRange.levelCount = 1;
        src_barrier.subresourceRange.baseArrayLayer = 0;
        src_barrier.subresourceRange.layerCount = 1;
        src_barrier.srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
        src_barrier.dstAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;

        rhi->cmdPipelineBarrier(
            cmd,
            RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            RHI_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &src_barrier);

        // 包装当前交换链图像
        VulkanRHI* vk_rhi = static_cast<VulkanRHI*>(rhi.get());
        VulkanImage dst_swap_image;
        dst_swap_image.setResource(vk_rhi->m_swapchain_images[swapchain_image_index]);

        // 将交换链图像转换为传输目标布局
        RHIImageMemoryBarrier dst_barrier_to_transfer{};
        dst_barrier_to_transfer.sType = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dst_barrier_to_transfer.oldLayout = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        dst_barrier_to_transfer.newLayout = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dst_barrier_to_transfer.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        dst_barrier_to_transfer.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        dst_barrier_to_transfer.image = &dst_swap_image;
        dst_barrier_to_transfer.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        dst_barrier_to_transfer.subresourceRange.baseMipLevel = 0;
        dst_barrier_to_transfer.subresourceRange.levelCount = 1;
        dst_barrier_to_transfer.subresourceRange.baseArrayLayer = 0;
        dst_barrier_to_transfer.subresourceRange.layerCount = 1;
        dst_barrier_to_transfer.srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst_barrier_to_transfer.dstAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;

        rhi->cmdPipelineBarrier(
            cmd,
            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            RHI_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &dst_barrier_to_transfer);

        // 复制到交换链图像
        rhi->cmdCopyImageToImage(
            cmd,
            rt_image,
            RHI_IMAGE_ASPECT_COLOR_BIT,
            &dst_swap_image,
            RHI_IMAGE_ASPECT_COLOR_BIT,
            swap_desc.extent.width,
            swap_desc.extent.height);

        // 将交换链图像恢复为颜色附件布局
        RHIImageMemoryBarrier dst_barrier_to_color{};
        dst_barrier_to_color.sType = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dst_barrier_to_color.oldLayout = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dst_barrier_to_color.newLayout = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        dst_barrier_to_color.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        dst_barrier_to_color.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        dst_barrier_to_color.image = &dst_swap_image;
        dst_barrier_to_color.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        dst_barrier_to_color.subresourceRange.baseMipLevel = 0;
        dst_barrier_to_color.subresourceRange.levelCount = 1;
        dst_barrier_to_color.subresourceRange.baseArrayLayer = 0;
        dst_barrier_to_color.subresourceRange.layerCount = 1;
        dst_barrier_to_color.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
        dst_barrier_to_color.dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        rhi->cmdPipelineBarrier(
            cmd,
            RHI_PIPELINE_STAGE_TRANSFER_BIT,
            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &dst_barrier_to_color);

        LOG_DEBUG("[RenderPipeline] Copied ray tracing output image to swapchain image {}", swapchain_image_index);
    }

    /**
     * @brief 交换链重建后更新Pass
     */
    void RenderPipeline::passUpdateAfterRecreateSwapchain()
    {
        // 更新方向光阴影渲染通道
        DirectionalLightShadowPass& directional_light_shadow_pass = *(static_cast<DirectionalLightShadowPass*>(m_directional_light_shadow_pass.get()));
        directional_light_shadow_pass.updateAfterFramebufferRecreate();
        
        // 更新主相机渲染通道
        MainCameraPass& main_camera_pass = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        main_camera_pass.updateAfterFramebufferRecreate();
        
        // 更新光线追踪渲染通道
        if (m_raytracing_pass)
        {
            m_raytracing_pass->updateAfterFramebufferRecreate();
        }
    }
    
    /**
     * @brief 启用或禁用光线追踪
     */
    void RenderPipeline::setRayTracingEnabled(bool enabled)
    {
        if (m_raytracing_pass)
        {
            m_raytracing_pass->setRayTracingEnabled(enabled);
        }
    }
    
    /**
     * @brief 获取光线追踪启用状态
     */
    bool RenderPipeline::isRayTracingEnabled() const
    {
        if (m_raytracing_pass)
        {
            return m_raytracing_pass->isRayTracingEnabled();
        }
        return false;
    }

}