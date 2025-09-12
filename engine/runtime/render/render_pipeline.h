//总渲染管线，组织数据
#pragma once

#include "render_pipeline_base.h"
#include "passes/directional_light_pass.h"
#include "passes/raytracing_pass.h"
#include <memory>

namespace Elish
{
    class UIPass;
    class RayTracingPass;

    /**
     * @brief 主渲染管线类
     * @details 负责组织和管理所有渲染通道的执行顺序，包括阴影渲染、主相机渲染和UI渲染
     */
    class RenderPipeline : public RenderPipelineBase
    {
    public:
        virtual void initialize() override final;
        virtual void forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResource> render_resource) override;
        void passUpdateAfterRecreateSwapchain();
        
        /**
         * @brief 获取UI渲染通道
         * @return UI渲染通道的共享指针
         */
        std::shared_ptr<UIPass> getUIPass() const { return m_ui_pass; }
        
        /**
         * @brief 获取方向光阴影渲染通道
         * @return 方向光阴影渲染通道的共享指针
         */
        std::shared_ptr<DirectionalLightShadowPass> getDirectionalLightShadowPass() const { 
            return std::dynamic_pointer_cast<DirectionalLightShadowPass>(m_directional_light_shadow_pass); 
        }
        
        /**
         * @brief 获取光线追踪渲染通道
         * @return 光线追踪渲染通道的共享指针
         */
        std::shared_ptr<RayTracingPass> getRayTracingPass() const { 
            return m_raytracing_pass; 
        }
        
        /**
         * @brief 启用或禁用光线追踪
         * @param enabled 是否启用光线追踪
         */
        void setRayTracingEnabled(bool enabled) override;
        
        /**
         * @brief 获取光线追踪启用状态
         * @return 是否启用光线追踪
         */
        bool isRayTracingEnabled() const override;

    private:
        std::shared_ptr<UIPass> m_ui_pass;  ///< UI渲染通道
        std::shared_ptr<RayTracingPass> m_raytracing_pass;  ///< 光线追踪渲染通道
        // 注意：m_directional_light_shadow_pass 已在基类 RenderPipelineBase 中声明，不需要重复声明
    };
} // namespace Elish
