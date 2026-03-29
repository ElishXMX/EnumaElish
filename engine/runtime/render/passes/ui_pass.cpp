#include "ui_pass.h"
#include "../interface/vulkan/vulkan_rhi.h"
#include "../interface/vulkan/vulkan_rhi_resource.h"
#include "../../core/base/macro.h"
#include "../../core/log/log_system.h"
#include "../../core/asset/asset_manager.h"
#include "../render_pipeline.h"
#include "../render_system.h"
#include "../window_system.h"
#include "../../global/global_context.h"
#include "../../physics/physics_scene.h"
#include "main_camera_pass.h"

// ImGui 相关头文件
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include "../../ui/asset_browser_ui.h"
#include <algorithm>

#include <stdexcept>
#include <fstream>
#include "../../3rdparty/json11/json11.hpp"

namespace Elish
{
    UIPass::UIPass()
    {
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
        setupImGuiContext();
        initializeImGuiGLFW();
        initializeImGuiVulkan();
    }

    /**
     * @brief 准备UI渲染通道的数据
     * @param render_resource 渲染资源管理器
     */
    void UIPass::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        m_render_resource = render_resource;
    }

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

        if (!command_buffer) {
            LOG_ERROR("[UIPass] Invalid command buffer provided to UI draw");
            return;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUIContent();
        renderColliderDebugLines();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        
        if (draw_data && draw_data->CmdListsCount > 0 && draw_data->TotalVtxCount > 0)
        {
            VkCommandBuffer vk_command_buffer = static_cast<VulkanCommandBuffer*>(command_buffer)->getResource();
            ImGui_ImplVulkan_RenderDrawData(draw_data, vk_command_buffer);
        }
    }

    /**
     * @brief 设置ImGui上下文和样式
     */
    void UIPass::setupImGuiContext()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        
        ImGui::StyleColorsDark();

        const char* font_path = "C:\\Windows\\Fonts\\msyh.ttc";
        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        ImFont* default_font = io.Fonts->AddFontDefault(&font_cfg);
        if (!default_font) {
            LOG_ERROR("[UIPass] Failed to add default font.");
        }
        
        ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 30.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
        if (!font)
        {
            LOG_ERROR("[UIPass] Failed to load Chinese font: %s. Using default font.", font_path);
        }
    }

    /**
     * @brief 初始化ImGui的GLFW后端
     */
    void UIPass::initializeImGuiGLFW()
    {
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
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vulkan_rhi->m_instance;
        init_info.PhysicalDevice = vulkan_rhi->m_physical_device;
        init_info.Device = vulkan_rhi->m_device;
        init_info.QueueFamily = vulkan_rhi->getQueueFamilyIndices().graphics_family.value();
        init_info.Queue = static_cast<VulkanQueue*>(vulkan_rhi->getGraphicsQueue())->getResource();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = static_cast<VulkanDescriptorPool*>(vulkan_rhi->getDescriptorPoor())->getResource();
        init_info.Subpass = 1;
        init_info.MinImageCount = vulkan_rhi->getMaxFramesInFlight();
        init_info.ImageCount = vulkan_rhi->getMaxFramesInFlight();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = checkVkResult;

        RHIRenderPass* main_render_pass = getMainCameraRenderPass();
        if (!main_render_pass)
        {
            throw std::runtime_error("[UIPass] Main camera render pass not available");
        }

        if (!ImGui_ImplVulkan_Init(&init_info, static_cast<VulkanRenderPass*>(main_render_pass)->getResource()))
        {
            throw std::runtime_error("[UIPass] Failed to initialize ImGui Vulkan backend");
        }

        uploadFonts(vulkan_rhi);
        
        m_imgui_initialized = true;
    }

    /**
     * @brief 上传ImGui字体纹理到GPU
     * @param vulkan_rhi Vulkan RHI实例
     */
    void UIPass::uploadFonts(VulkanRHI* vulkan_rhi)
    {
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
        
        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
        
        vkEndCommandBuffer(command_buffer);
        
        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        
        VkQueue graphics_queue = static_cast<VulkanQueue*>(vulkan_rhi->getGraphicsQueue())->getResource();
        vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);
        
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
        
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    /**
     * @brief 渲染UI内容
     * @details 实现三栏布局：左侧菜单栏、底部资产栏、右侧属性面板
     * 输出：渲染完整的编辑器UI界面
     */
    void UIPass::renderUIContent()
    {
        if (!m_render_resource) {
            LOG_WARN("[UIPass] Render resource is null, UI content may be limited");
        }

        auto render_system = g_runtime_global_context.m_render_system;
        std::shared_ptr<RenderPipeline> render_pipeline;
        if (render_system) {
            render_pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_system->getRenderPipeline());
        }

        if (!render_pipeline) {
             LOG_ERROR("[UIPass] RenderPipeline not available");
             return;
        }

        auto& layoutState = render_pipeline->getEditorLayoutState();
        
        // Check if swapchain was recreated (window resized)
        if (m_rhi && m_rhi->isSwapchainRecreated())
        {
            layoutState.isViewportDirty = true;
            m_rhi->resetSwapchainRecreatedFlag();
        }
        
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        if (!viewport) {
            LOG_ERROR("[UIPass] ImGui viewport is null, cannot render UI");
            return;
        }
        
        float& left_sidebar_width = layoutState.leftSidebarWidth;
        float& right_sidebar_width = layoutState.rightSidebarWidth;
        float& bottom_panel_height = layoutState.bottomPanelHeight;
        bool& left_sidebar_collapsed = layoutState.isLeftSidebarCollapsed;
        bool& right_sidebar_collapsed = layoutState.isRightSidebarCollapsed;
        
        const float min_sidebar_width = 200.0f;
        const float max_sidebar_width = 500.0f;
        const float min_bottom_panel_height = 150.0f;
        const float max_bottom_panel_height = std::max(min_bottom_panel_height, viewport->WorkSize.y * 0.6f);

        static float animated_left_width = left_sidebar_width;
        static float animated_right_width = right_sidebar_width;
        
        float target_left_width = left_sidebar_collapsed ? 0.0f : left_sidebar_width;
        float target_right_width = right_sidebar_collapsed ? 0.0f : right_sidebar_width;
        
        float animation_speed = 15.0f * ImGui::GetIO().DeltaTime;
        
        if (std::abs(animated_left_width - target_left_width) > 0.5f) {
            animated_left_width += (target_left_width - animated_left_width) * animation_speed;
            layoutState.isViewportDirty = true;
        } else {
            animated_left_width = target_left_width;
        }
        
        if (std::abs(animated_right_width - target_right_width) > 0.5f) {
            animated_right_width += (target_right_width - animated_right_width) * animation_speed;
            layoutState.isViewportDirty = true;
        } else {
            animated_right_width = target_right_width;
        }

        float left_window_width = 30.0f + animated_left_width + ((animated_left_width > 1.0f) ? 8.0f : 0.0f);
        float right_window_width = animated_right_width + ((animated_right_width > 1.0f) ? 8.0f : 0.0f);
        
        float total_ui_width = left_window_width + right_window_width;
        float max_ui_width = viewport->WorkSize.x * 0.9f;
        if (total_ui_width > max_ui_width) {
            float scale = max_ui_width / total_ui_width;
            left_window_width *= scale;
            right_window_width *= scale;
        }

        layoutState.sceneViewport.x = left_window_width;
        layoutState.sceneViewport.y = 0.0f;
        layoutState.sceneViewport.width = viewport->WorkSize.x - left_window_width - right_window_width;
        layoutState.sceneViewport.height = viewport->WorkSize.y - bottom_panel_height;
        layoutState.isViewportDirty = true;

        // === Render Left Sidebar ===
        renderLeftSidebar(viewport, layoutState, animated_left_width, left_sidebar_width, 
                          min_sidebar_width, max_sidebar_width, left_sidebar_collapsed);
        
        // === Render Right Property Panel ===
        renderRightPropertyPanel(viewport, layoutState, animated_right_width, right_sidebar_width,
                                 min_sidebar_width, max_sidebar_width, right_sidebar_collapsed);
        
        // === Render Bottom Asset Panel ===
        renderBottomAssetPanel(viewport, layoutState, bottom_panel_height, 
                               min_bottom_panel_height, max_bottom_panel_height, left_window_width, right_window_width);
    }

    /**
     * @brief Render left sidebar with scene hierarchy and controls
     */
    void UIPass::renderLeftSidebar(ImGuiViewport* viewport, 
                                    RenderPipeline::EditorLayoutState& layoutState,
                                    float animated_width, float& sidebar_width,
                                    float min_width, float max_width, bool& collapsed)
    {
        float window_width = 30.0f + animated_width + ((animated_width > 1.0f) ? 8.0f : 0.0f);
        
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(ImVec2(window_width, viewport->WorkSize.y));
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("Left Panel", nullptr, window_flags);
        ImGui::PopStyleVar(3);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.7f, 1.0f));
        
        const char* button_text = collapsed ? ">" : "<";
        const char* tooltip_text = collapsed ? "Expand Panel" : "Collapse Panel";
        
        if (ImGui::Button(button_text, ImVec2(30, 40)))
        {
            collapsed = !collapsed;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip_text);
        }
        
        ImGui::PopStyleColor(3);
        
        if (animated_width > 1.0f)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::BeginChild("LeftSidebar", ImVec2(animated_width, 0), true, ImGuiWindowFlags_NoScrollbar);
            
            ImGui::TextDisabled("Main Control Panel");
            ImGui::Spacing();
            
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
            
            // Scene Hierarchy
            static bool show_hierarchy = true;
            if (ImGui::CollapsingHeader("Scene Hierarchy", &show_hierarchy, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
                
                if (m_render_resource && !m_render_resource->getLoadedRenderObjects().empty())
                {
                    auto& renderObjects = m_render_resource->getLoadedRenderObjects();
                    
                    ImGui::Text("Scene Objects:");
                    if (ImGui::BeginListBox("##SceneHierarchyList", ImVec2(-1, 120)))
                    {
                        for (size_t i = 0; i < renderObjects.size(); ++i)
                        {
                            bool is_selected = (layoutState.selectedObjectIndex == static_cast<int>(i));
                            if (ImGui::Selectable(renderObjects[i].name.c_str(), is_selected))
                            {
                                layoutState.selectedObjectIndex = static_cast<int>(i);
                            }
                            if (is_selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndListBox();
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No objects in scene");
                }
                ImGui::Unindent(10.0f);
            }
            
            ImGui::Spacing();
            
            // Render Settings
            static bool show_render_settings = true;
            if (ImGui::CollapsingHeader("Render Settings", &show_render_settings, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
                
                MainCameraPass* main_camera_pass = getMainCameraPassInstance();
                if (main_camera_pass)
                {
                    bool enable_background = main_camera_pass->isBackgroundEnabled();
                    if (ImGui::Checkbox("Enable Background", &enable_background))
                    {
                        main_camera_pass->setBackgroundEnabled(enable_background);
                    }
                    
                    bool enable_skybox = main_camera_pass->isSkyboxEnabled();
                    if (ImGui::Checkbox("Enable Skybox", &enable_skybox))
                    {
                        main_camera_pass->setSkyboxEnabled(enable_skybox);
                    }

                    ImGui::Spacing();
                    ImGui::Checkbox("Show Colliders", &m_show_colliders);
                    
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Background: IBL Environment");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Skybox: 3D Environment Cube");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Main camera pass not available");
                }
                
                ImGui::Unindent(10.0f);
            }
            
            ImGui::Spacing();
            
            // Light Control
            static bool show_light_control = true;
            if (ImGui::CollapsingHeader("Light Control", &show_light_control, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
                
                if (m_render_resource)
                {
                    const DirectionalLightData* primary_light = m_render_resource->getPrimaryDirectionalLight();
                    if (primary_light)
                    {
                        static glm::vec3 light_direction = primary_light->direction;
                        
                        ImGui::Text("Directional Light:");
                        ImGui::Spacing();
                        
                        ImGui::Text("Direction:");
                        float direction[3] = { light_direction.x, light_direction.y, light_direction.z };
                        ImGui::PushItemWidth(-1);
                        if (ImGui::DragFloat3("##LightDirection", direction, 0.01f, -1.0f, 1.0f, "%.2f"))
                        {
                            light_direction = glm::vec3(direction[0], direction[1], direction[2]);
                            if (glm::length(light_direction) > 0.001f)
                            {
                                light_direction = glm::normalize(light_direction);
                                m_render_resource->updateDirectionalLightDirection(light_direction);
                            }
                        }
                        ImGui::PopItemWidth();
                        
                        ImGui::Spacing();
                        
                        static float light_intensity = primary_light->intensity;
                        ImGui::Text("Intensity:");
                        if (ImGui::SliderFloat("##LightIntensity", &light_intensity, 0.0f, 5.0f, "%.2f"))
                        {
                            m_render_resource->updateDirectionalLightIntensity(light_intensity);
                        }
                        
                        ImGui::Spacing();
                        
                        static glm::vec3 light_color = primary_light->color;
                        ImGui::Text("Color:");
                        float color[3] = { light_color.r, light_color.g, light_color.b };
                        if (ImGui::ColorEdit3("##LightColor", color))
                        {
                            light_color = glm::vec3(color[0], color[1], color[2]);
                            m_render_resource->updateDirectionalLightColor(light_color);
                        }
                        
                        ImGui::Spacing();
                        
                        ImGui::Text("Presets:");
                        if (ImGui::Button("Top", ImVec2(50, 0)))
                        {
                            light_direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
                            m_render_resource->updateDirectionalLightDirection(light_direction);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Side", ImVec2(50, 0)))
                        {
                            light_direction = glm::normalize(glm::vec3(-1.0f, -0.5f, 0.0f));
                            m_render_resource->updateDirectionalLightDirection(light_direction);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Diagonal", ImVec2(60, 0)))
                        {
                            light_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
                            m_render_resource->updateDirectionalLightDirection(light_direction);
                        }
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "No directional light");
                    }
                }
                
                ImGui::Unindent(10.0f);
            }
            
            ImGui::EndChild();
            
            // Resize splitter
            ImGui::SetCursorPos(ImVec2(30.0f + animated_width, 0.0f));
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            
            ImGui::Button("##left_splitter", ImVec2(8.0f, -1));
            
            ImGui::PopStyleColor(3);
            
            bool is_hovered = ImGui::IsItemHovered();
            bool is_active = ImGui::IsItemActive();
            
            ImU32 color;
            if (is_active) color = ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
            else if (is_hovered) color = ImGui::GetColorU32(ImVec4(0.4f, 0.6f, 0.8f, 0.8f));
            else color = ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
            
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetItemRectMin(), 
                ImGui::GetItemRectMax(), 
                color
            );

            if (is_hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SetTooltip("Drag to resize");
            }
            
            if (is_active)
            {
                float delta_x = ImGui::GetIO().MouseDelta.x;
                sidebar_width += delta_x;
                
                float max_allowed_width = (viewport->WorkSize.x * 0.4f) - 8.0f;
                sidebar_width = std::max(min_width, std::min(std::min(max_width, max_allowed_width), sidebar_width));
                
                layoutState.isViewportDirty = true;
            }
        }
        
        ImGui::End();
    }

    /**
     * @brief Render right property panel with object inspector
     */
    void UIPass::renderRightPropertyPanel(ImGuiViewport* viewport,
                                          RenderPipeline::EditorLayoutState& layoutState,
                                          float animated_width, float& sidebar_width,
                                          float min_width, float max_width, bool& collapsed)
    {
        if (animated_width < 1.0f)
        {
            return;
        }
        
        float panel_x = viewport->WorkPos.x + viewport->WorkSize.x - animated_width;
        float splitter_width = 8.0f;
        
        ImGui::SetNextWindowPos(ImVec2(panel_x - splitter_width, viewport->WorkPos.y));
        ImGui::SetNextWindowSize(ImVec2(animated_width + splitter_width, viewport->WorkSize.y - layoutState.bottomPanelHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("Property Panel", nullptr, window_flags);
        ImGui::PopStyleVar(3);
        
        // Resize splitter
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        
        ImGui::Button("##right_splitter", ImVec2(8.0f, -1));
        
        ImGui::PopStyleColor(3);
        
        bool is_hovered = ImGui::IsItemHovered();
        bool is_active = ImGui::IsItemActive();
        
        ImU32 color;
        if (is_active) color = ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
        else if (is_hovered) color = ImGui::GetColorU32(ImVec4(0.4f, 0.6f, 0.8f, 0.8f));
        else color = ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
        
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(), 
            ImGui::GetItemRectMax(), 
            color
        );
        
        if (is_hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::SetTooltip("Drag to resize");
        }
        
        if (is_active)
        {
            float delta_x = ImGui::GetIO().MouseDelta.x;
            sidebar_width -= delta_x;
            
            float max_allowed_width = (viewport->WorkSize.x * 0.4f) - 8.0f;
            sidebar_width = std::max(min_width, std::min(std::min(max_width, max_allowed_width), sidebar_width));
            
            layoutState.isViewportDirty = true;
        }
        
        ImGui::SameLine();
        
        ImGui::BeginChild("PropertyContent", ImVec2(animated_width - 5.0f, 0), true);
        
        ImGui::TextDisabled("Property Inspector");
        ImGui::Separator();
        ImGui::Spacing();
        
        // Object Properties
        ImGui::TextDisabled("Object Properties");
        ImGui::Separator();
        ImGui::Spacing();
        
        if (!m_render_resource || layoutState.selectedObjectIndex < 0)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No object selected");
        }
        else
        {
            auto& renderObjects = m_render_resource->getLoadedRenderObjects();
            if (layoutState.selectedObjectIndex >= static_cast<int>(renderObjects.size()))
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Selected object does not exist");
            }
            else
            {
                auto& selectedObject = renderObjects[layoutState.selectedObjectIndex];
                auto& animParams = selectedObject.animationParams;
                
                ImGui::Text("Name: %s", selectedObject.name.c_str());
                ImGui::Spacing();
                
                ImGui::Text("Position");
                float position[3] = { animParams.position.x, animParams.position.y, animParams.position.z };
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat3("##Position", position, 0.1f))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.position = glm::vec3(position[0], position[1], position[2]);
                    m_render_resource->updateRenderObjectAnimationParams(layoutState.selectedObjectIndex, updatedParams);
                }
                ImGui::PopItemWidth();
                
                ImGui::Spacing();
                
                ImGui::Text("Rotation (Degrees)");
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
                    m_render_resource->updateRenderObjectAnimationParams(layoutState.selectedObjectIndex, updatedParams);
                }
                ImGui::PopItemWidth();
                
                ImGui::Spacing();
                
                ImGui::Text("Scale");
                float scale[3] = { animParams.scale.x, animParams.scale.y, animParams.scale.z };
                ImGui::PushItemWidth(-1);
                if (ImGui::DragFloat3("##Scale", scale, 0.01f, 0.01f, 10.0f))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.scale = glm::vec3(scale[0], scale[1], scale[2]);
                    m_render_resource->updateRenderObjectAnimationParams(layoutState.selectedObjectIndex, updatedParams);
                }
                ImGui::PopItemWidth();
                
                ImGui::Spacing();
                
                bool enableAnimation = animParams.enableAnimation;
                if (ImGui::Checkbox("Auto Rotate", &enableAnimation))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.enableAnimation = enableAnimation;
                    if (enableAnimation) {
                        updatedParams.rotationSpeed = 1.0f;
                        updatedParams.rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                    m_render_resource->updateRenderObjectAnimationParams(layoutState.selectedObjectIndex, updatedParams);
                }
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Asset Binding
        ImGui::TextDisabled("Asset Binding");
        ImGui::Separator();
        ImGui::Spacing();
        
        if (!m_render_resource || layoutState.selectedObjectIndex < 0)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No object selected");
        }
        else
        {
            auto& renderObjects = m_render_resource->getLoadedRenderObjects();
            if (layoutState.selectedObjectIndex < static_cast<int>(renderObjects.size()))
            {
                auto& selectedObject = renderObjects[layoutState.selectedObjectIndex];
                
                ImGui::Text("Model Asset:");
                ImGui::Indent(10.0f);
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", selectedObject.modelName.c_str());
                ImGui::Unindent(10.0f);
                
                ImGui::Spacing();
                
                ImGui::Text("Texture Assets:");
                ImGui::Indent(10.0f);
                if (!selectedObject.textureImages.empty())
                {
                    for (size_t i = 0; i < selectedObject.textureImages.size(); ++i)
                    {
                        ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.4f, 1.0f), "Texture_%zu", i);
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No textures bound");
                }
                ImGui::Unindent(10.0f);
            }
        }
        
        ImGui::EndChild();
        
        ImGui::End();
    }

    /**
     * @brief Render bottom asset panel
     */
    void UIPass::renderBottomAssetPanel(ImGuiViewport* viewport,
                                        RenderPipeline::EditorLayoutState& layoutState,
                                        float& panel_height, float min_height, float max_height,
                                        float left_width, float right_width)
    {
        float panel_x = viewport->WorkPos.x + left_width;
        float panel_y = viewport->WorkPos.y + viewport->WorkSize.y - panel_height;
        float panel_w = viewport->WorkSize.x - left_width - right_width;
        
        ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
        ImGui::SetNextWindowSize(ImVec2(panel_w, panel_height));
        ImGui::SetNextWindowViewport(viewport->ID);
        
        ImGuiWindowFlags asset_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus;
                                      
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        
        ImGui::Begin("Asset Browser", nullptr, asset_flags);
        ImGui::PopStyleVar(3);
        
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::Button("##hsplitter", ImVec2(-1, 8.0f));
        ImGui::PopStyleColor();
        
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(), 
            ImGui::GetItemRectMax(), 
            ImGui::GetColorU32(ImGui::IsItemHovered() ? ImVec4(0.4f, 0.6f, 0.8f, 0.8f) : ImVec4(0.2f, 0.2f, 0.2f, 0.5f))
        );
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ImGui::SetTooltip("Drag to resize");
        }
        if (ImGui::IsItemActive())
        {
            float delta_y = ImGui::GetIO().MouseDelta.y;
            panel_height -= delta_y;
            panel_height = std::max(min_height, std::min(max_height, panel_height));
            
            layoutState.isViewportDirty = true;
        }
        
        ImGui::SetCursorPosY(10.0f);
        
        static bool assetBrowserInitialized = false;
        if (!assetBrowserInitialized && m_rhi)
        {
            AssetBrowserUI::getInstance().initialize(m_rhi);
            assetBrowserInitialized = true;
        }
        
        AssetBrowserUI::getInstance().render();
        
        ImGui::End();
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
        
        json11::Json::object config;
        json11::Json::array entities;
        
        for (size_t i = 0; i < renderObjects.size(); ++i)
        {
            const auto& obj = renderObjects[i];
            json11::Json::object entity;
            
            entity["name"] = obj.name;
            entity["model_path"] = obj.modelName;
            entity["animation_params"] = obj.animationParams.toJson();
            
            json11::Json::array textures;
            entity["textures"] = textures;
            
            entities.push_back(entity);
        }
        
        config["entities"] = entities;
        
        std::string json_string = json11::Json(config).dump();
        std::string config_path = "engine/content/levels/model_config.json";
        
        std::ofstream config_file(config_path);
        if (config_file.is_open())
        {
            config_file << json_string;
            config_file.close();
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
        auto render_system = g_runtime_global_context.m_render_system;
        if (!render_system)
        {
            LOG_ERROR("[UIPass] Render system not available");
            return nullptr;
        }
        
        auto render_pipeline = render_system->getRenderPipeline();
        if (!render_pipeline)
        {
            LOG_ERROR("[UIPass] Render pipeline not available");
            return nullptr;
        }
        
        auto main_camera_pass = render_pipeline->getMainCameraPass();
        if (!main_camera_pass)
        {
            LOG_ERROR("[UIPass] Main camera pass not available");
            return nullptr;
        }
        
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
            if (m_rhi)
            {
                VulkanRHI* vulkan_rhi = static_cast<VulkanRHI*>(m_rhi.get());
                vkDeviceWaitIdle(vulkan_rhi->m_device);
            }
            
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            
            m_imgui_initialized = false;
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

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUIContent();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        
        if (draw_data && draw_data->CmdListsCount > 0)
        {
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
            m_render_resource->clearAllRenderObjects();
            
            std::string scene_path = "engine/runtime/content/scenes/raytracing_demo_scene.json";
            std::ifstream scene_file(scene_path);
            
            if (!scene_file.is_open())
            {
                LOG_ERROR("[UIPass] Failed to open ray tracing demo scene file: {}", scene_path);
                return;
            }
            
            std::string json_content((std::istreambuf_iterator<char>(scene_file)),
                                   std::istreambuf_iterator<char>());
            scene_file.close();
            
            std::string json_error;
            json11::Json scene_config = json11::Json::parse(json_content, json_error);
            
            if (!json_error.empty())
            {
                LOG_ERROR("[UIPass] Failed to parse scene JSON: {}", json_error);
                return;
            }
            
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
            m_render_resource->clearAllRenderObjects();
            
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
        if (scene_config["lighting"].is_object())
        {
            const auto& lighting = scene_config["lighting"];
            
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
        
        if (scene_config["objects"].is_array())
        {
            for (const auto& obj_json : scene_config["objects"].array_items())
            {
                std::string name = obj_json["name"].string_value();
                std::string type = obj_json["type"].string_value();
                std::string model_path = obj_json["model_path"].string_value();
                if (model_path.empty())
                {
                    model_path = obj_json["model_paths"].string_value();
                }
                
                if (model_path.empty() && !type.empty())
                {
                    model_path = "engine/runtime/content/models/" + type + ".obj";
                }
                
                if (model_path.empty()) 
                {
                    LOG_WARN("[UIPass] Object '{}' missing model path or type", name);
                    continue;
                }

                std::vector<std::string> texture_paths;
                if (obj_json["textures"].is_array())
                {
                    for (const auto& tex : obj_json["textures"].array_items())
                    {
                        texture_paths.push_back(tex.string_value());
                    }
                }
                else if (obj_json["model_texture_map"].is_array())
                {
                    for (const auto& tex : obj_json["model_texture_map"].array_items())
                    {
                        texture_paths.push_back(tex.string_value());
                    }
                }

                RenderObject renderObject;
                renderObject.name = name;
                
                if (m_render_resource->createRenderObjectResource(renderObject, model_path, texture_paths))
                {
                    if (obj_json["transform"].is_object())
                    {
                        const auto& transform = obj_json["transform"];
                        
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
                        
                        if (transform["rotation"].is_array())
                        {
                            auto rot = transform["rotation"].array_items();
                            if (rot.size() >= 3)
                            {
                                renderObject.animationParams.rotation = glm::vec3(
                                    glm::radians(static_cast<float>(rot[0].number_value())), 
                                    glm::radians(static_cast<float>(rot[1].number_value())), 
                                    glm::radians(static_cast<float>(rot[2].number_value()))
                                );
                            }
                        }
                        
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

    void UIPass::renderColliderDebugLines()
    {
        if (!m_show_colliders)
        {
            return;
        }

        auto physics_scene = g_runtime_global_context.m_physics_scene;
        if (!physics_scene)
        {
            return;
        }

        auto render_system = g_runtime_global_context.m_render_system;
        if (!render_system)
        {
            return;
        }

        auto pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_system->getRenderPipeline());
        if (!pipeline)
        {
            return;
        }

        const auto& layout_state = pipeline->getEditorLayoutState();
        
        ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        float work_pos_x = main_viewport->WorkPos.x;
        float work_pos_y = main_viewport->WorkPos.y;
        
        float viewport_x = work_pos_x + layout_state.sceneViewport.x;
        float viewport_y = work_pos_y + layout_state.sceneViewport.y;
        float viewport_width = layout_state.sceneViewport.width;
        float viewport_height = layout_state.sceneViewport.height;

        if (viewport_width <= 1.0f || viewport_height <= 1.0f)
        {
            return;
        }

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        if (!draw_list)
        {
            return;
        }

        auto main_camera_pass = getMainCameraPassInstance();
        if (!main_camera_pass || !main_camera_pass->m_camera)
        {
            return;
        }

        const RenderCamera& camera = *main_camera_pass->m_camera;
        glm::mat4 view_matrix = camera.getViewMatrix();
        glm::mat4 proj_matrix = camera.getPersProjMatrix();

        static int collider_log_counter = 0;
        collider_log_counter++;
        if (collider_log_counter % 30 == 0) {
            LOG_INFO("[Collider] Matrices:");
            LOG_INFO("  view[0]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     view_matrix[0][0], view_matrix[0][1], view_matrix[0][2], view_matrix[0][3]);
            LOG_INFO("  view[1]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     view_matrix[1][0], view_matrix[1][1], view_matrix[1][2], view_matrix[1][3]);
            LOG_INFO("  view[2]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     view_matrix[2][0], view_matrix[2][1], view_matrix[2][2], view_matrix[2][3]);
            LOG_INFO("  view[3]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     view_matrix[3][0], view_matrix[3][1], view_matrix[3][2], view_matrix[3][3]);
            LOG_INFO("  proj[0]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     proj_matrix[0][0], proj_matrix[0][1], proj_matrix[0][2], proj_matrix[0][3]);
            LOG_INFO("  proj[1]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     proj_matrix[1][0], proj_matrix[1][1], proj_matrix[1][2], proj_matrix[1][3]);
            LOG_INFO("  proj[2]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     proj_matrix[2][0], proj_matrix[2][1], proj_matrix[2][2], proj_matrix[2][3]);
            LOG_INFO("  proj[3]: ({:.3f},{:.3f},{:.3f},{:.3f})", 
                     proj_matrix[3][0], proj_matrix[3][1], proj_matrix[3][2], proj_matrix[3][3]);
        }

        g_runtime_global_context.syncCollidersWithRenderObjects();

#ifdef JPH_DEBUG_RENDERER
        physics_scene->drawPhysicsBodies(view_matrix, proj_matrix, viewport_width, viewport_height);

        auto debug_renderer = physics_scene->getDebugRenderer();
        if (debug_renderer)
        {
            const auto& debug_lines = debug_renderer->getDebugLines();
            
            for (const auto& line : debug_lines)
            {
                glm::vec4 start_clip = proj_matrix * view_matrix * glm::vec4(line.start, 1.0f);
                glm::vec4 end_clip = proj_matrix * view_matrix * glm::vec4(line.end, 1.0f);
                
                if (start_clip.w <= 0.001f || end_clip.w <= 0.001f)
                {
                    continue;
                }
                
                float start_ndc_x = start_clip.x / start_clip.w;
                float start_ndc_y = start_clip.y / start_clip.w;
                float end_ndc_x = end_clip.x / end_clip.w;
                float end_ndc_y = end_clip.y / end_clip.w;
                
                float start_screen_x = viewport_x + (start_ndc_x + 1.0f) * 0.5f * viewport_width;
                float start_screen_y = viewport_y + (start_ndc_y + 1.0f) * 0.5f * viewport_height;
                float end_screen_x = viewport_x + (end_ndc_x + 1.0f) * 0.5f * viewport_width;
                float end_screen_y = viewport_y + (end_ndc_y + 1.0f) * 0.5f * viewport_height;
                
                draw_list->AddLine(
                    ImVec2(start_screen_x, start_screen_y),
                    ImVec2(end_screen_x, end_screen_y),
                    line.color,
                    2.0f
                );
            }
        }
#endif
    }

} // namespace Elish