#pragma once

#include "../render_pass.h"

namespace Elish
{
    class WindowUI;

    struct UIPassInitInfo : RenderPassInitInfo
    {
        RHIRenderPass* render_pass;
    };

    class UIPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info);
        void draw() override final;

    private:
        void uploadFonts();

    private:
        WindowUI* m_window_ui;
    };
} // namespace Elish
