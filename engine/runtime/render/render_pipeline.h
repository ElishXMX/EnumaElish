//总渲染管线，组织数据
#pragma once

#include "render_pipeline_base.h"
#include <memory>

namespace Elish
{
    class UIPass;

    class RenderPipeline : public RenderPipelineBase
    {
    public:
        virtual void initialize() override final;
        virtual void forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResource> render_resource) override;
        void passUpdateAfterRecreateSwapchain();

    private:
        std::shared_ptr<UIPass> m_ui_pass;  ///< UI渲染通道
    };
} // namespace Elish
