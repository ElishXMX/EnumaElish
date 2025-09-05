#pragma once

#include "../render_pass.h"
#include "../render_resource.h"
#include <memory>

// 前向声明
#include <vulkan/vulkan.h>

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

    private:
        WindowUI* m_window_ui = nullptr;                           ///< 窗口UI管理器
        std::shared_ptr<RenderResource> m_render_resource;         ///< 渲染资源管理器
        bool m_imgui_initialized = false;                         ///< ImGui是否已初始化
    };
} // namespace Elish
