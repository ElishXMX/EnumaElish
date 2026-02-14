#include "ui_pass.h"
#include "../interface/vulkan/vulkan_rhi.h"
#include "../interface/vulkan/vulkan_rhi_resource.h"
#include "../../core/base/macro.h"
#include "../../core/log/log_system.h"
#include "../../core/asset/asset_manager.h"
#include "../../ui/asset_browser_ui.h"
#include "../../global/global_context.h"
#include "../window_system.h"
#include "../render_pipeline.h"
#include "../render_system.h"
#include "main_camera_pass.h"

// ImGui 相关头文件
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <algorithm>

#include <stdexcept>
#include <fstream>
#include "../../3rdparty/json11/json11.hpp"

namespace Elish
{
    UIPass::UIPass()
    {
        // LOG_INFO("[UIPass] UIPass constructor called.");
        setupImGuiContext();
    }

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
        
        
        // 设置ImGui上下文
        setupImGuiContext();
        
        // 初始化ImGui的GLFW后端
        initializeImGuiGLFW();
        
        // 初始化ImGui的Vulkan后端
        initializeImGuiVulkan();
        
        
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
    /**
     * @brief UI渲染通道的主绘制函数
     * @details 处理ImGui的帧开始、内容渲染和帧结束，确保在正确的渲染通道内执行
     * @param command_buffer 当前的命令缓冲区
     */
    void UIPass::draw(RHICommandBuffer* command_buffer)
    {
        if (!m_imgui_initialized)
        {
            LOG_WARN("[UIPass] ImGui not initialized, skipping UI rendering");
            return;
        }

        // 验证命令缓冲区有效性
        if (!command_buffer) {
            LOG_ERROR("[UIPass] Invalid command buffer provided to UI draw");
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
        
        // 只有在有实际绘制数据时才进行渲染
        if (draw_data && draw_data->CmdListsCount > 0 && draw_data->TotalVtxCount > 0)
        {
            // 使用Vulkan命令缓冲区渲染ImGui
            // 注意：ImGui_ImplVulkan_RenderDrawData 期望在活跃的渲染通道内被调用
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
        // LOG_INFO("[UIPass] ImGui context created.");
        
        // 启用键盘和游戏手柄控制
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        
        // 设置ImGui样式
        ImGui::StyleColorsDark();

        // 加载中文字体
        // 检查字体文件是否存在，如果不存在则使用默认字体
        // 微软雅黑字体路径，通常在Windows系统上可用
        const char* font_path = "C:\\Windows\\Fonts\\msyh.ttc";
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false; // 字体数据由我们管理
        // 合并到默认字体，这样可以保留默认字体的ASCII字符，同时添加中文支持
        // 如果只加载中文字体，可能会导致英文字符显示不正常
        ImFont* default_font = io.Fonts->AddFontDefault(&font_cfg);
        if (default_font) {
            // LOG_INFO("[UIPass] Default font added.");
        } else {
            LOG_ERROR("[UIPass] Failed to add default font.");
        }
        
        // 添加中文字体，指定字体大小
        // 注意：ImGui内部使用UTF-8编码，所以传入的字符串应该是UTF-8编码
        // 如果你的字符串是GBK编码，需要先转换为UTF-8
        // LOG_INFO("[UIPass] Attempting to load Chinese font from: %s with size 16.0f", font_path);
        ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 30.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
        if (font)
        {
            // LOG_INFO("[UIPass] Successfully loaded Chinese font: %s", font_path);
        }
        else
        {
            LOG_ERROR("[UIPass] Failed to load Chinese font: %s. Using default font. Error: Font pointer is null.", font_path);
        }

        
        
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
        init_info.Subpass = 1; // 使用主相机渲染通道的子通道1（UI子通道）
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
        
        // LOG_INFO("[UIPass] ImGui fonts uploaded to GPU");
    }

    /**
     * @brief 渲染UI内容
     * 显示模型变换控制界面
     */
    void UIPass::renderUIContent()
    {
        // 检查渲染资源是否有效
        if (!m_render_resource) {
            LOG_WARN("[UIPass] Render resource is null, UI content may be limited");
        }

        // 获取 RenderPipeline 以访问 EditorLayoutState
        auto render_system = g_runtime_global_context.m_render_system;
        std::shared_ptr<RenderPipeline> render_pipeline;
        if (render_system) {
            render_pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_system->getRenderPipeline());
        }

        if (!render_pipeline) {
             LOG_ERROR("[UIPass] RenderPipeline not available");
             return;
        }

        // 获取全局 EditorLayoutState
        auto& layoutState = render_pipeline->getEditorLayoutState();
        
        // 🎨 主窗口 - 左侧边栏布局，为3D渲染留出空间
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        // 检查视口有效性
        if (!viewport) {
            LOG_ERROR("[UIPass] ImGui viewport is null, cannot render UI");
            return;
        }
        
        // 📏 左侧边栏配置（使用 EditorLayoutState）
        float& sidebar_width = layoutState.sidebarWidth;
        bool& sidebar_collapsed = layoutState.isSidebarCollapsed;
        
        // ⌨️ Keyboard Shortcuts
        if (ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_B)) {
                sidebar_collapsed = !sidebar_collapsed;
            }
            if (!sidebar_collapsed) {
                if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
                    sidebar_width = std::max(200.0f, sidebar_width - 5.0f);
                }
                if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
                    sidebar_width = std::min(500.0f, sidebar_width + 5.0f);
                }
            }
        }

        // 🎬 Animation State
        static float animated_content_width = sidebar_width;
        float target_content_width = sidebar_collapsed ? 0.0f : sidebar_width;
        
        // Smooth transition
        float animation_speed = 15.0f * ImGui::GetIO().DeltaTime;
        if (std::abs(animated_content_width - target_content_width) > 0.5f) {
            animated_content_width += (target_content_width - animated_content_width) * animation_speed;
            layoutState.isViewportDirty = true; // Keep updating viewport during animation
        } else {
            animated_content_width = target_content_width;
        }

        // Calculate window width based on animated content width
        // Base width (button area) is 30.0f
        // Splitter (8.0f) is only needed if content is visible (width > 0)
        float splitter_width = (animated_content_width > 1.0f) ? 8.0f : 0.0f;
        // 增加额外的缓冲宽度 (2.0f)，防止因浮点精度或微小间距导致 Splitter 被裁剪
        float window_width = 30.0f + animated_content_width + splitter_width + 2.0f;
        
        // 确保窗口宽度不超过视口宽度的80%
        float max_window_width = viewport->WorkSize.x * 0.8f;
        window_width = std::min(window_width, max_window_width);

        // 底部面板配置
        static float bottom_panel_height = 250.0f;
        const float min_bottom_panel_height = 150.0f;
        const float max_bottom_panel_height = std::max(min_bottom_panel_height, viewport->WorkSize.y * 0.6f);

        // 更新 layoutState 中的视口信息 (这是关键步骤，将 UI 布局同步回渲染管线)
        // 视口起始 X 坐标为侧边栏宽度
        layoutState.sceneViewport.x = window_width;
        layoutState.sceneViewport.y = 0.0f;
        // 视口宽度为总宽度减去侧边栏宽度
        layoutState.sceneViewport.width = viewport->WorkSize.x - window_width;
        // 视口高度减去底部面板高度
        layoutState.sceneViewport.height = viewport->WorkSize.y - bottom_panel_height;
        
        // 标记视口尺寸可能已改变
        layoutState.isViewportDirty = true;
        
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(ImVec2(window_width, viewport->WorkSize.y)); // Sidebar takes full height visually, but splitter will limit interaction
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("EnumaElish Engine - Main Interface", nullptr, window_flags);
        ImGui::PopStyleVar(3);
        
        // 📏 左侧边栏配置（使用前面声明的变量）
        const float min_sidebar_width = 200.0f;
        const float max_sidebar_width = 500.0f; // 限制最大宽度为 500px
        
        // 🔄 边栏折叠/展开按钮
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.7f, 1.0f));
        
        const char* button_text = sidebar_collapsed ? "▶" : "◀";
        const char* tooltip_text = sidebar_collapsed ? "Expand Control Panel" : "Collapse Control Panel";
        
        if (ImGui::Button(button_text, ImVec2(30, 40)))
        {
            sidebar_collapsed = !sidebar_collapsed;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip_text);
        }
        
        ImGui::PopStyleColor(3);
        
        // 🎯 左侧边栏区域（仅在未折叠时显示）
        if (animated_content_width > 1.0f)
        {
            // 紧凑排列，消除间距，确保 Splitter 不会被挤出窗口
            ImGui::SameLine(0.0f, 0.0f);
            // 使用动画宽度作为子窗口宽度，确保平滑过渡且不超出主窗口
            ImGui::BeginChild("Sidebar", ImVec2(animated_content_width, 0), true, ImGuiWindowFlags_NoScrollbar);
        
            ImGui::TextDisabled("MAIN CONTROLS");
            ImGui::Spacing();

            // 📊 性能概览 (Simplified)
            static bool show_performance = true;
            if (ImGui::CollapsingHeader("📊 Performance", &show_performance, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
                ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
                if (m_render_resource)
                {
                    ImGui::Text("Objects: %zu", m_render_resource->getLoadedRenderObjects().size());
                }
                ImGui::Unindent(10.0f);
            }
        
            ImGui::Spacing();
        
            // 🏗️ 场景层级与检视器 (Merged Hierarchy & Inspector)
            static bool show_hierarchy = true;
            if (ImGui::CollapsingHeader("🏗️ Hierarchy & Inspector", &show_hierarchy, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
            
                if (m_render_resource && !m_render_resource->getLoadedRenderObjects().empty())
                {
                    auto& renderObjects = m_render_resource->getLoadedRenderObjects();
                    static int selected_model = 0;
                    
                    // 1. Hierarchy List
                    ImGui::Text("Scene Objects:");
                    if (ImGui::BeginListBox("##SceneObjects", ImVec2(-1, 100)))
                    {
                        for (size_t i = 0; i < renderObjects.size(); ++i)
                        {
                            bool is_selected = (selected_model == static_cast<int>(i));
                            if (ImGui::Selectable(renderObjects[i].name.c_str(), is_selected))
                            {
                                selected_model = static_cast<int>(i);
                            }
                            if (is_selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndListBox();
                    }
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                
                    // 2. Inspector (Transform)
                    if (selected_model >= 0 && selected_model < static_cast<int>(renderObjects.size()))
                    {
                        auto& selectedObject = renderObjects[selected_model];
                        auto& animParams = selectedObject.animationParams;
                        
                        ImGui::Text("Inspector: %s", selectedObject.name.c_str());
                        ImGui::Spacing();
                        
                        // Position
                        ImGui::Text("Position");
                        float position[3] = { animParams.position.x, animParams.position.y, animParams.position.z };
                        ImGui::PushItemWidth(-1);
                        if (ImGui::DragFloat3("##Position", position, 0.1f))
                        {
                            ModelAnimationParams updatedParams = animParams;
                            updatedParams.position = glm::vec3(position[0], position[1], position[2]);
                            m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                        }
                        ImGui::PopItemWidth();
                        
                        // Rotation
                        ImGui::Text("Rotation");
                        float rotation[3] = { 
                            glm::degrees(animParams.rotation.x), 
                            glm::degrees(animParams.rotation.y), 
                            glm::degrees(animParams.rotation.z) 
                        };
                        ImGui::PushItemWidth(-1);
                        if (ImGui::DragFloat3("##Rotation", rotation, 1.0f))
                        {
                            ModelAnimationParams updatedParams = animParams;
                            updatedParams.rotation = glm::vec3(
                                glm::radians(rotation[0]), 
                                glm::radians(rotation[1]), 
                                glm::radians(rotation[2])
                            );
                            m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                        }
                        ImGui::PopItemWidth();
                        
                        // Scale
                        ImGui::Text("Scale");
                        float scale[3] = { animParams.scale.x, animParams.scale.y, animParams.scale.z };
                        ImGui::PushItemWidth(-1);
                        if (ImGui::DragFloat3("##Scale", scale, 0.01f, 0.01f, 10.0f))
                        {
                            ModelAnimationParams updatedParams = animParams;
                            updatedParams.scale = glm::vec3(scale[0], scale[1], scale[2]);
                            m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                        }
                        ImGui::PopItemWidth();
                        
                        ImGui::Spacing();
                        
                        // Auto Rotate
                        bool enableAnimation = animParams.enableAnimation;
                        if (ImGui::Checkbox("Auto Rotate", &enableAnimation))
                        {
                            ModelAnimationParams updatedParams = animParams;
                            updatedParams.enableAnimation = enableAnimation;
                            if (enableAnimation) {
                                updatedParams.rotationSpeed = 1.0f;
                                updatedParams.rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                            }
                            m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                        }
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No objects in scene");
                }
                ImGui::Unindent(10.0f);
            }
        
        ImGui::Spacing();
        
        // 🌅 渲染设置折叠菜单
        static bool show_render_settings = true;
        if (ImGui::CollapsingHeader("🌅 Render Settings", &show_render_settings, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);
            
            MainCameraPass* main_camera_pass = getMainCameraPassInstance();
            if (main_camera_pass)
            {
                // 背景绘制控制
                bool enable_background = main_camera_pass->isBackgroundEnabled();
                if (ImGui::Checkbox("Enable Background", &enable_background))
                {
                    main_camera_pass->setBackgroundEnabled(enable_background);
                }
                
                // 天空盒绘制控制
                bool enable_skybox = main_camera_pass->isSkyboxEnabled();
                if (ImGui::Checkbox("Enable Skybox", &enable_skybox))
                {
                    main_camera_pass->setSkyboxEnabled(enable_skybox);
                }
                
                // 添加说明文本
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Background: IBL environment map");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Skybox: 3D environment cube");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Main camera pass not available");
            }
            
            ImGui::Unindent(10.0f);
        }
        
        ImGui::Spacing();
        
        // 🌟 光线追踪设置折叠菜单
        static bool show_raytracing_settings = true;
        if (ImGui::CollapsingHeader("🌟 Ray Tracing Settings", &show_raytracing_settings, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);
            
            // 获取渲染管线实例
            auto render_system = g_runtime_global_context.m_render_system;
            if (render_system)
            {
                auto render_pipeline = render_system->getRenderPipeline();
                if (render_pipeline)
                {
                    // 光线追踪启用/禁用控制
                    bool raytracing_enabled = render_pipeline->isRayTracingEnabled();
                    if (ImGui::Checkbox("Enable Ray Tracing", &raytracing_enabled))
                    {
                        render_pipeline->setRayTracingEnabled(raytracing_enabled);
                    }
                    
                    if (raytracing_enabled)
                    {
                        ImGui::Spacing();
                        
                        // 1. Effects (Common)
                        static bool enable_reflections = true;
                        static bool enable_shadows = true;
                        static bool enable_global_illumination = false;
                        
                        ImGui::Text("Effects:");
                        ImGui::Checkbox("Reflections", &enable_reflections);
                        ImGui::SameLine();
                        ImGui::Checkbox("Shadows", &enable_shadows);
                        ImGui::SameLine();
                        ImGui::Checkbox("GI", &enable_global_illumination);
                        
                        ImGui::Spacing();
                        
                        // 2. Quality Presets (Common)
                        ImGui::Text("Quality:");
                        static int max_ray_depth = 5;
                        static int samples_per_pixel = 1;
                        static float resolution_scale = 1.0f;

                        if (ImGui::Button("Low")) { max_ray_depth = 3; samples_per_pixel = 1; resolution_scale = 0.5f; }
                        ImGui::SameLine();
                        if (ImGui::Button("Med")) { max_ray_depth = 5; samples_per_pixel = 2; resolution_scale = 0.75f; }
                        ImGui::SameLine();
                        if (ImGui::Button("High")) { max_ray_depth = 8; samples_per_pixel = 4; resolution_scale = 1.0f; }
                        ImGui::SameLine();
                        if (ImGui::Button("Ultra")) { max_ray_depth = 10; samples_per_pixel = 8; resolution_scale = 1.0f; }
                        
                        ImGui::Spacing();
                        
                        // 3. Advanced Parameters (Folded)
                        if (ImGui::TreeNode("Advanced Parameters"))
                        {
                            ImGui::Text("Max Ray Depth:");
                            ImGui::SliderInt("##MaxRayDepth", &max_ray_depth, 1, 10);
                            
                            ImGui::Text("Samples Per Pixel:");
                            ImGui::SliderInt("##SamplesPerPixel", &samples_per_pixel, 1, 16);
                            
                            ImGui::Text("Resolution Scale:");
                            ImGui::SliderFloat("##ResolutionScale", &resolution_scale, 0.25f, 2.0f, "%.2fx");
                            
                            ImGui::Separator();
                            
                            static int render_mode = 0;
                            const char* render_modes[] = { "Hybrid", "Pure RT", "Raster Only" };
                            ImGui::Combo("Render Mode", &render_mode, render_modes, IM_ARRAYSIZE(render_modes));
                            
                            ImGui::TreePop();
                        }
                        
                        // 4. Demo Scenes (Folded)
                        if (ImGui::TreeNode("Demo Scenes"))
                        {
                             if (ImGui::Button("Load RT Demo Scene", ImVec2(-1, 0)))
                             {
                                 loadRayTracingDemoScene();
                             }
                             if (ImGui::Button("Reset Scene", ImVec2(-1, 0)))
                             {
                                 resetToDefaultScene();
                             }
                             ImGui::TreePop();
                        }
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "RT is disabled.");
                    }
                }
            }
            ImGui::Unindent(10.0f);
        }
        
        ImGui::Spacing();
        
        // ☀️ 光源控制折叠菜单
        static bool show_light_control = true;
        if (ImGui::CollapsingHeader("☀️ Light Control", &show_light_control, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);
            
            if (m_render_resource)
            {
                // 获取主方向光源
                const DirectionalLightData* primary_light = m_render_resource->getPrimaryDirectionalLight();
                if (primary_light)
                {
                    // 创建可修改的光源方向副本
                    static glm::vec3 light_direction = primary_light->direction;
                    
                    ImGui::Text("Directional Light Settings:");
                    ImGui::Spacing();
                    
                    // 光源方向控制
                    ImGui::Text("Direction:");
                    float direction[3] = { light_direction.x, light_direction.y, light_direction.z };
                    ImGui::PushItemWidth(-1);
                    if (ImGui::DragFloat3("##LightDirection", direction, 0.01f, -1.0f, 1.0f, "%.2f"))
                    {
                        light_direction = glm::vec3(direction[0], direction[1], direction[2]);
                        // 归一化方向向量
                        if (glm::length(light_direction) > 0.001f)
                        {
                            light_direction = glm::normalize(light_direction);
                            // 更新渲染资源中的光源数据
                            m_render_resource->updateDirectionalLightDirection(light_direction);
                        }
                    }
                    ImGui::PopItemWidth();
                    
                    ImGui::Spacing();
                    
                    // 光源强度控制
                    static float light_intensity = primary_light->intensity;
                    ImGui::Text("Intensity:");
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderFloat("##LightIntensity", &light_intensity, 0.0f, 5.0f, "%.2f"))
                    {
                        m_render_resource->updateDirectionalLightIntensity(light_intensity);
                    }
                    ImGui::PopItemWidth();
                    
                    ImGui::Spacing();
                    
                    // 光源颜色控制
                    static glm::vec3 light_color = primary_light->color;
                    ImGui::Text("Color:");
                    float color[3] = { light_color.r, light_color.g, light_color.b };
                    ImGui::PushItemWidth(-1);
                    if (ImGui::ColorEdit3("##LightColor", color))
                    {
                        light_color = glm::vec3(color[0], color[1], color[2]);
                        m_render_resource->updateDirectionalLightColor(light_color);
                    }
                    ImGui::PopItemWidth();
                    
                    ImGui::Spacing();
                    
                    // 光源距离控制
                    static float light_distance = primary_light->distance;
                    ImGui::Text("Distance (Shadow Range):");
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderFloat("##LightDistance", &light_distance, 1.0f, 50.0f, "%.1f"))
                    {
                        m_render_resource->updateDirectionalLightDistance(light_distance);
                    }
                    ImGui::PopItemWidth();
                    
                    // 显示深度贴图范围信息
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("📊 Shadow Map Info:");
                    
                    // 计算并显示深度贴图范围参数
                    float base_scene_radius = 20.0f;
                    float distance_factor = glm::clamp(light_distance / 20.0f, 0.8f, 3.0f);
                    float shadow_coverage_radius = base_scene_radius * distance_factor;
                    float ortho_near = std::max(0.1f, light_distance * 0.1f);
                    float ortho_far = light_distance + shadow_coverage_radius * 2.0f;
                    
                    ImGui::Text("Near Plane: %.2f", ortho_near);
                    ImGui::Text("Far Plane: %.2f", ortho_far);
                    ImGui::Text("Coverage Radius: %.2f", shadow_coverage_radius);
                    ImGui::Text("Depth Range: %.2f", ortho_far - ortho_near);
                    
                    ImGui::Spacing();
                    
                    // 预设光源方向按钮
                    ImGui::Text("Presets:");
                    if (ImGui::Button("Top-Down", ImVec2(80, 0)))
                    {
                        light_direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
                        m_render_resource->updateDirectionalLightDirection(light_direction);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Side", ImVec2(60, 0)))
                    {
                        light_direction = glm::normalize(glm::vec3(-1.0f, -0.5f, 0.0f));
                        m_render_resource->updateDirectionalLightDirection(light_direction);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Diagonal", ImVec2(70, 0)))
                    {
                        light_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
                        m_render_resource->updateDirectionalLightDirection(light_direction);
                    }
                    
                    if (ImGui::Button("Reset All", ImVec2(-1, 0)))
                    {
                        light_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
                        light_intensity = 1.0f;
                        light_color = glm::vec3(1.0f, 1.0f, 1.0f);
                        light_distance = 10.0f;
                        m_render_resource->updateDirectionalLightDirection(light_direction);
                        m_render_resource->updateDirectionalLightIntensity(light_intensity);
                        m_render_resource->updateDirectionalLightColor(light_color);
                        m_render_resource->updateDirectionalLightDistance(light_distance);
                    }
                    
                    ImGui::Spacing();
                    
                    // 显示当前光源信息
                    ImGui::Separator();
                    ImGui::Text("Current Light Info:");
                    ImGui::Text("Direction: (%.2f, %.2f, %.2f)", light_direction.x, light_direction.y, light_direction.z);
                    ImGui::Text("Intensity: %.2f", light_intensity);
                    ImGui::Text("Color: (%.2f, %.2f, %.2f)", light_color.r, light_color.g, light_color.b);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "No directional light available");
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Render resource not available");
            }
            
            ImGui::Unindent(10.0f);
        }
        
        ImGui::Spacing();
        
        // 🎭 场景对象折叠菜单
        static bool show_scene = true;
        if (ImGui::CollapsingHeader("🎭 Scene Objects", &show_scene, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);
            if (m_render_resource && !m_render_resource->getLoadedRenderObjects().empty())
            {
                const auto& renderObjects = m_render_resource->getLoadedRenderObjects();
                for (size_t i = 0; i < renderObjects.size(); ++i)
                {
                    const auto& obj = renderObjects[i];
                    ImGui::BulletText("%s", obj.name.c_str());
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No objects loaded");
            }
            ImGui::Unindent(10.0f);
        }
        
        ImGui::Spacing();
        
        // 🔍 光线追踪调试面板
        static bool show_rt_debug = false;
        if (ImGui::CollapsingHeader("🔍 Ray Tracing Debug", &show_rt_debug))
        {
            ImGui::Indent(10.0f);
            
            auto render_system = g_runtime_global_context.m_render_system;
            if (render_system)
            {
                auto render_pipeline = render_system->getRenderPipeline();
                if (render_pipeline && render_pipeline->isRayTracingEnabled())
                {
                    ImGui::Text("📊 RT Status & Performance:");
                    ImGui::Spacing();
                    
                    // 光线追踪状态信息
                    ImGui::Text("Status: ");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
                    
                    // 性能监控数据（模拟数据，实际应从渲染系统获取）
                    static float rt_frame_time = 16.7f;
                    static float rays_per_second = 125.5f;
                    static int active_rays = 1920 * 1080;
                    
                    ImGui::Text("Frame Time: %.2f ms", rt_frame_time);
                    ImGui::Text("Rays/Second: %.1fM", rays_per_second);
                    ImGui::Text("Active Rays: %d", active_rays);
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("🎨 Debug Visualization:");
                    ImGui::Spacing();
                    
                    // 调试可视化选项
                    static bool show_ray_directions = false;
                    static bool show_intersection_points = false;
                    static bool show_bvh_bounds = false;
                    static bool show_material_ids = false;
                    static bool show_depth_complexity = false;
                    
                    ImGui::Checkbox("Ray Directions", &show_ray_directions);
                    if (show_ray_directions)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Lines)");
                    }
                    
                    ImGui::Checkbox("Intersection Points", &show_intersection_points);
                    if (show_intersection_points)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Dots)");
                    }
                    
                    ImGui::Checkbox("BVH Bounds", &show_bvh_bounds);
                    if (show_bvh_bounds)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Wireframe)");
                    }
                    
                    ImGui::Checkbox("Material IDs", &show_material_ids);
                    if (show_material_ids)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Color Coded)");
                    }
                    
                    ImGui::Checkbox("Depth Complexity", &show_depth_complexity);
                    if (show_depth_complexity)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Heat Map)");
                    }
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("⚙️ Debug Controls:");
                    ImGui::Spacing();
                    
                    // 调试控制按钮
                    if (ImGui::Button("Capture Frame", ImVec2(120, 0)))
                    {
                        // TODO: 实现帧捕获功能
                        LOG_INFO("[RT Debug] Frame capture requested");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Export Stats", ImVec2(120, 0)))
                    {
                        // TODO: 实现统计数据导出
                        LOG_INFO("[RT Debug] Stats export requested");
                    }
                    
                    if (ImGui::Button("Reset Counters", ImVec2(-1, 0)))
                    {
                        // TODO: 重置性能计数器
                        rt_frame_time = 0.0f;
                        rays_per_second = 0.0f;
                        LOG_INFO("[RT Debug] Performance counters reset");
                    }
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("📈 Memory Usage:");
                    ImGui::Spacing();
                    
                    // 内存使用情况（模拟数据）
                    static float bvh_memory = 45.2f; // MB
                    static float texture_memory = 128.7f; // MB
                    static float buffer_memory = 32.1f; // MB
                    
                    ImGui::Text("BVH Memory: %.1f MB", bvh_memory);
                    ImGui::Text("Texture Memory: %.1f MB", texture_memory);
                    ImGui::Text("Buffer Memory: %.1f MB", buffer_memory);
                    ImGui::Text("Total RT Memory: %.1f MB", bvh_memory + texture_memory + buffer_memory);
                    
                    ImGui::Spacing();
                    
                    // 内存使用进度条
                    float total_memory = bvh_memory + texture_memory + buffer_memory;
                    float memory_usage_ratio = total_memory / 512.0f; // 假设512MB为上限
                    ImGui::Text("Memory Usage:");
                    ImGui::ProgressBar(memory_usage_ratio, ImVec2(-1, 0), 
                                     (std::to_string(static_cast<int>(memory_usage_ratio * 100)) + "%").c_str());
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Ray tracing is disabled.");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enable RT to access debug features.");
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Render system not available");
            }
            
            ImGui::Unindent(10.0f);
        }
        
            ImGui::EndChild(); // 结束左侧边栏
            
            // 📏 可拖拽的分隔线（仅在边栏展开时显示）
            // 显式设置光标位置到侧边栏右侧（基于当前动画宽度），确保位置准确且可见
            ImGui::SetCursorPos(ImVec2(30.0f + animated_content_width, 0.0f));
            
            // 使用不可见按钮作为交互区域
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            
            // 按钮填充剩余高度
            ImGui::Button("##splitter", ImVec2(8.0f, -1));
            
            ImGui::PopStyleColor(3);
            
            // 绘制可视化的分隔线 (类似底部面板的样式)
            bool is_hovered = ImGui::IsItemHovered();
            bool is_active = ImGui::IsItemActive();
            
            ImU32 color;
            if (is_active) color = ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
            else if (is_hovered) color = ImGui::GetColorU32(ImVec4(0.4f, 0.6f, 0.8f, 0.8f));
            else color = ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f)); // Make it slightly darker/visible
            
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetItemRectMin(), 
                ImGui::GetItemRectMax(), 
                color
            );

            // 绘制一个细边框，确保即使背景色很暗也能看清位置
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), 
                ImGui::GetItemRectMax(), 
                ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 0.3f))
            );
            
            // 检测鼠标悬停状态
            if (is_hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); // 设置水平调整光标
                ImGui::SetTooltip("Drag to resize sidebar");
            }
            
            // 处理拖拽逻辑
            if (is_active)
            {
                 float delta_x = ImGui::GetIO().MouseDelta.x;
                 sidebar_width += delta_x;
                 
                 // 应用边界限制，同时考虑视口限制
                 float max_allowed_width = (viewport->WorkSize.x * 0.8f) - 8.0f; // 减去分隔线宽度
                 sidebar_width = std::max(min_sidebar_width, std::min(std::min(max_sidebar_width, max_allowed_width), sidebar_width));
                 
                 // 拖拽时标记视口需要更新
                 layoutState.isViewportDirty = true;
                 
                 // 拖拽时直接同步动画宽度，消除延迟感
                 animated_content_width = sidebar_width;
            }
        }
        
        // 主内容区域已删除，界面只保留左侧边栏
        
        ImGui::End(); // 结束主窗口

        // 📂 底部资产面板
        {
            float asset_panel_x = viewport->WorkPos.x + window_width;
            float asset_panel_y = viewport->WorkPos.y + viewport->WorkSize.y - bottom_panel_height;
            float asset_panel_w = viewport->WorkSize.x - window_width;
            
            // 设置资产面板窗口位置和大小
            ImGui::SetNextWindowPos(ImVec2(asset_panel_x, asset_panel_y));
            ImGui::SetNextWindowSize(ImVec2(asset_panel_w, bottom_panel_height));
            ImGui::SetNextWindowViewport(viewport->ID);
            
            ImGuiWindowFlags asset_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus;
                                          
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
            
            ImGui::Begin("Asset Browser", nullptr, asset_flags);
            ImGui::PopStyleVar(3);
            
            // 📏 顶部调整分隔线 (Invisible button at top)
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::Button("##hsplitter", ImVec2(-1, 8.0f));
            ImGui::PopStyleColor();
            
            // 绘制可视化的分隔线
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetItemRectMin(), 
                ImGui::GetItemRectMax(), 
                ImGui::GetColorU32(ImGui::IsItemHovered() ? ImVec4(0.4f, 0.6f, 0.8f, 0.8f) : ImVec4(0.2f, 0.2f, 0.2f, 0.5f))
            );
            
            if (ImGui::IsItemHovered())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                ImGui::SetTooltip("Drag to resize asset panel");
            }
            if (ImGui::IsItemActive())
            {
                float delta_y = ImGui::GetIO().MouseDelta.y;
                bottom_panel_height -= delta_y;
                bottom_panel_height = std::max(min_bottom_panel_height, std::min(max_bottom_panel_height, bottom_panel_height));
                
                // 标记视口需要更新
                layoutState.isViewportDirty = true;
            }
            
            // 资产内容
            ImGui::SetCursorPosY(10.0f); // Move down past splitter
            
            // 初始化资产浏览器（如果需要）
            static bool assetBrowserInitialized = false;
            if (!assetBrowserInitialized && m_rhi)
            {
                AssetBrowserUI::getInstance().initialize(m_rhi);
                assetBrowserInitialized = true;
            }
            
            // 渲染真实的资产浏览器
            AssetBrowserUI::getInstance().render();
            
            ImGui::End();
        }
    }

    /**
     * @brief 检测UI是否获得焦点
     * 当UI获得焦点时，应该禁用相机视角移动
     * @return true if UI has focus, false otherwise
     */
    bool UIPass::isUIFocused() const
    {
        if (!m_imgui_initialized)
        {
            return false;
        }
        
        ImGuiIO& io = ImGui::GetIO();
        // Check if mouse is over any UI element or if any UI element wants mouse input
        return io.WantCaptureMouse || io.WantCaptureKeyboard || ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered();
    }
    
    /**
     * @brief 保存模型配置到JSON文件
     */
    void UIPass::saveModelConfiguration()
    {
        if (!m_render_resource)
        {
            LOG_ERROR("[UIPass::saveModelConfiguration] Render resource not available");
            return;
        }
        
        const auto& renderObjects = m_render_resource->getLoadedRenderObjects();
        if (renderObjects.empty())
        {
            LOG_WARN("[UIPass::saveModelConfiguration] No render objects to save");
            return;
        }
        
        // 构建JSON配置
        json11::Json::object config;
        json11::Json::array entities;
        
        for (size_t i = 0; i < renderObjects.size(); ++i)
        {
            const auto& obj = renderObjects[i];
            json11::Json::object entity;
            
            entity["name"] = obj.name;
            entity["model_path"] = obj.modelName;
            entity["animation_params"] = obj.animationParams.toJson();
            
            // 添加纹理信息（如果需要）
            json11::Json::array textures;
            // 这里可以根据需要添加纹理路径信息
            entity["textures"] = textures;
            
            entities.push_back(entity);
        }
        
        config["entities"] = entities;
        
        // 将JSON写入文件
        std::string json_string = json11::Json(config).dump();
        std::string config_path = "engine/content/levels/model_config.json";
        
        std::ofstream config_file(config_path);
        if (config_file.is_open())
        {
            config_file << json_string;
            config_file.close();
            // LOG_INFO("[UIPass::saveModelConfiguration] Model configuration saved to: {}", config_path);
        }
        else
        {
            LOG_ERROR("[UIPass::saveModelConfiguration] Failed to open config file: {}", config_path);
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
            // LOG_INFO("[UIPass] ImGui resources cleaned up");
        }
    }

    /**
     * @brief 在子通道中渲染UI内容
     * 专门用于在MainCameraPass的UI子通道中调用
     * @param command_buffer 当前的命令缓冲区
     */
    void UIPass::drawInSubpass(RHICommandBuffer* command_buffer)
    {
        if (!m_imgui_initialized)
        {
            LOG_WARN("[UIPass] ImGui not initialized, skipping UI rendering in subpass");
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
            // 在子通道中使用Vulkan命令缓冲区渲染ImGui
            VkCommandBuffer vk_command_buffer = static_cast<VulkanCommandBuffer*>(command_buffer)->getResource();
            ImGui_ImplVulkan_RenderDrawData(draw_data, vk_command_buffer);
        }
    }

    /**
     * @brief 获取主相机渲染通道实例
     * @return 主相机渲染通道指针，如果获取失败返回nullptr
     */
    MainCameraPass* UIPass::getMainCameraPassInstance() const
    {
        auto render_system = g_runtime_global_context.m_render_system;
        if (!render_system)
        {
            return nullptr;
        }
        
        auto render_pipeline = render_system->getRenderPipeline();
        if (!render_pipeline)
        {
            return nullptr;
        }
        
        auto main_camera_pass = render_pipeline->getMainCameraPass();
        return static_cast<MainCameraPass*>(main_camera_pass.get());
    }
    
    /**
     * @brief 加载光线追踪演示场景
     * 加载包含反射材质和几何体的演示场景
     */
    void UIPass::loadRayTracingDemoScene()
    {
        LOG_INFO("[UIPass] Loading ray tracing demo scene...");
        
        if (!m_render_resource)
        {
            LOG_ERROR("[UIPass] Render resource not available for scene loading");
            return;
        }
        
        try
        {
            // 清除当前场景中的所有对象
            m_render_resource->clearAllRenderObjects();
            
            // 加载演示场景的JSON配置文件
            std::string scene_path = "engine/runtime/content/scenes/raytracing_demo_scene.json";
            std::ifstream scene_file(scene_path);
            
            if (!scene_file.is_open())
            {
                LOG_ERROR("[UIPass] Failed to open ray tracing demo scene file: {}", scene_path);
                return;
            }
            
            // 读取JSON内容
            std::string json_content((std::istreambuf_iterator<char>(scene_file)),
                                   std::istreambuf_iterator<char>());
            scene_file.close();
            
            // 解析JSON
            std::string json_error;
            json11::Json scene_config = json11::Json::parse(json_content, json_error);
            
            if (!json_error.empty())
            {
                LOG_ERROR("[UIPass] Failed to parse scene JSON: {}", json_error);
                return;
            }
            
            // 应用场景配置
            applySceneConfiguration(scene_config);
            
            LOG_INFO("[UIPass] Ray tracing demo scene loaded successfully");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[UIPass] Exception while loading demo scene: {}", e.what());
        }
    }
    
    /**
     * @brief 重置到默认场景
     * 恢复到引擎的默认场景配置
     */
    void UIPass::resetToDefaultScene()
    {
        LOG_INFO("[UIPass] Resetting to default scene...");
        
        if (!m_render_resource)
        {
            LOG_ERROR("[UIPass] Render resource not available for scene reset");
            return;
        }
        
        try
        {
            // 清除当前场景中的所有对象
            m_render_resource->clearAllRenderObjects();
            
            // 重新加载默认模型（如果有的话）
            // 这里可以加载引擎的默认场景配置
            // 例如：单个立方体或球体
            
            // 重置光源到默认设置
            glm::vec3 default_light_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
            glm::vec3 default_light_color = glm::vec3(1.0f, 0.95f, 0.8f);
            float default_light_intensity = 2.0f;
            float default_light_distance = 20.0f;
            
            m_render_resource->updateDirectionalLightDirection(default_light_direction);
            m_render_resource->updateDirectionalLightColor(default_light_color);
            m_render_resource->updateDirectionalLightIntensity(default_light_intensity);
            m_render_resource->updateDirectionalLightDistance(default_light_distance);
            
            LOG_INFO("[UIPass] Default scene restored successfully");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[UIPass] Exception while resetting to default scene: {}", e.what());
        }
    }
    
    /**
     * @brief 应用场景配置
     * 根据JSON配置设置场景参数
     * @param scene_config 场景配置JSON对象
     */
    void UIPass::applySceneConfiguration(const json11::Json& scene_config)
    {
        // 应用光照设置
        if (scene_config["lighting"].is_object())
        {
            const auto& lighting = scene_config["lighting"];
            
            // 设置方向光
            if (lighting["directional_light"].is_object())
            {
                const auto& dir_light = lighting["directional_light"];
                
                if (dir_light["direction"].is_array())
                {
                    const auto& dir_array = dir_light["direction"].array_items();
                    if (dir_array.size() >= 3)
                    {
                        glm::vec3 direction(
                            static_cast<float>(dir_array[0].number_value()),
                            static_cast<float>(dir_array[1].number_value()),
                            static_cast<float>(dir_array[2].number_value())
                        );
                        m_render_resource->updateDirectionalLightDirection(glm::normalize(direction));
                    }
                }
                
                if (dir_light["color"].is_array())
                {
                    const auto& color_array = dir_light["color"].array_items();
                    if (color_array.size() >= 3)
                    {
                        glm::vec3 color(
                            static_cast<float>(color_array[0].number_value()),
                            static_cast<float>(color_array[1].number_value()),
                            static_cast<float>(color_array[2].number_value())
                        );
                        m_render_resource->updateDirectionalLightColor(color);
                    }
                }
                
                if (dir_light["intensity"].is_number())
                {
                    float intensity = static_cast<float>(dir_light["intensity"].number_value());
                    m_render_resource->updateDirectionalLightIntensity(intensity);
                }
            }
        }
        
        // 应用场景对象
        if (scene_config["objects"].is_array())
        {
            for (const auto& obj_json : scene_config["objects"].array_items())
            {
                std::string name = obj_json["name"].string_value();
                std::string type = obj_json["type"].string_value();
                std::string model_path = obj_json["model_path"].string_value();
                if (model_path.empty())
                {
                    // 尝试兼容 levels1.json 的 model_paths 字段
                    model_path = obj_json["model_paths"].string_value();
                }
                
                // 如果没有指定路径，尝试根据类型推断
                if (model_path.empty() && !type.empty())
                {
                    model_path = "engine/runtime/content/models/" + type + ".obj";
                }
                
                // 如果还是空的，跳过
                if (model_path.empty()) 
                {
                    LOG_WARN("[UIPass] Object '{}' missing model path or type", name);
                    continue;
                }

                // 纹理处理
                std::vector<std::string> texture_paths;
                if (obj_json["textures"].is_array())
                {
                    for (const auto& tex : obj_json["textures"].array_items())
                    {
                        texture_paths.push_back(tex.string_value());
                    }
                }
                // 兼容 levels1.json 的格式 (model_texture_map)
                else if (obj_json["model_texture_map"].is_array())
                {
                    for (const auto& tex : obj_json["model_texture_map"].array_items())
                    {
                        texture_paths.push_back(tex.string_value());
                    }
                }

                RenderObject renderObject;
                renderObject.name = name;
                
                // 加载资源
                // 注意：createRenderObjectResource 需要实现具体的加载逻辑
                if (m_render_resource->createRenderObjectResource(renderObject, model_path, texture_paths))
                {
                    // 应用变换
                    if (obj_json["transform"].is_object())
                    {
                        const auto& transform = obj_json["transform"];
                        
                        // Position
                        if (transform["position"].is_array())
                        {
                            auto pos = transform["position"].array_items();
                            if (pos.size() >= 3)
                            {
                                renderObject.animationParams.position = glm::vec3(
                                    static_cast<float>(pos[0].number_value()), 
                                    static_cast<float>(pos[1].number_value()), 
                                    static_cast<float>(pos[2].number_value())
                                );
                            }
                        }
                        
                        // Rotation
                        if (transform["rotation"].is_array())
                        {
                            auto rot = transform["rotation"].array_items();
                            if (rot.size() >= 3)
                            {
                                // 假设 JSON 中的旋转是角度制，转换为弧度
                                renderObject.animationParams.rotation = glm::vec3(
                                    glm::radians(static_cast<float>(rot[0].number_value())), 
                                    glm::radians(static_cast<float>(rot[1].number_value())), 
                                    glm::radians(static_cast<float>(rot[2].number_value()))
                                );
                            }
                        }
                        
                        // Scale
                        if (transform["scale"].is_array())
                        {
                            auto scale = transform["scale"].array_items();
                            if (scale.size() >= 3)
                            {
                                renderObject.animationParams.scale = glm::vec3(
                                    static_cast<float>(scale[0].number_value()), 
                                    static_cast<float>(scale[1].number_value()), 
                                    static_cast<float>(scale[2].number_value())
                                );
                            }
                        }
                    }
                    
                    // 将对象添加到资源管理器
                    m_render_resource->addRenderObject(renderObject);
                    LOG_INFO("[UIPass] Loaded object: {}", name);
                }
                else
                {
                    LOG_ERROR("[UIPass] Failed to load model: {}", model_path);
                }
            }
        }
        
        LOG_INFO("[UIPass] Scene lighting and objects configuration applied");
    }

} // namespace Elish