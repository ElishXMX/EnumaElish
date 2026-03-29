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
     * @brief 光线追踪渲染模式枚举
     * @details 定义光线追踪的不同使用场景
     */
    enum class RayTracingMode {
        DISABLED,           ///< 禁用光线追踪
        DEBUG_VIEW,         ///< 调试视图：直接显示光线追踪结果
        REFLECTION_ONLY,    ///< 仅反射：用于反射表面
        REFRACTION_ONLY,    ///< 仅折射：用于透明材质
        FULL                ///< 完整模式：反射+折射
    };

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
         * @brief 设置光线追踪渲染模式
         * @param mode 渲染模式
         */
        void setRayTracingMode(RayTracingMode mode) { m_rt_mode = mode; }
        
        /**
         * @brief 获取光线追踪渲染模式
         * @return 当前渲染模式
         */
        RayTracingMode getRayTracingMode() const { return m_rt_mode; }

        /**
         * @brief 编辑器布局状态结构体
         * @details 管理三栏布局：左侧菜单栏、底部资产栏、右侧属性面板
         */
        struct EditorLayoutState {
            float leftSidebarWidth = 280.0f;    // 左侧菜单栏宽度
            float rightSidebarWidth = 320.0f;   // 右侧属性面板宽度
            float bottomPanelHeight = 200.0f;   // 底部资产面板高度
            
            bool isLeftSidebarCollapsed = false;   // 左侧菜单栏是否折叠
            bool isRightSidebarCollapsed = false;  // 右侧属性面板是否折叠
            bool isBottomPanelCollapsed = false;   // 底部资产面板是否折叠

            // 计算出的渲染视口区域 (每一帧由 UIPass 更新)
            struct ViewportRect {
                float x = 0.0f;
                float y = 0.0f;
                float width = 100.0f;
                float height = 100.0f;
            } sceneViewport;
            
            // 视口尺寸改变标志 (用于触发相机投影矩阵更新)
            bool isViewportDirty = true;
            
            // 当前选中的物体索引 (-1表示未选中)
            int selectedObjectIndex = -1;
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
        
        // 光线追踪渲染模式（启用调试视图模式进行调试）
        RayTracingMode m_rt_mode = RayTracingMode::DEBUG_VIEW;

        /**
         * @brief 将光线追踪输出图像复制到交换链图像（仅用于调试模式）
         * @param rhi RHI接口
         * @param swapchain_image_index 交换链图像索引
         */
        void copyRayTracingOutputToSwapchain(std::shared_ptr<RHI> rhi, uint32_t swapchain_image_index);
        
        std::shared_ptr<UIPass> m_ui_pass;  ///< UI渲染通道
        std::shared_ptr<RayTracingPass> m_raytracing_pass;  ///< 光线追踪渲染通道
        // 注意：m_directional_light_shadow_pass 已在基类 RenderPipelineBase 中声明，不需要重复声明
    };
} // namespace Elish
