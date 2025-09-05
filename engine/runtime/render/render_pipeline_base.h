#pragma once

#include <memory>
#include <vector>

#include "render_pass_base.h"

namespace Elish
{
    class RHI;
    class RenderResource;

    

    class RenderPipelineBase
    {
        friend class RenderSystem;

    public:        
        virtual void initialize() = 0;//成为纯虚类，不可实例化
        
        virtual void forwardRender(std::shared_ptr<RHI> rhi,
                                   std::shared_ptr<RenderResource> render_resource) = 0;

        virtual void preparePassData(std::shared_ptr<RenderResource> render_resource);

    protected:
        std::shared_ptr<RHI> m_rhi;
        std::shared_ptr<RenderPassBase> m_main_camera_pass;
    };
} // namespace Elish
