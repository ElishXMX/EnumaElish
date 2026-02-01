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

        /**
         * @brief 编辑器布局状态结构体
         */
        struct EditorLayoutState {
            float sidebarWidth = 300.0f;        // 侧边栏宽度
            float assetPanelHeight = 200.0f;    // 底部资产面板高度
            bool isSidebarCollapsed = false;    // 侧边栏是否折叠
            bool isAssetPanelCollapsed = false; // 资产面板是否折叠

            // 计算出的渲染视口区域 (每一帧由 UIPass 更新)
            struct ViewportRect {
                float x = 0.0f;
                float y = 0.0f;
                float width = 100.0f;
                float height = 100.0f;
            } sceneViewport;
            
            // 视口尺寸改变标志 (用于触发相机投影矩阵更新)
            bool isViewportDirty = true;
        };

        /**
         * @brief 获取编辑器布局状态
         */
        EditorLayoutState& getEditorLayoutState() { return m_editor_layout_state; }
        const EditorLayoutState& getEditorLayoutState() const { return m_editor_layout_state; }

    private:
        EditorLayoutState m_editor_layout_state;

        /**
         * @brief RT任务监控结构
         * @details 记录RT任务开始/结束时间、耗时、成功/失败次数、异常统计与超时阈值
         */
        struct RTTaskMonitor {
            bool running = false;
            bool completed = false;
            uint64_t start_ms = 0;
            uint64_t end_ms = 0;
            uint64_t last_duration_ms = 0;
            uint64_t timeout_threshold_ms = 500;
            uint64_t success_count = 0;
            uint64_t failure_count = 0;
            uint64_t last_error_timestamp_ms = 0;
            uint32_t error_count_window = 0;
            uint64_t window_start_ms = 0;
            void begin();
            void finish(bool success);
            bool isTimeout(uint64_t now_ms) const;
        } m_rt_monitor;

        // 任务队列简化：记录RT完成后的回调（用于触发UI）
        std::function<void()> m_rt_complete_callback;
        
        // 控制是否在本帧执行RT图像复制到交换链（默认关闭以避免覆盖UI）
        bool m_rt_copy_enabled = false;

        /**
         * @brief 将光线追踪输出图像复制到交换链图像
         * @param rhi RHI接口
         * @param swapchain_image_index 交换链图像索引
         */
        void copyRayTracingOutputToSwapchain(std::shared_ptr<RHI> rhi, uint32_t swapchain_image_index);
        
        std::shared_ptr<UIPass> m_ui_pass;  ///< UI渲染通道
        std::shared_ptr<RayTracingPass> m_raytracing_pass;  ///< 光线追踪渲染通道
        // 注意：m_directional_light_shadow_pass 已在基类 RenderPipelineBase 中声明，不需要重复声明
    };
} // namespace Elish
