//总渲染管线，组织数据
#pragma once

#include "render_pipeline_base.h"

namespace Elish
{
    class RenderPipeline : public RenderPipelineBase
    {
    public:
        virtual void initialize() override final;
        virtual void forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResource> render_resource) override;
         void passUpdateAfterRecreateSwapchain() ;


    };
} // namespace Elish
