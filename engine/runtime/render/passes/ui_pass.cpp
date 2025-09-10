#include "ui_pass.h"
#include "../interface/vulkan/vulkan_rhi.h"
#include "../interface/vulkan/vulkan_rhi_resource.h"
#include "../../core/base/macro.h"
#include "../../core/log/log_system.h"
#include "../../global/global_context.h"
#include "../window_system.h"
#include "../render_pipeline_base.h"
#include "../render_system.h"

// ImGui 相关头文件
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

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
    void UIPass::draw(RHICommandBuffer* command_buffer)
    {
        if (!m_imgui_initialized)
        {
            
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
        ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 16.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
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
        // 性能信息窗口（简化版）
        ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (m_render_resource)
        {
            ImGui::Text("Models: %zu", m_render_resource->getLoadedRenderObjects().size());
        }
        ImGui::End();
        
        // Model Transform Control Window
        ImGui::Begin("Transform Control");
        
        if (m_render_resource && !m_render_resource->getLoadedRenderObjects().empty())
        {
            auto& renderObjects = m_render_resource->getLoadedRenderObjects();
            static int selected_model = 0;
            
            // Model selection combo box
            ImGui::PushItemWidth(-1);
            if (ImGui::BeginCombo("##Model", selected_model < renderObjects.size() ? renderObjects[selected_model].name.c_str() : "Select Model"))
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
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            
            if (selected_model >= 0 && selected_model < static_cast<int>(renderObjects.size()))
            {
                auto& selectedObject = renderObjects[selected_model];
                auto& animParams = selectedObject.animationParams;
                
                ImGui::Spacing();
                
                // Position controls
                ImGui::AlignTextToFramePadding();
                ImGui::Text("P:"); ImGui::SameLine();
                float position[3] = { animParams.position.x, animParams.position.y, animParams.position.z };
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##Position", position, 0.1f, -100.0f, 100.0f, "%.1f"))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.position = glm::vec3(position[0], position[1], position[2]);
                    m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                }
                
                // Rotation controls
                ImGui::AlignTextToFramePadding();
                ImGui::Text("R:"); ImGui::SameLine();
                float rotation[3] = { 
                    glm::degrees(animParams.rotation.x), 
                    glm::degrees(animParams.rotation.y), 
                    glm::degrees(animParams.rotation.z) 
                };
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##Rotation", rotation, 1.0f, -180.0f, 180.0f, "%.0f°"))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.rotation = glm::vec3(
                        glm::radians(rotation[0]), 
                        glm::radians(rotation[1]), 
                        glm::radians(rotation[2])
                    );
                    m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                }
                
                // Scale controls
                ImGui::AlignTextToFramePadding();
                ImGui::Text("S:"); ImGui::SameLine();
                float scale[3] = { animParams.scale.x, animParams.scale.y, animParams.scale.z };
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##Scale", scale, 0.01f, 0.01f, 10.0f, "%.2f"))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.scale = glm::vec3(scale[0], scale[1], scale[2]);
                    m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                }
                
                ImGui::Spacing();
                
                // Animation controls (simplified)
                bool enableAnimation = animParams.enableAnimation;
                if (ImGui::Checkbox("Auto Rotate", &enableAnimation))
                {
                    ModelAnimationParams updatedParams = animParams;
                    updatedParams.enableAnimation = enableAnimation;
                    updatedParams.rotationSpeed = enableAnimation ? 1.0f : 0.0f;
                    updatedParams.rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                    m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                }
                
                if (animParams.enableAnimation)
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Speed:"); ImGui::SameLine();
                    float rotationSpeed = animParams.rotationSpeed;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("##Speed", &rotationSpeed, 0.1f, 0.0f, 5.0f, "%.1f"))
                    {
                        ModelAnimationParams updatedParams = animParams;
                        updatedParams.rotationSpeed = rotationSpeed;
                        m_render_resource->updateRenderObjectAnimationParams(selected_model, updatedParams);
                    }
                }
                
                ImGui::Spacing();
                
                // Control buttons
                if (ImGui::Button("Reset", ImVec2(-1, 0)))
                {
                    ModelAnimationParams resetParams = animParams;
                    resetParams.position = glm::vec3(0.0f);
                    resetParams.rotation = glm::vec3(0.0f);
                    resetParams.scale = glm::vec3(1.0f);
                    resetParams.enableAnimation = false;
                    m_render_resource->updateRenderObjectAnimationParams(selected_model, resetParams);
                }
                
                if (ImGui::Button("Save Config", ImVec2(-1, 0)))
                {
                    saveModelConfiguration();
                }
            }
        }
        else
        {
            ImGui::Text("No models available for transformation");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Please ensure model files are loaded correctly");
        }
        
        ImGui::End();
        
        // Scene Objects Window (Compact)
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        if (m_render_resource && !m_render_resource->getLoadedRenderObjects().empty())
        {
            const auto& renderObjects = m_render_resource->getLoadedRenderObjects();
            for (size_t i = 0; i < renderObjects.size(); ++i)
            {
                const auto& obj = renderObjects[i];
                ImGui::Text("%zu. %s", i + 1, obj.name.c_str());
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No objects");
        }
        
        ImGui::End();
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

} // namespace Elish