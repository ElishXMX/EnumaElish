#include "render_pipeline_base.h"
#include <iostream>
#include "../core/base/macro.h"


namespace Elish{

    /**
     * @brief 准备渲染通道数据
     * 
     * @param render_resource 渲染资源的共享指针
     * 
     * @details 该函数负责为渲染管线中的各个通道准备必要的渲染数据：
     * 1. 准备定向光源阴影通道(Directional Light Shadow Pass)的数据
     *    - 用于生成阴影贴图
     *    - 处理光源相关的渲染数据
     * 
     * 2. 准备主相机通道(Main Camera Pass)的数据
     *    - 处理场景的主要渲染数据
     *    - 设置相机视角相关参数
     */
    void RenderPipelineBase::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        // LOG_DEBUG("[RenderPipelineBase::preparePassData] Starting preparePassData");
        
        // 准备定向光源阴影通道的数据
        // LOG_DEBUG("[RenderPipelineBase::preparePassData] About to call directional light shadow pass preparePassData");
        m_directional_light_shadow_pass->preparePassData(render_resource);
        // LOG_DEBUG("[RenderPipelineBase::preparePassData] Directional light shadow pass preparePassData completed");
        
        // 准备主相机通道的数据
        // LOG_DEBUG("[RenderPipelineBase::preparePassData] About to call main camera pass preparePassData");
        m_main_camera_pass->preparePassData(render_resource);
        // LOG_DEBUG("[RenderPipelineBase::preparePassData] Main camera pass preparePassData completed");
    }

     void RenderPipelineBase::forwardRender(std::shared_ptr<RHI>                rhi,
                                           std::shared_ptr<RenderResource> render_resource)
    {}


}