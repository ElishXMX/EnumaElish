#include "ui_pass.h"
#include "../interface/vulkan/vulkan_rhi.h"
#include "../interface/vulkan/vulkan_rhi_resource.h"
#include "../../core/base/macro.h"
#include "../../core/log/log_system.h"
#include "../../global/global_context.h"
#include "../window_system.h"
#include "../render_pipeline_base.h"

// ImGui 相关头文件
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdexcept>

namespace Elish
{
    /**
     * @brief UIPass类的析构函数
     * 负责清理ImGui相关资源
     */
    UIPass::~UIPass()
    {
        cleanup();
    }

    /**
     * @brief 初始化UI渲染通道
     * 设置ImGui上下文、初始化GLFW和Vulkan后端
     */
    void UIPass::initialize()
    {
        LOG_INFO("[UIPass] Initializing UI Pass...");
        
        // 设置ImGui上下文
        setupImGuiContext();
        
        // 初始化ImGui的GLFW后端
        initializeImGuiGLFW();
        
        // 初始化ImGui的Vulkan后端
        initializeImGuiVulkan();
        
        LOG_INFO("[UIPass] UI Pass initialized successfully");
    }

    /**
     * @brief 准备UI渲染通道的数据
     * @param render_resource 渲染资源管理器
     */
    void UIPass::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        // UI Pass通常不需要从render_resource获取数据
        // 但可以在这里更新UI相关的状态
        m_render_resource = render_resource;
    }

    /**
     * @brief 渲染UI内容
     * @param command_buffer 当前的命令缓冲区
     */
    void UIPass::draw(RHICommandBuffer* command_buffer)
    {
        if (!m_imgui_initialized)
        {
            LOG_WARN("[UIPass] ImGui not initialized, skipping UI rendering");
            return;
        }

        // 开始新的ImGui帧
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 渲染UI内容
        renderUIContent();

        // 结束ImGui帧并渲染
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        
        if (draw_data && draw_data->CmdListsCount > 0)
        {
            // 使用Vulkan命令缓冲区渲染ImGui
            VkCommandBuffer vk_command_buffer = static_cast<VulkanCommandBuffer*>(command_buffer)->getResource();
            ImGui_ImplVulkan_RenderDrawData(draw_data, vk_command_buffer);
        }
    }

    /**
     * @brief 设置ImGui上下文和样式
     */
    void UIPass::setupImGuiContext()
    {
        // 创建ImGui上下文
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        
        // 启用键盘和游戏手柄控制
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        
        // 设置ImGui样式
        ImGui::StyleColorsDark();
        
        LOG_INFO("[UIPass] ImGui context created");
    }

    /**
     * @brief 初始化ImGui的GLFW后端
     */
    void UIPass::initializeImGuiGLFW()
    {
        // 获取GLFW窗口句柄
        auto window_system = g_runtime_global_context.m_window_system;
        if (!window_system)
        {
            throw std::runtime_error("[UIPass] Window system not available");
        }
        
        GLFWwindow* window = window_system->getWindow();
        if (!window)
        {
            throw std::runtime_error("[UIPass] GLFW window not available");
        }

        // 初始化ImGui GLFW后端
        if (!ImGui_ImplGlfw_InitForVulkan(window, true))
        {
            throw std::runtime_error("[UIPass] Failed to initialize ImGui GLFW backend");
        }
        
        LOG_INFO("[UIPass] ImGui GLFW backend initialized");
    }

    /**
     * @brief 初始化ImGui的Vulkan后端
     */
    void UIPass::initializeImGuiVulkan()
    {
        if (!m_rhi)
        {
            throw std::runtime_error("[UIPass] RHI not available");
        }

        VulkanRHI* vulkan_rhi = static_cast<VulkanRHI*>(m_rhi.get());
        
        // 设置ImGui Vulkan初始化信息
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vulkan_rhi->m_instance;
        init_info.PhysicalDevice = vulkan_rhi->m_physical_device;
        init_info.Device = vulkan_rhi->m_device;
        init_info.QueueFamily = vulkan_rhi->getQueueFamilyIndices().graphics_family.value();
        init_info.Queue = static_cast<VulkanQueue*>(vulkan_rhi->getGraphicsQueue())->getResource();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = static_cast<VulkanDescriptorPool*>(vulkan_rhi->getDescriptorPoor())->getResource();
        init_info.Subpass = 0; // 使用主相机渲染通道的子通道0
        init_info.MinImageCount = vulkan_rhi->getMaxFramesInFlight();
        init_info.ImageCount = vulkan_rhi->getMaxFramesInFlight();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = checkVkResult;

        // 获取主相机通道的渲染通道
        // 注意：这里需要确保UI Pass在主相机通道之后初始化
        RHIRenderPass* main_render_pass = getMainCameraRenderPass();
        if (!main_render_pass)
        {
            throw std::runtime_error("[UIPass] Main camera render pass not available");
        }

        // 初始化ImGui Vulkan后端
        if (!ImGui_ImplVulkan_Init(&init_info, static_cast<VulkanRenderPass*>(main_render_pass)->getResource()))
        {
            throw std::runtime_error("[UIPass] Failed to initialize ImGui Vulkan backend");
        }

        // 上传字体纹理
        uploadFonts(vulkan_rhi);
        
        m_imgui_initialized = true;
        LOG_INFO("[UIPass] ImGui Vulkan backend initialized");
    }

    /**
     * @brief 上传ImGui字体纹理到GPU
     * @param vulkan_rhi Vulkan RHI实例
     */
    void UIPass::uploadFonts(VulkanRHI* vulkan_rhi)
    {
        // 创建临时命令缓冲区来上传字体
        VkCommandPool command_pool = static_cast<VulkanCommandPool*>(vulkan_rhi->getCommandPoor())->getResource();
        VkDevice device = vulkan_rhi->m_device;
        
        VkCommandBuffer command_buffer;
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = command_pool;
        alloc_info.commandBufferCount = 1;
        
        vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
        
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(command_buffer, &begin_info);
        
        // 上传字体纹理
        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
        
        vkEndCommandBuffer(command_buffer);
        
        // 提交命令缓冲区
        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        
        VkQueue graphics_queue = static_cast<VulkanQueue*>(vulkan_rhi->getGraphicsQueue())->getResource();
        vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);
        
        // 清理临时命令缓冲区
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        
        // 销毁CPU端的字体纹理数据
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        
        LOG_INFO("[UIPass] ImGui fonts uploaded to GPU");
    }

    /**
     * @brief 渲染UI内容
     * 显示基础文本用于功能验证
     */
    void UIPass::renderUIContent()
    {
        // 创建一个简单的调试窗口
        ImGui::Begin("EnumaElish Engine - Debug Info");
        
        ImGui::Text("欢迎使用 EnumaElish 渲染引擎!");
        ImGui::Text("UI Pass 功能验证成功");
        ImGui::Separator();
        
        // 显示帧率信息
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                   1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        
        // 显示渲染统计信息
        if (m_render_resource)
        {
            ImGui::Text("已加载模型数量: %zu", m_render_resource->getLoadedRenderObjects().size());
        }
        
        ImGui::End();
        
        // 显示ImGui演示窗口（可选）
        static bool show_demo_window = false;
        if (ImGui::Button("显示演示窗口"))
        {
            show_demo_window = !show_demo_window;
        }
        
        if (show_demo_window)
        {
            ImGui::ShowDemoWindow(&show_demo_window);
        }
    }

    /**
     * @brief 获取主相机渲染通道
     * @return 主相机渲染通道指针
     */
    RHIRenderPass* UIPass::getMainCameraRenderPass()
    {
        // 从全局上下文获取渲染系统
        auto render_system = g_runtime_global_context.m_render_system;
        if (!render_system)
        {
            LOG_ERROR("[UIPass] Render system not available");
            return nullptr;
        }
        
        // 获取渲染管线
        auto render_pipeline = render_system->getRenderPipeline();
        if (!render_pipeline)
        {
            LOG_ERROR("[UIPass] Render pipeline not available");
            return nullptr;
        }
        
        // 获取主相机Pass
        auto main_camera_pass = render_pipeline->getMainCameraPass();
        if (!main_camera_pass)
        {
            LOG_ERROR("[UIPass] Main camera pass not available");
            return nullptr;
        }
        
        // 获取主相机Pass的渲染通道
        return static_cast<RenderPass*>(main_camera_pass.get())->getRenderPass();
    }

    /**
     * @brief Vulkan结果检查回调函数
     * @param err Vulkan错误代码
     */
    void UIPass::checkVkResult(VkResult err)
    {
        if (err != VK_SUCCESS)
        {
            LOG_ERROR("[UIPass] Vulkan error: {}", static_cast<int>(err));
            throw std::runtime_error("Vulkan operation failed in UIPass");
        }
    }

    /**
     * @brief 清理UI Pass资源
     */
    void UIPass::cleanup()
    {
        if (m_imgui_initialized)
        {
            // 等待设备空闲
            if (m_rhi)
            {
                VulkanRHI* vulkan_rhi = static_cast<VulkanRHI*>(m_rhi.get());
                vkDeviceWaitIdle(vulkan_rhi->m_device);
            }
            
            // 清理ImGui资源
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            
            m_imgui_initialized = false;
            LOG_INFO("[UIPass] ImGui resources cleaned up");
        }
    }

} // namespace Elish