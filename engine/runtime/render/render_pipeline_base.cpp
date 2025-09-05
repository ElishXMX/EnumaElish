#include "render_pipeline_base.h"
#include <iostream>


namespace Elish{

    void RenderPipelineBase::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        m_main_camera_pass->preparePassData(render_resource);
        // m_pick_pass->preparePassData(render_resource);
        // m_directional_light_pass->preparePassData(render_resource);
        // m_point_light_shadow_pass->preparePassData(render_resource);
        // m_particle_pass->preparePassData(render_resource);
        // g_runtime_global_context.m_debugdraw_manager->preparePassData(render_resource);
    }

     void RenderPipelineBase::forwardRender(std::shared_ptr<RHI>                rhi,
                                           std::shared_ptr<RenderResource> render_resource)
    {}


}