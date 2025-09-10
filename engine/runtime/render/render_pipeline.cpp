#include "render_pipeline.h"
#include "interface/vulkan/vulkan_rhi.h"
#include "passes/main_camera_pass.h"
#include "passes/ui_pass.h"
#include "passes/directional_light_pass.h"
#include "render_pass_base.h"
#include "../core/base/macro.h"
#include <iostream>


namespace Elish
{
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

        // 初始化UI渲染通道
        m_ui_pass = std::make_shared<UIPass>();
        m_ui_pass->setCommonInfo(pass_common_info);
        m_ui_pass->initialize();
    }
    void RenderPipeline::forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResource> render_resource)
    {
        // LOG_INFO("[RenderPipeline] Starting forwardRender - frame rendering begins");
        
        VulkanRHI*      vulkan_rhi      = static_cast<VulkanRHI*>(rhi.get());

        // vulkan_resource->resetRingBufferOffset(vulkan_rhi->m_current_frame_index);
        vulkan_rhi->waitForFences();

        vulkan_rhi->resetCommandPool();
        
        // 准备渲染上下文，设置当前命令缓冲区
        
        // 准备Pass数据，包括模型数据和相机矩阵
        m_directional_light_shadow_pass->preparePassData(render_resource);
        m_main_camera_pass->preparePassData(render_resource);
        m_ui_pass->preparePassData(render_resource);
        
        // 准备渲染前的上下文，包括开始command buffer记录
         // 准备渲染前环境（检查窗口大小变化，如需重建交换链则返回）
        bool prepare_success =
            vulkan_rhi->prepareBeforePass(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));  // 交换链重建后回调
        if (!prepare_success)  // 若prepareBeforePass失败（包括交换链重建或其他错误），当前帧渲染终止
        {
            return;
        }

        // 1. 首先执行方向光阴影渲染通道（生成阴影贴图）
        // LOG_INFO("[RenderPipeline] About to call DirectionalLightShadowPass::draw()");
        static_cast<DirectionalLightShadowPass*>(m_directional_light_shadow_pass.get())
        ->draw();
        // LOG_INFO("[RenderPipeline] DirectionalLightShadowPass::draw() completed");

        // 2. 执行主相机渲染通道（包含UI子通道，使用阴影贴图）
        static_cast<MainCameraPass*>(m_main_camera_pass.get())
        ->drawForward(vulkan_rhi->m_current_swapchain_image_index);  // 传递当前交换链图像索引（多缓冲渲染）
        
        // 注意：UI渲染现在已经集成到MainCameraPass的子通道中，不再需要独立调用

        // 提交渲染命令并释放交换链图像
        vulkan_rhi->submitRendering([](){});

    }

    void RenderPipeline::passUpdateAfterRecreateSwapchain()
    {
        // 更新方向光阴影渲染通道
        DirectionalLightShadowPass& directional_light_shadow_pass = *(static_cast<DirectionalLightShadowPass*>(m_directional_light_shadow_pass.get()));
        directional_light_shadow_pass.updateAfterFramebufferRecreate();
        
        // 更新主相机渲染通道
        MainCameraPass& main_camera_pass = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        main_camera_pass.updateAfterFramebufferRecreate();
    }

}