#pragma once

#include "../render_pass.h"
#include "../render_resource.h"
#include "../render_pipeline.h"
#include <memory>

// 前向声明
#include <vulkan/vulkan.h>

struct ImGuiViewport;
struct ImVec2;

namespace Elish
{
    class WindowUI;
    class VulkanRHI;
    class RHIRenderPass;
    
    struct UIPassInitInfo : RenderPassInitInfo
    {
        RHIRenderPass* render_pass;
    };

    /**
     * @brief UI渲染通道类
     * 负责使用ImGui渲染用户界面元素
     * 集成到主渲染管线中，作为最后的渲染步骤
     */
    class UIPass : public RenderPass
    {
    public:
        /**
         * @brief 构造函数
         */
        UIPass();

        /**
         * @brief 析构函数
         * 负责清理ImGui相关资源
         */
        ~UIPass();

        /**
         * @brief 初始化UI渲染通道
         * 设置ImGui上下文、初始化GLFW和Vulkan后端
         */
        void initialize() override;

        /**
         * @brief 准备UI渲染通道的数据
         * @param render_resource 渲染资源管理器
         */
        void preparePassData(std::shared_ptr<RenderResource> render_resource) override;

        /**
         * @brief 渲染UI内容
         * @param command_buffer 当前的命令缓冲区
         */
        void draw(RHICommandBuffer* command_buffer);

        /**
         * @brief 在子通道中渲染UI内容
         * 专门用于在MainCameraPass的UI子通道中调用
         * @param command_buffer 当前的命令缓冲区
         */
        void drawInSubpass(RHICommandBuffer* command_buffer);

        /**
         * @brief 检测UI是否获得焦点
         * 当UI获得焦点时，应该禁用相机视角移动
         * @return true if UI has focus, false otherwise
         */
        bool isUIFocused() const;
        
        /**
         * @brief 加载光线追踪演示场景
         * 加载包含反射材质和几何体的演示场景
         */
        void loadRayTracingDemoScene();
        
        /**
         * @brief 重置到默认场景
         * 恢复到引擎的默认场景配置
         */
        void resetToDefaultScene();
        
        /**
         * @brief 应用场景配置
         * 根据JSON配置设置场景参数
         * @param scene_config 场景配置JSON对象
         */
        void applySceneConfiguration(const json11::Json& scene_config);

        /**
         * @brief 获取主相机渲染通道实例
         * @return 主相机渲染通道指针，如果获取失败返回nullptr
         */
        class MainCameraPass* getMainCameraPassInstance() const;

    private:
        /**
         * @brief 设置ImGui上下文和样式
         */
        void setupImGuiContext();

        /**
         * @brief 初始化ImGui的GLFW后端
         */
        void initializeImGuiGLFW();

        /**
         * @brief 初始化ImGui的Vulkan后端
         */
        void initializeImGuiVulkan();

        /**
         * @brief 上传ImGui字体纹理到GPU
         * @param vulkan_rhi Vulkan RHI实例
         */
        void uploadFonts(VulkanRHI* vulkan_rhi);

        /**
         * @brief 渲染UI内容
         * 显示基础文本用于功能验证
         */
        void renderUIContent();
        
        /**
         * @brief 渲染左侧菜单栏
         * @param viewport ImGui视口
         * @param layoutState 布局状态
         * @param animated_width 动画宽度
         * @param sidebar_width 侧边栏宽度
         * @param min_width 最小宽度
         * @param max_width 最大宽度
         * @param collapsed 是否折叠
         * 输出：渲染左侧菜单栏UI
         */
        void renderLeftSidebar(ImGuiViewport* viewport, 
                               RenderPipeline::EditorLayoutState& layoutState,
                               float animated_width, float& sidebar_width,
                               float min_width, float max_width, bool& collapsed);
        
        /**
         * @brief 渲染右侧属性面板
         * @param viewport ImGui视口
         * @param layoutState 布局状态
         * @param animated_width 动画宽度
         * @param sidebar_width 侧边栏宽度
         * @param min_width 最小宽度
         * @param max_width 最大宽度
         * @param collapsed 是否折叠
         * 输出：渲染右侧属性面板UI
         */
        void renderRightPropertyPanel(ImGuiViewport* viewport,
                                      RenderPipeline::EditorLayoutState& layoutState,
                                      float animated_width, float& sidebar_width,
                                      float min_width, float max_width, bool& collapsed);
        
        /**
         * @brief 渲染底部资产面板
         * @param viewport ImGui视口
         * @param layoutState 布局状态
         * @param panel_height 面板高度
         * @param min_height 最小高度
         * @param max_height 最大高度
         * @param left_width 左侧宽度
         * @param right_width 右侧宽度
         * 输出：渲染底部资产面板UI
         */
        void renderBottomAssetPanel(ImGuiViewport* viewport,
                                    RenderPipeline::EditorLayoutState& layoutState,
                                    float& panel_height, float min_height, float max_height,
                                    float left_width, float right_width);
        
        /**
         * @brief 渲染场景层级面板
         * @param layoutState 布局状态
         * 输出：渲染场景层级列表
         */
        void renderSceneHierarchy(RenderPipeline::EditorLayoutState& layoutState);
        
        /**
         * @brief 渲染渲染设置面板
         * 输出：渲染渲染设置UI
         */
        void renderRenderSettings();
        
        /**
         * @brief 渲染光线追踪设置面板
         * 输出：渲染光线追踪设置UI
         */
        void renderRayTracingSettings();
        
        /**
         * @brief 渲染灯光控制面板
         * 输出：渲染灯光控制UI
         */
        void renderLightControl();
        
        /**
         * @brief 渲染物体属性检查器
         * @param layoutState 布局状态
         * 输出：渲染选中物体的属性编辑器
         */
        void renderObjectInspector(RenderPipeline::EditorLayoutState& layoutState);
        
        /**
         * @brief 渲染资产绑定信息
         * @param layoutState 布局状态
         * 输出：渲染选中物体的资产绑定信息
         */
        void renderAssetBindingInfo(RenderPipeline::EditorLayoutState& layoutState);
        
        /**
         * @brief 保存模型配置到JSON文件
         */
        void saveModelConfiguration();

        /**
         * @brief 获取主相机渲染通道
         * @return 主相机渲染通道指针
         */
        RHIRenderPass* getMainCameraRenderPass();

        /**
         * @brief Vulkan结果检查回调函数
         * @param err Vulkan错误代码
         */
        static void checkVkResult(VkResult err);

        /**
         * @brief 清理UI Pass资源
         */
        void cleanup();

        /**
         * @brief 检测是否显示碰撞体调试线
         * @return true if collider debug visualization is enabled
         */
        bool isShowColliders() const { return m_show_colliders; }

    private:
        /**
         * @brief 渲染碰撞体调试线
         * 在当前ImGui窗口中绘制所有碰撞体的边框线
         */
        void renderColliderDebugLines();

        WindowUI* m_window_ui = nullptr;                           ///< 窗口UI管理器
        std::shared_ptr<RenderResource> m_render_resource;         ///< 渲染资源管理器
        bool m_imgui_initialized = false;                         ///< ImGui是否已初始化
        bool m_show_colliders = false;                            ///< 是否显示碰撞体调试线
    };
} // namespace Elish
