#pragma once

#include "interface/rhi.h"
#include <memory>

namespace Elish
{
    class RHI;
    class RenderResource;

    struct RenderPassInitInfo
    {};

    struct RenderPassCommonInfo
    {
        std::shared_ptr<RHI>                rhi;
    };

    class RenderPassBase
    {
    public:
        virtual void initialize() = 0;
        virtual void setCommonInfo(RenderPassCommonInfo common_info);
        
        /**
         * @brief 准备渲染通道数据
         * @param render_resource 渲染资源管理器
         */
        virtual void preparePassData(std::shared_ptr<RenderResource> render_resource);
        

    protected:
        std::shared_ptr<RHI>                m_rhi;
    };
} // namespace Elish
