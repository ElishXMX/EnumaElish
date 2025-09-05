#include "render_pass_base.h"
#include "../core/log/log_system.h"
#include "../core/base/macro.h"


namespace Elish
{
    void RenderPassBase::setCommonInfo(RenderPassCommonInfo common_info)
    {
        m_rhi = common_info.rhi;
    }
    
    void RenderPassBase::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        // 默认实现为空，子类可以重载此方法
    
    }
    
} // namespace Elish