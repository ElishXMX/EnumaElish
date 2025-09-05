#pragma once

#include "../render_pass.h"

namespace Elish
{
    class RenderResourceBase;

    class DirectionalLightShadowPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info)  ;
        void postInitialize()  ;
        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource)  ;
        void draw() override final;

        void setPerMeshLayout(RHIDescriptorSetLayout* layout) { m_per_mesh_layout = layout; }

    private:
        void setupAttachments();
        void setupRenderPass();
        void setupFramebuffer();
        void setupDescriptorSetLayout();
        void setupPipelines();
        void setupDescriptorSet();
        void drawModel();

    private:
        RHIDescriptorSetLayout* m_per_mesh_layout;
       
    };
} // namespace Elish
