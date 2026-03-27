#include "main_camera_pass.h"
#include "directional_light_pass.h"
#include "../../core/base/macro.h"
#include "../../core/asset/asset_manager.h"
#include "../../render/interface/rhi.h"
#include "../../render/interface/vulkan/vulkan_rhi_resource.h"
#include "../../render/interface/vulkan/vulkan_rhi.h"
#include "../../render/interface/vulkan/vulkan_util.h"
#include "../../render/render_system.h"
#include "../render_resource.h"
#include "../render_pipeline.h"
#include "ui_pass.h"
#include "../../global/global_context.h"
#include "../../input/input_system.h"

#include <vector>
#include <cstring>
#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>

#include "../../shader/generated/cpp/pic_vert.h"
#include "../../shader/generated/cpp/pic_frag.h"
#include "../../shader/generated/cpp/skybox_new_vert.h"
#include "../../shader/generated/cpp/skybox_new_frag.h"
#include "../../shader/generated/cpp/model_vert.h"
#include "../../shader/generated/cpp/model_frag.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../../3rdparty/tinyobjloader/examples/viewer/stb_image.h"

namespace Elish
{
    /**
     * @brief 初始化主相机渲染通道。
     * 该函数负责设置渲染通道所需的所有资源，包括创建Uniform Buffer、设置渲染通道、描述符集布局、图形管线、描述符集以及交换链帧缓冲。
     * 它不涉及模型的加载，模型加载现在由RenderSystem负责。
     */
    void MainCameraPass::initialize()
    {
        // LOG_INFO("[MainCameraPass] Starting initialize");
        // 初始化相机
        m_camera = std::make_shared<RenderCamera>();
        
        // 获取窗口尺寸设置相机参数
        auto swapchainInfo = m_rhi->getSwapchainInfo();
        float windowWidth = static_cast<float>(swapchainInfo.extent.width);
        float windowHeight = static_cast<float>(swapchainInfo.extent.height);
        float aspectRatio = windowWidth / windowHeight;
        
        // 设置相机初始位置和参数
        m_camera->m_position = glm::vec3(1.0f, 1.0f, 1.0f);
        m_camera->setAspect(aspectRatio);
        m_camera->setFOVx(45.0f);
        m_camera->m_znear = 0.1f;
        m_camera->m_zfar = 1000.0f;
        
        setupBackgroundTexture();
		// 直接绘制方式不需要顶点缓冲和索引缓冲
		// createVertexBuffer();		// 创建VertexBuffer顶点缓存区
		// createIndexBuffer();		// 创建IndexBuffer顶点点序缓存区

		createUniformBuffers();		// 创建UnifromBuffer统一缓存区


        setupAttachments();
        setupRenderPass();

        setupDescriptorSetLayout();//背景，模型相关资源在rs中创建
        setupPipelines();//背景管线
        setupBackgroundDescriptorSet();//分配背景描述符集空间
        setupFramebufferDescriptorSet();//专门设置帧缓冲相关的描述符集
        setupSwapchainFramebuffers();
        
        
    }
    /**
     * @brief 执行主相机通道的绘制操作。
     * 该函数目前主要用于延时渲染的占位符，实际的绘制逻辑在 `drawForward` 中实现。
     */
    void MainCameraPass::draw()
    {
        // 延时渲染
    }
    // /**
    //  * @brief 准备渲染通道所需的数据。
    //  * 该函数负责从渲染资源中获取已加载的模型数据，并更新本地的模型数据列表。
    //  * 同时，它还会更新Uniform Buffer中的数据，例如相机矩阵等，以确保渲染时使用最新的场景信息。
    //  * @param render_resource 包含渲染所需资源的共享指针。
    void MainCameraPass::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        // 获取当前时间戳
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        // 从输入系统获取最新的摄像机状态
        if (m_camera && g_runtime_global_context.m_input_system) {
            auto input_system = g_runtime_global_context.m_input_system;
            
            // 获取输入系统中计算好的摄像机位置和旋转
            glm::vec3 camera_position = input_system->getCameraPosition();
            glm::quat camera_rotation = input_system->getCameraRotation();
            glm::mat4 view_matrix = input_system->getCameraViewMatrix();
            
            // LOG_DEBUG("[CAMERA_PASS][{}ms] preparePassData - Camera Position: ({:.3f}, {:.3f}, {:.3f})", 
            //           timestamp, camera_position.x, camera_position.y, camera_position.z);
            
            // 直接设置摄像机的位置和旋转
            m_camera->m_position = camera_position;
            m_camera->m_rotation = camera_rotation;
            m_camera->m_invRotation = glm::inverse(camera_rotation); // 更新逆旋转矩阵
        }
        else
        {
            // LOG_DEBUG("[CAMERA_PASS][{}ms] Camera or input system not available", timestamp);
        }
        
        // Store render resource for later use
        m_render_resource = render_resource;
        LOG_DEBUG("[preparePassData] m_render_resource set to: {}, render_resource parameter: {}", 
                 (void*)m_render_resource.get(), (void*)render_resource.get());
        
        // 获取阴影通道资源并设置到渲染资源中
        auto render_system = g_runtime_global_context.m_render_system;
        if (render_system) {
            auto render_pipeline = render_system->getRenderPipeline();
            if (render_pipeline) {
                auto shadow_pass_base = render_pipeline->getDirectionalLightShadowPass();
                if (shadow_pass_base) {
                    // 转换为 DirectionalLightShadowPass 类型
                    auto shadow_pass = std::dynamic_pointer_cast<DirectionalLightShadowPass>(shadow_pass_base);
                    if (shadow_pass) {
                        // 获取阴影贴图资源
                        auto shadow_image_view = shadow_pass->getShadowMapView();
                        auto shadow_sampler = shadow_pass->getShadowMapSampler();
                        
                        LOG_DEBUG("[MainCameraPass::preparePassData] Shadow resources: imageView={}, sampler={}", 
                                 (void*)shadow_image_view, (void*)shadow_sampler);
                        
                        if (shadow_image_view && shadow_sampler) {
                            // 设置阴影贴图资源到渲染资源中
                            render_resource->setDirectionalLightShadowResources(shadow_image_view, shadow_sampler);
                            LOG_INFO("[MainCameraPass::preparePassData] Successfully set shadow resources to RenderResource");
                        } else {
                            LOG_ERROR("[MainCameraPass::preparePassData] Failed to get shadow resources from DirectionalLightShadowPass");
                        }
                    } else {
                        LOG_ERROR("[MainCameraPass::preparePassData] Failed to cast shadow_pass_base to DirectionalLightShadowPass");
                    }
                } else {
                    LOG_ERROR("[MainCameraPass::preparePassData] shadow_pass_base is null");
                }
            } else {
                LOG_ERROR("[MainCameraPass::preparePassData] render_pipeline is null");
            }
        } else {
            LOG_ERROR("[MainCameraPass::preparePassData] render_system is null");
        }
        
        // Store loaded render objects to avoid repeated access
        // 只在首次或对象数量变化时更新，避免重复分配描述符集
        if (m_render_resource) {
            auto currentObjects = m_render_resource->getLoadedRenderObjects();
            
            // 检查对象列表是否发生变化
            bool objectsChanged = (currentObjects.size() != m_loaded_render_objects.size());
            if (objectsChanged && m_model_descriptor_sets_initialized) {
                LOG_WARN("[preparePassData] Render objects count changed from {} to {}, need to reinitialize descriptor sets",
                        m_loaded_render_objects.size(), currentObjects.size());
                m_model_descriptor_sets_initialized = false;
            }
            
            m_loaded_render_objects = currentObjects;
            
            if (!m_loaded_render_objects.empty()) {
                const auto& renderObject = m_loaded_render_objects[0];
            } else {
                // LOG_ERROR("[preparePassData] No loaded render objects available");
            }
        } else {
            LOG_ERROR("[preparePassData] Render resource is null");
        }
        
        // Create model rendering pipeline if not already created
        if (m_render_resource && !m_render_resource->isModelPipelineResourceCreated()) {
            if (!m_render_resource->createModelPipelineResource(m_framebuffer.render_pass)) {
                LOG_ERROR("[MainCameraPass::preparePassData] Failed to create model pipeline resource");
            } else {
                // Set up model pipeline in render pipelines array
                if (m_render_pipelines.size() >= 3) {
                    const auto& modelPipelineResource = m_render_resource->getModelPipelineResource();
                    m_render_pipelines[2].pipelineLayout = modelPipelineResource.pipelineLayout;
                    m_render_pipelines[2].graphicsPipeline = modelPipelineResource.graphicsPipeline;
                    m_render_pipelines[2].descriptorSetLayout = modelPipelineResource.descriptorSetLayout;
                }
            }
        }
        
        // Setup model descriptor set now that textures are available (only once)
        LOG_DEBUG("[preparePassData] Checking model descriptor set setup: m_loaded_render_objects.size()={}, m_model_descriptor_sets_initialized={}", 
                 m_loaded_render_objects.size(), m_model_descriptor_sets_initialized);
        
        if (!m_loaded_render_objects.empty() && !m_model_descriptor_sets_initialized) {
            // 检查阴影贴图资源是否已设置
            auto shadowImageView = m_render_resource->getDirectionalLightShadowImageView();
            auto shadowSampler = m_render_resource->getDirectionalLightShadowImageSampler();
            LOG_INFO("[preparePassData] Before setupModelDescriptorSet: shadowImageView={}, shadowSampler={}", 
                     (void*)shadowImageView, (void*)shadowSampler);
            
            if (shadowImageView && shadowSampler) {
                bool success = setupModelDescriptorSet();
                if (success) {
                    m_model_descriptor_sets_initialized = true;
                    LOG_INFO("[preparePassData] Model descriptor sets initialized successfully");
                } else {
                    LOG_ERROR("[preparePassData] Model descriptor sets setup failed, will retry next frame");
                }
            } else {
                LOG_WARN("[preparePassData] Shadow resources not ready yet, will retry next frame");
            }
        } else if (m_loaded_render_objects.empty()) {
            LOG_WARN("[preparePassData] m_loaded_render_objects is empty, cannot setup model descriptor sets");
        } else if (m_model_descriptor_sets_initialized) {
            LOG_DEBUG("[preparePassData] Model descriptor sets already initialized, skipping");
        }
        
        // 天空盒描述符集将在首次渲染时延迟初始化
    }
    //补充前向渲染的命令
    /**
     * @brief 执行前向渲染命令。
     * 该函数负责在给定的交换链图像索引上执行所有前向渲染操作。
     * 它会更新Uniform Buffer，获取当前命令缓冲区，开始渲染通道，设置视口和裁剪区域，
     * 绑定并绘制背景管线，然后切换到模型渲染管线，绑定全局描述符集，最后绘制场景中的模型。
     * @param swapchain_image_index 当前交换链图像的索引，用于绑定正确的帧缓冲。
     */
    void MainCameraPass::drawForward(uint32_t swapchain_image_index)
    {
        

        
        updateUniformBuffer(m_rhi->getCurrentFrameIndex());//更新统一缓存区，更新描述符集
        
        
        // 获取当前命令缓冲区
        
        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (!command_buffer) {
            LOG_ERROR("Failed to get current command buffer");
            return;
        }
        
        // 设置渲染通道开始信息
        RHIRenderPassBeginInfo render_pass_begin_info{};
        render_pass_begin_info.sType = RHI_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass = m_framebuffer.render_pass;
        render_pass_begin_info.framebuffer = m_swapchain_framebuffers[swapchain_image_index];
        render_pass_begin_info.renderArea.offset = {0, 0};
        render_pass_begin_info.renderArea.extent = m_rhi->getSwapchainInfo().extent;
        
        // 设置清除值 - 只为颜色附件设置，深度附件由RHI管理
        std::vector<RHIClearValue> clear_values(2);
        clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // 黑色背景
        clear_values[1].depthStencil = {1.0f, 0}; // 深度值1.0，模板值0
        
        render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
        render_pass_begin_info.pClearValues = clear_values.data();
        

        
        // 开始渲染通道
        m_rhi->cmdBeginRenderPassPFN(command_buffer, &render_pass_begin_info, RHI_SUBPASS_CONTENTS_INLINE);

        // === 子通道0：主渲染（背景+模型） ===
        float main_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        m_rhi->pushEvent(command_buffer, "MAIN RENDER SUBPASS", main_color);
        
        // 设置视口和裁剪矩形
        // 尝试从 RenderPipeline 获取 EditorLayoutState
        bool viewportSet = false;
        if (g_runtime_global_context.m_render_system) {
            auto pipeline = std::dynamic_pointer_cast<RenderPipeline>(g_runtime_global_context.m_render_system->getRenderPipeline());
            if (pipeline) {
                const auto& layoutState = pipeline->getEditorLayoutState();
                
                // 如果计算出的视口有效（宽度和高度大于0），则使用它
                if (layoutState.sceneViewport.width > 1.0f && layoutState.sceneViewport.height > 1.0f) {
                    RHIViewport viewport;
                    viewport.x = layoutState.sceneViewport.x;
                    viewport.y = layoutState.sceneViewport.y;
                    viewport.width = layoutState.sceneViewport.width;
                    viewport.height = layoutState.sceneViewport.height;
                    viewport.minDepth = 0.0f;
                    viewport.maxDepth = 1.0f;
                    
                    RHIRect2D scissor;
                    scissor.offset.x = (int32_t)viewport.x;
                    scissor.offset.y = (int32_t)viewport.y;
                    scissor.extent.width = (uint32_t)viewport.width;
                    scissor.extent.height = (uint32_t)viewport.height;
                    
                    m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, &viewport);
                    m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, &scissor);
                    viewportSet = true;
                    
                    // 更新相机宽高比
                    if (m_camera) {
                        m_camera->setAspect(viewport.width / viewport.height);
                    }
                }
            }
        }
        
        if (!viewportSet) {
            // 回退到默认全屏视口
            m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
            m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);
        }
        
        // 渲染背景
        drawBackground(command_buffer);
        
        // 渲染天空盒
        drawSkybox(command_buffer);
        
        // 渲染模型
        drawModels(command_buffer);
        
        m_rhi->popEvent(command_buffer);
        
        // === 切换到子通道1：UI渲染 ===
        m_rhi->cmdNextSubpassPFN(command_buffer, RHI_SUBPASS_CONTENTS_INLINE);
        
        float ui_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        m_rhi->pushEvent(command_buffer, "UI RENDER SUBPASS", ui_color);
        
        // 渲染UI内容
        drawUI(command_buffer);
        
        m_rhi->popEvent(command_buffer);
        
        // 结束渲染通道
        m_rhi->cmdEndRenderPassPFN(command_buffer);

        

    }

    void MainCameraPass::drawBackground(RHICommandBuffer* command_buffer){
        if (!m_enable_background) {
            return; // 如果背景绘制未启用，直接返回
        }
        
        // 绑定图形管线
        if (!m_render_pipelines.empty() && m_render_pipelines[0].graphicsPipeline) {
            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[0].graphicsPipeline);
    
        } else {
            LOG_ERROR("MainCameraPass: No graphics pipeline available");
        }
        
        // 直接绘制方式不需要绑定顶点缓冲区和索引缓冲区
        
        // 绑定背景渲染的描述符集
        uint32_t currentFrame = m_rhi->getCurrentFrameIndex();
        
        
        
        
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, 
                                      m_render_pipelines[0].pipelineLayout, 0, 1, &m_descriptor_infos[currentFrame].descriptor_set, 0, nullptr);
        
        
    
        
        
        // 执行直接绘制命令（绘制6个顶点组成的两个三角形）
        m_rhi->cmdDraw(command_buffer, 6, 1, 0, 0);
    }

    /**
     * @brief 绘制天空盒。
     * 该函数使用天空盒渲染管线和IBL立方体贴图资源绘制天空盒
     */
    void MainCameraPass::drawSkybox(RHICommandBuffer* command_buffer)
    {
        if (!m_enable_skybox) {
            LOG_DEBUG("[Skybox] Skybox rendering disabled, skipping draw");
            return; // 如果天空盒未启用，直接返回
        }
        
        // LOG_DEBUG("[Skybox] Starting skybox rendering");

        // 延迟初始化天空盒描述符集（只在首次渲染时初始化一次）
        if (!m_skybox_descriptor_sets_initialized) {
            LOG_DEBUG("[Skybox] Descriptor sets not initialized, setting up now");
            setupSkyboxDescriptorSet();
            // 如果初始化失败，setupSkyboxDescriptorSet会禁用天空盒
            if (!m_skybox_descriptor_sets_initialized) {
                return;
            }
        }

        // 绑定天空盒图形管线
        if (m_render_pipelines.size() > 1 && m_render_pipelines[1].graphicsPipeline) {
            // LOG_DEBUG("[Skybox] Binding skybox graphics pipeline");
            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[1].graphicsPipeline);
        } else {
            LOG_ERROR("[Skybox] No skybox graphics pipeline available (pipeline count: {})", m_render_pipelines.size());
            return;
        }

        // 绑定天空盒渲染的描述符集
        uint32_t currentFrame = m_rhi->getCurrentFrameIndex();
        // LOG_DEBUG("[Skybox] Binding descriptor sets for frame {}", currentFrame);
        
        // 检查描述符集数组边界
        if (currentFrame >= m_skybox_descriptor_sets.size()) {
            LOG_ERROR("[Skybox] Current frame index {} exceeds descriptor sets size {}", currentFrame, m_skybox_descriptor_sets.size());
            return;
        }
        
        // 检查描述符集是否有效
        if (!m_skybox_descriptor_sets[currentFrame]) {
            LOG_ERROR("[Skybox] Descriptor set for frame {} is null", currentFrame);
            return;
        }
        
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                       m_render_pipelines[1].pipelineLayout, 0, 1, &m_skybox_descriptor_sets[currentFrame], 0, nullptr);

        // 准备推送常量数据（视图投影矩阵和相机位置）
        struct SkyboxPushConstants {
            glm::mat4 viewProjectionMatrix;
            glm::vec3 cameraPosition;
            float padding; // 对齐到16字节边界
        } pushConstants;

        // 获取当前相机的视图投影矩阵和位置
        if (m_camera) {
            pushConstants.viewProjectionMatrix = m_camera->getPersProjMatrix() * m_camera->getViewMatrix();
            pushConstants.cameraPosition = m_camera->position();
        } else {
            // 如果相机未初始化，使用默认值
            pushConstants.viewProjectionMatrix = glm::mat4(1.0f);
            pushConstants.cameraPosition = glm::vec3(0.0f, 0.0f, 2.0f);
        }

        // 推送常量到着色器
        // LOG_DEBUG("[Skybox] Pushing constants to vertex shader");
        m_rhi->cmdPushConstantsPFN(command_buffer, m_render_pipelines[1].pipelineLayout, 
                                  RHI_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkyboxPushConstants), &pushConstants);

        // 执行天空盒绘制命令（绘制36个顶点组成的立方体）
        // LOG_DEBUG("[Skybox] Executing draw command (36 vertices)");
        m_rhi->cmdDraw(command_buffer, 36, 1, 0, 0);
        // LOG_DEBUG("[Skybox] Skybox rendering completed successfully");
    }

    // 设置渲染附件的方法
    void MainCameraPass::setupAttachments()
    {
        // 记录日志信息，表示开始设置附件，这里的附件指交换链以外的附件

        
        // MainCameraPass直接使用交换链图像视图作为颜色附件，不需要创建自己的颜色附件
        // 深度附件使用全局深度缓冲
        // 清空附件数组，因为我们直接使用交换链图像视图
        m_framebuffer.attachments.clear();
        

    }
    // 设置渲染通道的方法
    /**
     * @brief 设置主相机渲染通道，包含两个子通道：主渲染子通道和UI子通道
     * 子通道0：主渲染（背景+模型）
     * 子通道1：UI渲染（ImGui界面）
     */
    void MainCameraPass::setupRenderPass()
    {
        

        // 定义附件：颜色附件和深度附件
        RHIAttachmentDescription attachments[2] = {};

        // 配置颜色附件 - 使用交换链图像格式
        attachments[0].format = m_rhi->getSwapchainInfo().image_format;
        attachments[0].samples = RHI_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = RHI_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = RHI_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // 配置深度附件
        attachments[1].format = m_rhi->getDepthImageInfo().depth_image_format;
        attachments[1].samples = RHI_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = RHI_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 定义附件引用
        RHIAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        RHIAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // === 子通道定义 ===
        RHISubpassDescription subpasses[2] = {};
        
        // 子通道0：主渲染通道（背景+模型）
        subpasses[0].pipelineBindPoint = RHI_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[0].colorAttachmentCount = 1;
        subpasses[0].pColorAttachments = &colorAttachmentRef;
        subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;
        subpasses[0].inputAttachmentCount = 0;
        subpasses[0].pInputAttachments = nullptr;
        subpasses[0].pResolveAttachments = nullptr;
        subpasses[0].preserveAttachmentCount = 0;
        subpasses[0].pPreserveAttachments = nullptr;
        
        // 子通道1：UI渲染通道（ImGui）
        subpasses[1].pipelineBindPoint = RHI_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[1].colorAttachmentCount = 1;
        subpasses[1].pColorAttachments = &colorAttachmentRef;
        subpasses[1].pDepthStencilAttachment = nullptr; // UI不需要深度测试
        subpasses[1].inputAttachmentCount = 0;
        subpasses[1].pInputAttachments = nullptr;
        subpasses[1].pResolveAttachments = nullptr;
        subpasses[1].preserveAttachmentCount = 0;
        subpasses[1].pPreserveAttachments = nullptr;

        // === 子通道依赖关系定义 ===
        RHISubpassDependency dependencies[2] = {};
        
        // 依赖0：外部到子通道0（主渲染）
        dependencies[0].srcSubpass = RHI_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstStageMask = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | RHI_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = 0;
        
        // 依赖1：子通道0到子通道1（主渲染到UI渲染）
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = 1;
        dependencies[1].srcStageMask = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

        // === 创建渲染通道 ===
        RHIRenderPassCreateInfo renderpass_create_info{};
        renderpass_create_info.sType = RHI_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderpass_create_info.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
        renderpass_create_info.pAttachments = attachments;
        renderpass_create_info.subpassCount = sizeof(subpasses) / sizeof(subpasses[0]);
        renderpass_create_info.pSubpasses = subpasses;
        renderpass_create_info.dependencyCount = sizeof(dependencies) / sizeof(dependencies[0]);
        renderpass_create_info.pDependencies = dependencies;

        if (m_rhi->createRenderPass(&renderpass_create_info, m_framebuffer.render_pass) != RHI_SUCCESS)
        {
            throw std::runtime_error("[MainCameraPass] Failed to create render pass with subpasses");
        }

        
    }
    // 设置描述符集布局的方法
    /**
     * @brief 设置主相机渲染通道的描述符集布局。
     * 该函数负责定义和创建渲染管线所需的描述符集布局，分别为背景shader和模型shader创建不同的布局。
     * 背景shader需要：binding 0 (UBO), binding 1 (纹理采样器)
     * 模型shader需要：binding 0 (UBO), binding 1 (纹理采样器1), binding 2 (纹理采样器2)
     */
    void MainCameraPass::setupDescriptorSetLayout()
    {
        // 调整描述符布局数组大小为飞行中的帧数，为每个帧分配独立的描述符集
        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        m_descriptor_infos.resize(maxFramesInFlight);

        // === 创建背景渲染的描述符集布局 ===
        RHIDescriptorSetLayoutBinding background_bindings[2];

        // 绑定点 0 的配置：UBO
        RHIDescriptorSetLayoutBinding& bg_uboLayoutBinding = background_bindings[0];
        bg_uboLayoutBinding.binding                       = 0;
        bg_uboLayoutBinding.descriptorType                = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bg_uboLayoutBinding.descriptorCount               = 1;
        bg_uboLayoutBinding.stageFlags                    = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;
        bg_uboLayoutBinding.pImmutableSamplers            = NULL;

        // 绑定点 1 的配置：纹理采样器
        RHIDescriptorSetLayoutBinding& bg_samplerLayoutBinding = background_bindings[1];
        bg_samplerLayoutBinding.binding = 1;
        bg_samplerLayoutBinding.descriptorCount = 1;
        bg_samplerLayoutBinding.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bg_samplerLayoutBinding.pImmutableSamplers = nullptr;
        bg_samplerLayoutBinding.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;

        RHIDescriptorSetLayoutCreateInfo bg_layoutInfo{};
        bg_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        bg_layoutInfo.bindingCount = sizeof(background_bindings) / sizeof(background_bindings[0]);
        bg_layoutInfo.pBindings = background_bindings;

        // === 修复：所有帧共享同一个描述符集布局 ===
        // 只创建一个描述符集布局，所有帧共享
        RHIDescriptorSetLayout* sharedLayout;
        if (m_rhi->createDescriptorSetLayout(&bg_layoutInfo, sharedLayout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create background descriptor set layout");
        }
        
        // 所有帧使用相同的布局
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
            m_descriptor_infos[frameIndex].layout = sharedLayout;
        }

        // === 创建天空盒专用的描述符集布局 ===
        RHIDescriptorSetLayoutBinding skybox_bindings[1];
        
        // 绑定点 0 的配置：立方体贴图采样器（天空盒不需要UBO）
        RHIDescriptorSetLayoutBinding& skybox_samplerLayoutBinding = skybox_bindings[0];
        skybox_samplerLayoutBinding.binding = 0;
        skybox_samplerLayoutBinding.descriptorCount = 1;
        skybox_samplerLayoutBinding.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skybox_samplerLayoutBinding.pImmutableSamplers = nullptr;
        skybox_samplerLayoutBinding.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;

        RHIDescriptorSetLayoutCreateInfo skybox_layoutInfo{};
        skybox_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        skybox_layoutInfo.bindingCount = sizeof(skybox_bindings) / sizeof(skybox_bindings[0]);
        skybox_layoutInfo.pBindings = skybox_bindings;

        // 创建天空盒描述符集布局
        if (m_rhi->createDescriptorSetLayout(&skybox_layoutInfo, m_skybox_descriptor_layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create skybox descriptor set layout");
        }

        // === 创建模型渲染的描述符集布局 ===
        // 根据mesh.vert和mesh.frag的绑定需求：
        // binding 0: UBO (view/proj matrices)
        // binding 1: UBOV (lights info) 
        // binding 2: Cubemap sampler (IBL)
        // binding 3-7: Texture samplers (5 textures)
        // binding 8: Directional light shadow map
        // binding 9: Light space matrix UBO
        
        RHIDescriptorSetLayoutBinding model_bindings[10];
        
        // Binding 0: UBO (view/proj matrices)
        model_bindings[0].binding = 0;
        model_bindings[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[0].descriptorCount = 1;
        model_bindings[0].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[0].pImmutableSamplers = nullptr;
        
        // Binding 1: UBOV (lights info)
        model_bindings[1].binding = 1;
        model_bindings[1].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[1].descriptorCount = 1;
        model_bindings[1].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[1].pImmutableSamplers = nullptr;
        
        // Binding 2: Cubemap sampler (IBL)
        model_bindings[2].binding = 2;
        model_bindings[2].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        model_bindings[2].descriptorCount = 1;
        model_bindings[2].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[2].pImmutableSamplers = nullptr;
        
        // Binding 3-7: Texture samplers (5 textures)
        for (int i = 3; i < 8; ++i) {
            model_bindings[i].binding = i;
            model_bindings[i].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            model_bindings[i].descriptorCount = 1;
            model_bindings[i].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
            model_bindings[i].pImmutableSamplers = nullptr;
        }
        
        // Binding 8: Directional light shadow map
        model_bindings[8].binding = 8;
        model_bindings[8].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        model_bindings[8].descriptorCount = 1;
        model_bindings[8].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[8].pImmutableSamplers = nullptr;
        
        // Binding 9: Light space matrix UBO
        model_bindings[9].binding = 9;
        model_bindings[9].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[9].descriptorCount = 1;
        model_bindings[9].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
        model_bindings[9].pImmutableSamplers = nullptr;

        RHIDescriptorSetLayoutCreateInfo model_layoutInfo{};
        model_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        model_layoutInfo.bindingCount = 10;
        model_layoutInfo.pBindings = model_bindings;

        // 创建模型描述符集布局并存储到m_render_pipelines[2]
        if (m_render_pipelines.size() < 3) {
            m_render_pipelines.resize(3);
        }
        if (m_rhi->createDescriptorSetLayout(&model_layoutInfo, m_render_pipelines[2].descriptorSetLayout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create model descriptor set layout");
        }
        
        LOG_INFO("[setupDescriptorSetLayout] Model descriptor set layout created successfully");
        
    }
    /**
     * @brief 设置渲染管线。
     * 该函数负责创建和配置主相机渲染通道所需的图形管线，包括背景三角形管线和模型渲染管线。
     * 每个管线都包含其布局、着色器阶段、顶点输入状态、输入汇编状态、视口状态、光栅化状态、多重采样状态、颜色混合状态、深度模板状态和动态状态。
     * @note 目前创建两个管线：m_render_pipelines[0] 用于背景三角形，m_render_pipelines[1] 用于模型渲染。
     */
    // 函数功能：为"主相机渲染通道"创建两个图形管线（背景渲染管线 + 模型渲染管线）
    // 图形管线是Vulkan渲染的核心，定义了从顶点数据到像素输出的完整渲染流程
    void MainCameraPass::setupPipelines()
    {
        
        m_render_pipelines.resize(3);  // Resize to accommodate background, skybox, and model pipelines  

                
        
        
        
        RHIDescriptorSetLayout*      descriptorset_layouts[1] = {m_descriptor_infos[0].layout};

        RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
        pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;
        // 背景渲染不使用推送常量，所以设置为0
        pipeline_layout_create_info.pushConstantRangeCount = 0;
        pipeline_layout_create_info.pPushConstantRanges = nullptr;

        if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[0].pipelineLayout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create mesh lighting pipeline layout");
        }
        
        
        //shader创建和顶点缓冲区绑定
        RHIShader* vert_shader_module = m_rhi->createShaderModule(PIC_VERT);
        RHIShader* frag_shader_module = m_rhi->createShaderModule(PIC_FRAG);

        RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
        vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
        vert_pipeline_shader_stage_create_info.module = vert_shader_module;
        vert_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
        frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
        frag_pipeline_shader_stage_create_info.module = frag_shader_module;
        frag_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};
                                                        

        // 直接绘制方式不需要顶点缓冲，设置空的顶点输入状态
        RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
        vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state_create_info.vertexBindingDescriptionCount   = 0;
        vertex_input_state_create_info.pVertexBindingDescriptions      = nullptr;
        vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
        vertex_input_state_create_info.pVertexAttributeDescriptions    = nullptr;

        RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
        input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

        RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
        viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state_create_info.viewportCount = 1;
        viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
        viewport_state_create_info.scissorCount  = 1;
        viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

        RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
        rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
        rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
        rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
        rasterization_state_create_info.lineWidth               = 1.0f;
        rasterization_state_create_info.cullMode                = RHI_CULL_MODE_NONE;
        rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
        rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
        rasterization_state_create_info.depthBiasClamp          = 0.0f;
        rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

        RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
        multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
        multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

        RHIPipelineColorBlendAttachmentState color_blend_attachments[1] = {};
        color_blend_attachments[0].colorWriteMask = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                    RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
        color_blend_attachments[0].blendEnable         = RHI_FALSE;
        // color_blend_attachments[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
        // color_blend_attachments[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ONE;
        // color_blend_attachments[0].colorBlendOp        = RHI_BLEND_OP_ADD;
        // color_blend_attachments[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
        // color_blend_attachments[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
        // color_blend_attachments[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

        RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
        color_blend_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend_state_create_info.logicOpEnable = RHI_FALSE;
        color_blend_state_create_info.logicOp       = RHI_LOGIC_OP_COPY;
        color_blend_state_create_info.attachmentCount =
            sizeof(color_blend_attachments) / sizeof(color_blend_attachments[0]);
        color_blend_state_create_info.pAttachments      = &color_blend_attachments[0];
        color_blend_state_create_info.blendConstants[0] = 0.0f;
        color_blend_state_create_info.blendConstants[1] = 0.0f;
        color_blend_state_create_info.blendConstants[2] = 0.0f;
        color_blend_state_create_info.blendConstants[3] = 0.0f;

        RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
        depth_stencil_create_info.sType            = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil_create_info.depthTestEnable  = RHI_FALSE; // Enable depth testing
        depth_stencil_create_info.depthWriteEnable = RHI_FALSE; // Enable writing to depth buffer
        depth_stencil_create_info.depthCompareOp   = RHI_COMPARE_OP_LESS_OR_EQUAL; // Allow objects at same depth or closer to be drawn
        depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
        depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

        RHIDynamicState                   dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
        RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
        dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_create_info.dynamicStateCount = 2;
        dynamic_state_create_info.pDynamicStates    = dynamic_states;

        RHIGraphicsPipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = shader_stages;
        pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
        pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
        pipelineInfo.pViewportState      = &viewport_state_create_info;
        pipelineInfo.pRasterizationState = &rasterization_state_create_info;
        pipelineInfo.pMultisampleState   = &multisample_state_create_info;
        pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
        pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
        pipelineInfo.layout              = m_render_pipelines[0].pipelineLayout;
        pipelineInfo.renderPass          = m_framebuffer.render_pass;
        pipelineInfo.subpass             = 0;
        pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
        pipelineInfo.pDynamicState       = &dynamic_state_create_info;

        if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
            1,
            &pipelineInfo,
            m_render_pipelines[0].graphicsPipeline) !=
            RHI_SUCCESS)
        {
            throw std::runtime_error("create mesh lighting graphics pipeline");
        }
        m_rhi->destroyShaderModule(vert_shader_module);
        m_rhi->destroyShaderModule(frag_shader_module);

        // 创建天空盒渲染管线
        RHIDescriptorSetLayout* skybox_descriptorset_layouts[1] = {m_skybox_descriptor_layout};

        // 定义推送常量范围（用于传递视图投影矩阵和相机位置）
        RHIPushConstantRange skybox_push_constant_range {};
        skybox_push_constant_range.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
        skybox_push_constant_range.offset = 0;
        skybox_push_constant_range.size = sizeof(glm::mat4) + sizeof(glm::vec3) + sizeof(float); // mat4 + vec3 + padding

        RHIPipelineLayoutCreateInfo skybox_pipeline_layout_create_info {};
        skybox_pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        skybox_pipeline_layout_create_info.setLayoutCount = 1;
        skybox_pipeline_layout_create_info.pSetLayouts    = skybox_descriptorset_layouts;
        skybox_pipeline_layout_create_info.pushConstantRangeCount = 1;
        skybox_pipeline_layout_create_info.pPushConstantRanges = &skybox_push_constant_range;

        if (m_rhi->createPipelineLayout(&skybox_pipeline_layout_create_info, m_render_pipelines[1].pipelineLayout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create skybox pipeline layout");
        }

        // 创建天空盒着色器模块
        RHIShader* skybox_vert_shader_module = m_rhi->createShaderModule(SKYBOX_NEW_VERT);
        RHIShader* skybox_frag_shader_module = m_rhi->createShaderModule(SKYBOX_NEW_FRAG);

        RHIPipelineShaderStageCreateInfo skybox_vert_pipeline_shader_stage_create_info {};
        skybox_vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        skybox_vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
        skybox_vert_pipeline_shader_stage_create_info.module = skybox_vert_shader_module;
        skybox_vert_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo skybox_frag_pipeline_shader_stage_create_info {};
        skybox_frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        skybox_frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
        skybox_frag_pipeline_shader_stage_create_info.module = skybox_frag_shader_module;
        skybox_frag_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo skybox_shader_stages[] = {skybox_vert_pipeline_shader_stage_create_info,
                                                                   skybox_frag_pipeline_shader_stage_create_info};

        // 天空盒使用相同的顶点输入状态（无顶点缓冲）
        RHIPipelineVertexInputStateCreateInfo skybox_vertex_input_state_create_info {};
        skybox_vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        skybox_vertex_input_state_create_info.vertexBindingDescriptionCount   = 0;
        skybox_vertex_input_state_create_info.pVertexBindingDescriptions      = nullptr;
        skybox_vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
        skybox_vertex_input_state_create_info.pVertexAttributeDescriptions    = nullptr;

        // 天空盒深度测试设置：深度测试开启，深度写入关闭，比较操作为小于等于
        RHIPipelineDepthStencilStateCreateInfo skybox_depth_stencil_create_info {};
        skybox_depth_stencil_create_info.sType            = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        skybox_depth_stencil_create_info.depthTestEnable  = RHI_TRUE;  // 开启深度测试
        skybox_depth_stencil_create_info.depthWriteEnable = RHI_FALSE; // 关闭深度写入
        skybox_depth_stencil_create_info.depthCompareOp   = RHI_COMPARE_OP_LESS_OR_EQUAL; // 小于等于通过测试
        skybox_depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
        skybox_depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

        RHIGraphicsPipelineCreateInfo skybox_pipelineInfo {};
        skybox_pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        skybox_pipelineInfo.stageCount          = 2;
        skybox_pipelineInfo.pStages             = skybox_shader_stages;
        skybox_pipelineInfo.pVertexInputState   = &skybox_vertex_input_state_create_info;
        skybox_pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
        skybox_pipelineInfo.pViewportState      = &viewport_state_create_info;
        skybox_pipelineInfo.pRasterizationState = &rasterization_state_create_info;
        skybox_pipelineInfo.pMultisampleState   = &multisample_state_create_info;
        skybox_pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
        skybox_pipelineInfo.pDepthStencilState  = &skybox_depth_stencil_create_info;
        skybox_pipelineInfo.layout              = m_render_pipelines[1].pipelineLayout;
        skybox_pipelineInfo.renderPass          = m_framebuffer.render_pass;
        skybox_pipelineInfo.subpass             = 0;
        skybox_pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
        skybox_pipelineInfo.pDynamicState       = &dynamic_state_create_info;

        if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
            1,
            &skybox_pipelineInfo,
            m_render_pipelines[1].graphicsPipeline) !=
            RHI_SUCCESS)
        {
            throw std::runtime_error("create skybox graphics pipeline");
        }
        m_rhi->destroyShaderModule(skybox_vert_shader_module);
        m_rhi->destroyShaderModule(skybox_frag_shader_module);

        // === 创建模型渲染管线 (m_render_pipelines[2]) ===
        LOG_INFO("[setupPipelines] Creating model rendering pipeline...");
        
        // 检查模型描述符集布局是否已创建
        if (!m_render_pipelines[2].descriptorSetLayout) {
            LOG_ERROR("[setupPipelines] Model descriptor set layout is null! Cannot create model pipeline.");
            throw std::runtime_error("model descriptor set layout is null");
        }
        
        // 模型管线使用模型描述符集布局
        RHIDescriptorSetLayout* model_descriptorset_layouts[1] = {m_render_pipelines[2].descriptorSetLayout};
        
        // 定义推送常量范围（用于传递模型矩阵）
        RHIPushConstantRange model_push_constant_range {};
        model_push_constant_range.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
        model_push_constant_range.offset = 0;
        model_push_constant_range.size = sizeof(glm::mat4); // 模型变换矩阵

        RHIPipelineLayoutCreateInfo model_pipeline_layout_create_info {};
        model_pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        model_pipeline_layout_create_info.setLayoutCount = 1;
        model_pipeline_layout_create_info.pSetLayouts    = model_descriptorset_layouts;
        model_pipeline_layout_create_info.pushConstantRangeCount = 1;
        model_pipeline_layout_create_info.pPushConstantRanges = &model_push_constant_range;

        if (m_rhi->createPipelineLayout(&model_pipeline_layout_create_info, m_render_pipelines[2].pipelineLayout) != RHI_SUCCESS)
        {
            throw std::runtime_error("create model pipeline layout");
        }
        LOG_INFO("[setupPipelines] Model pipeline layout created successfully");

        // 创建模型着色器模块
        RHIShader* model_vert_shader_module = m_rhi->createShaderModule(MODEL_VERT);
        RHIShader* model_frag_shader_module = m_rhi->createShaderModule(MODEL_FRAG);

        RHIPipelineShaderStageCreateInfo model_vert_pipeline_shader_stage_create_info {};
        model_vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        model_vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
        model_vert_pipeline_shader_stage_create_info.module = model_vert_shader_module;
        model_vert_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo model_frag_pipeline_shader_stage_create_info {};
        model_frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        model_frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_frag_pipeline_shader_stage_create_info.module = model_frag_shader_module;
        model_frag_pipeline_shader_stage_create_info.pName  = "main";

        RHIPipelineShaderStageCreateInfo model_shader_stages[] = {model_vert_pipeline_shader_stage_create_info,
                                                                   model_frag_pipeline_shader_stage_create_info};

        // 模型渲染需要顶点输入状态
        // 顶点格式：position(vec3) + color(vec3) + texCoord(vec2)
        RHIPipelineVertexInputStateCreateInfo model_vertex_input_state_create_info {};
        model_vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        
        // 顶点绑定描述
        RHIVertexInputBindingDescription model_binding_description {};
        model_binding_description.binding = 0;
        model_binding_description.stride = sizeof(float) * 8; // 3 + 3 + 2
        model_binding_description.inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;
        
        // 顶点属性描述
        RHIVertexInputAttributeDescription model_attribute_descriptions[3];
        // Position
        model_attribute_descriptions[0].binding = 0;
        model_attribute_descriptions[0].location = 0;
        model_attribute_descriptions[0].format = RHI_FORMAT_R32G32B32_SFLOAT;
        model_attribute_descriptions[0].offset = 0;
        // Color
        model_attribute_descriptions[1].binding = 0;
        model_attribute_descriptions[1].location = 1;
        model_attribute_descriptions[1].format = RHI_FORMAT_R32G32B32_SFLOAT;
        model_attribute_descriptions[1].offset = sizeof(float) * 3;
        // TexCoord
        model_attribute_descriptions[2].binding = 0;
        model_attribute_descriptions[2].location = 2;
        model_attribute_descriptions[2].format = RHI_FORMAT_R32G32_SFLOAT;
        model_attribute_descriptions[2].offset = sizeof(float) * 6;
        
        model_vertex_input_state_create_info.vertexBindingDescriptionCount = 1;
        model_vertex_input_state_create_info.pVertexBindingDescriptions = &model_binding_description;
        model_vertex_input_state_create_info.vertexAttributeDescriptionCount = 3;
        model_vertex_input_state_create_info.pVertexAttributeDescriptions = model_attribute_descriptions;

        // 模型渲染深度测试设置：开启深度测试和深度写入
        RHIPipelineDepthStencilStateCreateInfo model_depth_stencil_create_info {};
        model_depth_stencil_create_info.sType            = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        model_depth_stencil_create_info.depthTestEnable  = RHI_TRUE;
        model_depth_stencil_create_info.depthWriteEnable = RHI_TRUE;
        model_depth_stencil_create_info.depthCompareOp   = RHI_COMPARE_OP_LESS;
        model_depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
        model_depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

        RHIGraphicsPipelineCreateInfo model_pipelineInfo {};
        model_pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        model_pipelineInfo.stageCount          = 2;
        model_pipelineInfo.pStages             = model_shader_stages;
        model_pipelineInfo.pVertexInputState   = &model_vertex_input_state_create_info;
        model_pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
        model_pipelineInfo.pViewportState      = &viewport_state_create_info;
        model_pipelineInfo.pRasterizationState = &rasterization_state_create_info;
        model_pipelineInfo.pMultisampleState   = &multisample_state_create_info;
        model_pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
        model_pipelineInfo.pDepthStencilState  = &model_depth_stencil_create_info;
        model_pipelineInfo.layout              = m_render_pipelines[2].pipelineLayout;
        model_pipelineInfo.renderPass          = m_framebuffer.render_pass;
        model_pipelineInfo.subpass             = 0;
        model_pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
        model_pipelineInfo.pDynamicState       = &dynamic_state_create_info;

        if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
            1,
            &model_pipelineInfo,
            m_render_pipelines[2].graphicsPipeline) !=
            RHI_SUCCESS)
        {
            throw std::runtime_error("create model graphics pipeline");
        }
        m_rhi->destroyShaderModule(model_vert_shader_module);
        m_rhi->destroyShaderModule(model_frag_shader_module);
        
        LOG_INFO("[setupPipelines] Model graphics pipeline created successfully");

    }
    /**
     * @brief 设置描述符集。
     * 该函数负责为主相机渲染通道设置所有必要的描述符集
     */
    void MainCameraPass::setupBackgroundDescriptorSet()//分配背景描述符集
    {
        
        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        // 为每个飞行中的帧创建独立的描述符集
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
            // update common model's global descriptor set 背景描述符集布局
            RHIDescriptorSetAllocateInfo mesh_global_descriptor_set_alloc_info;
            mesh_global_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; 
            mesh_global_descriptor_set_alloc_info.pNext              = NULL;
            mesh_global_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPoor();//自动创建描述符集
            mesh_global_descriptor_set_alloc_info.descriptorSetCount = 1;
            mesh_global_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[frameIndex].layout;

            
            
            
            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&mesh_global_descriptor_set_alloc_info, m_descriptor_infos[frameIndex].descriptor_set))
            {
                LOG_ERROR("Failed to allocate descriptor set for frame " + std::to_string(frameIndex));
                throw std::runtime_error("allocate mesh global descriptor set");
            }
            
            

            RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info{};
            mesh_perframe_storage_buffer_info.buffer = uniformBuffers[frameIndex];
            mesh_perframe_storage_buffer_info.offset = 0;
            mesh_perframe_storage_buffer_info.range = sizeof(UniformBufferObject);

            RHIDescriptorImageInfo backgroundImageInfo{};
            backgroundImageInfo.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            backgroundImageInfo.imageView = textureImageView;//背景
            backgroundImageInfo.sampler = textureSampler;//背景

            RHIWriteDescriptorSet mesh_descriptor_writes_info[2];
            mesh_descriptor_writes_info[0].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_descriptor_writes_info[0].pNext = nullptr;
            mesh_descriptor_writes_info[0].dstSet = m_descriptor_infos[frameIndex].descriptor_set;
            mesh_descriptor_writes_info[0].dstBinding = 0;
            mesh_descriptor_writes_info[0].dstArrayElement = 0;
            mesh_descriptor_writes_info[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            mesh_descriptor_writes_info[0].descriptorCount = 1;
            mesh_descriptor_writes_info[0].pBufferInfo = &mesh_perframe_storage_buffer_info;
            mesh_descriptor_writes_info[0].pImageInfo = nullptr;
            mesh_descriptor_writes_info[0].pTexelBufferView = nullptr;

            mesh_descriptor_writes_info[1].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_descriptor_writes_info[1].pNext = nullptr;
            mesh_descriptor_writes_info[1].dstSet = m_descriptor_infos[frameIndex].descriptor_set;
            mesh_descriptor_writes_info[1].dstBinding = 1;
            mesh_descriptor_writes_info[1].dstArrayElement = 0;
            mesh_descriptor_writes_info[1].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            mesh_descriptor_writes_info[1].descriptorCount = 1;
            mesh_descriptor_writes_info[1].pBufferInfo = nullptr;
            mesh_descriptor_writes_info[1].pImageInfo = &backgroundImageInfo;
            mesh_descriptor_writes_info[1].pTexelBufferView = nullptr;
            m_rhi->updateDescriptorSets(2, mesh_descriptor_writes_info, 0, nullptr);
        }
        
    }

    /**
     * @brief 设置天空盒描述符集，复用IBL立方体贴图资源。
     * 该函数为天空盒渲染创建描述符集，使用IBL资源中的立方体贴图
     */
    void MainCameraPass::setupSkyboxDescriptorSet()
    {
        // 使用原子操作确保只初始化一次，避免多线程竞争
        static std::atomic<bool> setup_in_progress{false};
        
        if (m_skybox_descriptor_sets_initialized) {
            LOG_DEBUG("[Skybox] Descriptor sets already initialized, skipping setup");
            return; // 避免重复初始化
        }
        
        // 检查是否正在初始化中
        if (setup_in_progress.exchange(true)) {
            LOG_DEBUG("[Skybox] Setup already in progress, waiting...");
            // 等待其他线程完成初始化
            while (!m_skybox_descriptor_sets_initialized && setup_in_progress.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return;
        }
        
        LOG_INFO("[Skybox] Starting skybox descriptor set setup");

        // 检查RenderResource是否可用
        if (!m_render_resource) {
            LOG_ERROR("[Skybox] RenderResource is null, disabling skybox rendering");
            setSkyboxEnabled(false);
            setup_in_progress.store(false);
            return;
        }

        // 检查立方体贴图资源是否可用
        auto cubemapImageView = m_render_resource->getCubemapImageView();
        auto cubemapSampler = m_render_resource->getCubemapImageSampler();
        
        if (!cubemapImageView || !cubemapSampler) {
            LOG_ERROR("[Skybox] Cubemap resources not available (ImageView: {}, Sampler: {}), disabling skybox rendering", 
                     (void*)cubemapImageView, (void*)cubemapSampler);
            setSkyboxEnabled(false);
            setup_in_progress.store(false);
            return;
        }
        
        LOG_DEBUG("[Skybox] Cubemap resources validated successfully");

        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        LOG_DEBUG("[Skybox] Setting up descriptor sets for {} frames in flight", maxFramesInFlight);
        
        // 调整vector大小以容纳所有帧的描述符集
        m_skybox_descriptor_sets.resize(maxFramesInFlight);
        
        // 为每个飞行中的帧创建独立的天空盒描述符集
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++) {
            LOG_DEBUG("[Skybox] Setting up descriptor set for frame {}", frameIndex);
            // 分配天空盒描述符集
            RHIDescriptorSetAllocateInfo skybox_descriptor_set_alloc_info;
            skybox_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            skybox_descriptor_set_alloc_info.pNext              = NULL;
            skybox_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPoor();
            skybox_descriptor_set_alloc_info.descriptorSetCount = 1;
            skybox_descriptor_set_alloc_info.pSetLayouts        = &m_skybox_descriptor_layout;

            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&skybox_descriptor_set_alloc_info, m_skybox_descriptor_sets[frameIndex]))
            {
                LOG_ERROR("[Skybox] Failed to allocate skybox descriptor set for frame {}, disabling skybox rendering", frameIndex);
                setSkyboxEnabled(false);
                setup_in_progress.store(false);
                return;
            }
            
            LOG_DEBUG("[Skybox] Successfully allocated descriptor set for frame {}", frameIndex);

            // 设置IBL立方体贴图信息（天空盒只需要立方体贴图，不需要UBO）
            RHIDescriptorImageInfo skyboxImageInfo{};
            skyboxImageInfo.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            skyboxImageInfo.imageView = cubemapImageView;
            skyboxImageInfo.sampler = cubemapSampler;

            // 写入描述符集（只写入立方体贴图到绑定点0）
            RHIWriteDescriptorSet skybox_descriptor_write_info{};
            skybox_descriptor_write_info.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            skybox_descriptor_write_info.pNext = nullptr;
            skybox_descriptor_write_info.dstSet = m_skybox_descriptor_sets[frameIndex];
            skybox_descriptor_write_info.dstBinding = 0;  // 立方体贴图绑定到绑定点0
            skybox_descriptor_write_info.dstArrayElement = 0;
            skybox_descriptor_write_info.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            skybox_descriptor_write_info.descriptorCount = 1;
            skybox_descriptor_write_info.pBufferInfo = nullptr;
            skybox_descriptor_write_info.pImageInfo = &skyboxImageInfo;
            skybox_descriptor_write_info.pTexelBufferView = nullptr;
            
            m_rhi->updateDescriptorSets(1, &skybox_descriptor_write_info, 0, nullptr);
            LOG_DEBUG("[Skybox] Updated descriptor set for frame {} with cubemap only", frameIndex);
        }
        
        m_skybox_descriptor_sets_initialized = true;
        LOG_INFO("[Skybox] Skybox descriptor sets setup completed successfully");
        
        // 重置初始化进行中的标志
        setup_in_progress.store(false);
    }

    bool MainCameraPass::setupModelDescriptorSet()//分配模型描述符集
    {
        LOG_DEBUG("[setupModelDescriptorSet] Starting, m_render_resource: {}", (void*)m_render_resource.get());
        
        if (!m_render_resource) {
            LOG_ERROR("[setupModelDescriptorSet] m_render_resource is null!");
            return false;
        }
        
        // 直接获取可修改的渲染对象引用，而不是使用副本
        auto& renderObjects = m_render_resource->getLoadedRenderObjectsMutable();
        
        if (renderObjects.empty()) {
            LOG_ERROR("[setupModelDescriptorSet] renderObjects is empty!");
            return false;
        }
        
        // 检查模型描述符集布局是否有效
        if (m_render_pipelines.size() < 3 || !m_render_pipelines[2].descriptorSetLayout) {
            LOG_ERROR("[setupModelDescriptorSet] Model descriptor set layout is not initialized! m_render_pipelines.size()={}, layout={}", 
                     m_render_pipelines.size(), 
                     (m_render_pipelines.size() >= 3) ? (void*)m_render_pipelines[2].descriptorSetLayout : nullptr);
            return false;
        }
        
        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        bool allSuccess = true;
        
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++)
        {
            // 获取UBO信息 (所有对象共享)
            if (!uniformBuffers[frameIndex]) {
                LOG_ERROR("[setupModelDescriptorSet] Uniform buffer is null for frame {}", frameIndex);
                allSuccess = false;
                continue;
            }
            
            RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info = {};
            mesh_perframe_storage_buffer_info.offset = 0;
            mesh_perframe_storage_buffer_info.range  = sizeof(UniformBufferObject);
            mesh_perframe_storage_buffer_info.buffer = uniformBuffers[frameIndex];

            RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info_view = {};
            mesh_perframe_storage_buffer_info_view.offset = 0;
            mesh_perframe_storage_buffer_info_view.range  = sizeof(UniformBufferObjectView);
            mesh_perframe_storage_buffer_info_view.buffer = viewUniformBuffers[frameIndex];
            
            
            // Setup texture bindings dynamically for each render object
            bool hasModelTextures = false;
            if (m_render_resource && !renderObjects.empty()) {
                // 为每一个渲染对象创建独立的描述符集
                for (size_t objIndex = 0; objIndex < renderObjects.size(); ++objIndex) {
                    auto& renderObject = renderObjects[objIndex];
                    
                    // 确保渲染对象有描述符集数组
                    if (renderObject.descriptorSets.size() != maxFramesInFlight) {
                        renderObject.descriptorSets.resize(maxFramesInFlight);
                    }
                    
                    // 检查是否已经分配过描述符集，避免重复分配
                    if (renderObject.descriptorSets[frameIndex] == nullptr) {
                        // 检查描述符集布局是否有效
                        if (m_render_pipelines.size() < 3 || !m_render_pipelines[2].descriptorSetLayout) {
                            LOG_ERROR("[setupModelDescriptorSet] Model descriptor set layout is not initialized! m_render_pipelines.size()={}, layout={}", 
                                     m_render_pipelines.size(), 
                                     (m_render_pipelines.size() >= 3) ? (void*)m_render_pipelines[2].descriptorSetLayout : nullptr);
                            continue;
                        }
                        
                        // 分配当前对象的描述符集 (使用共享的模型描述符布局)
                        RHIDescriptorSetAllocateInfo object_descriptor_set_alloc_info{};
                        object_descriptor_set_alloc_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                        object_descriptor_set_alloc_info.pNext = nullptr;
                        object_descriptor_set_alloc_info.descriptorPool = m_rhi->getDescriptorPoor();
                        object_descriptor_set_alloc_info.descriptorSetCount = 1;
                        object_descriptor_set_alloc_info.pSetLayouts = &m_render_pipelines[2].descriptorSetLayout;

                        if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&object_descriptor_set_alloc_info, 
                                                                         renderObject.descriptorSets[frameIndex])) {
                            LOG_ERROR("[setupModelDescriptorSet] Failed to allocate descriptor set for object {} frame {}", 
                                     objIndex, frameIndex);
                            allSuccess = false;
                            continue;
                        }
                        LOG_INFO("[setupModelDescriptorSet] Successfully allocated descriptor set for object {} frame {}", objIndex, frameIndex);
                    } else {
                        LOG_DEBUG("[setupModelDescriptorSet] Descriptor set already allocated for object {} frame {}, skipping allocation", objIndex, frameIndex);
                    }
                    
                    // 为当前对象创建描述符写入信息
                    
                    std::vector<RHIWriteDescriptorSet> object_descriptor_writes_info(layout_size);
                    std::vector<RHIDescriptorImageInfo> objectImageInfos(layout_size - 3);
                    
                    // Binding 0: UBO (每个对象都需要UBO)
                    object_descriptor_writes_info[0].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    object_descriptor_writes_info[0].pNext = nullptr;
                    object_descriptor_writes_info[0].dstSet = renderObject.descriptorSets[frameIndex];
                    object_descriptor_writes_info[0].dstBinding = 0;
                    object_descriptor_writes_info[0].dstArrayElement = 0;
                    object_descriptor_writes_info[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    object_descriptor_writes_info[0].descriptorCount = 1;
                    object_descriptor_writes_info[0].pBufferInfo = &mesh_perframe_storage_buffer_info;
                    object_descriptor_writes_info[0].pImageInfo = nullptr;
                    object_descriptor_writes_info[0].pTexelBufferView = nullptr;

                    // Binding 1: UBOV
                    object_descriptor_writes_info[1].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    object_descriptor_writes_info[1].pNext = nullptr;
                    object_descriptor_writes_info[1].dstSet = renderObject.descriptorSets[frameIndex];
                    object_descriptor_writes_info[1].dstBinding = 1;
                    object_descriptor_writes_info[1].dstArrayElement = 0;
                    object_descriptor_writes_info[1].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    object_descriptor_writes_info[1].descriptorCount = 1;
                    object_descriptor_writes_info[1].pBufferInfo = &mesh_perframe_storage_buffer_info_view;
                    object_descriptor_writes_info[1].pImageInfo = nullptr;
                    object_descriptor_writes_info[1].pTexelBufferView = nullptr;

                    RHIDescriptorImageInfo CubeMapImageInfo{};
                    CubeMapImageInfo.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    
                    // Add null pointer checks for cubemap resources
                    auto cubemapImageView = m_render_resource->getCubemapImageView();
                    auto cubemapSampler = m_render_resource->getCubemapImageSampler();
                    
                    if (!cubemapImageView || !cubemapSampler) {
                        LOG_ERROR("[setupModelDescriptorSet] Cubemap resources are null (imageView: {}, sampler: {}) for object {} frame {}, skipping model rendering", 
                                 (void*)cubemapImageView, (void*)cubemapSampler, objIndex, frameIndex);
                        // 将描述符集标记为无效，避免在drawModels中使用
                        renderObject.descriptorSets[frameIndex] = nullptr;
                        allSuccess = false;
                        continue;
                    }
                    
                    CubeMapImageInfo.imageView = cubemapImageView;
                    CubeMapImageInfo.sampler = cubemapSampler;
                    
                    

                    object_descriptor_writes_info[2].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    object_descriptor_writes_info[2].dstSet = renderObject.descriptorSets[frameIndex];
                    object_descriptor_writes_info[2].dstBinding = 2;
                    object_descriptor_writes_info[2].dstArrayElement = 0;
                    object_descriptor_writes_info[2].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    object_descriptor_writes_info[2].descriptorCount = 1;
                    object_descriptor_writes_info[2].pImageInfo = &CubeMapImageInfo;

                    // Binding 8: 方向光阴影贴图
                    RHIDescriptorImageInfo shadowMapImageInfo{};
                    shadowMapImageInfo.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    
                    // 获取阴影贴图资源
                    auto shadowMapImageView = m_render_resource->getDirectionalLightShadowImageView();
                    auto shadowMapSampler = m_render_resource->getDirectionalLightShadowImageSampler();
                    
                    if (!shadowMapImageView || !shadowMapSampler) {
                        LOG_ERROR("[setupModelDescriptorSet] Shadow map resources are null (imageView: {}, sampler: {}) for object {} frame {}, skipping model rendering", 
                                 (void*)shadowMapImageView, (void*)shadowMapSampler, objIndex, frameIndex);
                        // 将描述符集标记为无效，避免在drawModels中使用
                        renderObject.descriptorSets[frameIndex] = nullptr;
                        allSuccess = false;
                        continue;
                    }
                    
                    shadowMapImageInfo.imageView = shadowMapImageView;
                    shadowMapImageInfo.sampler = shadowMapSampler;
                    
                    object_descriptor_writes_info[8].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    object_descriptor_writes_info[8].dstSet = renderObject.descriptorSets[frameIndex];
                    object_descriptor_writes_info[8].dstBinding = 8;
                    object_descriptor_writes_info[8].dstArrayElement = 0;
                    object_descriptor_writes_info[8].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    object_descriptor_writes_info[8].descriptorCount = 1;
                    object_descriptor_writes_info[8].pImageInfo = &shadowMapImageInfo;
                    
                    // Binding 9: 光源投影视图矩阵uniform buffer
                    RHIDescriptorBufferInfo lightSpaceMatrixBufferInfo = {};
                    lightSpaceMatrixBufferInfo.offset = 0;
                    lightSpaceMatrixBufferInfo.range = sizeof(glm::mat4);
                    lightSpaceMatrixBufferInfo.buffer = lightSpaceMatrixBuffers[frameIndex];
                    
                    object_descriptor_writes_info[9].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    object_descriptor_writes_info[9].pNext = nullptr;
                    object_descriptor_writes_info[9].dstSet = renderObject.descriptorSets[frameIndex];
                    object_descriptor_writes_info[9].dstBinding = 9;
                    object_descriptor_writes_info[9].dstArrayElement = 0;
                    object_descriptor_writes_info[9].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    object_descriptor_writes_info[9].descriptorCount = 1;
                    object_descriptor_writes_info[9].pBufferInfo = &lightSpaceMatrixBufferInfo;
                    object_descriptor_writes_info[9].pImageInfo = nullptr;
                    object_descriptor_writes_info[9].pTexelBufferView = nullptr;

                    if (!renderObject.textureImageViews.empty() && !renderObject.textureSamplers.empty()) {
                        hasModelTextures = true;
                        
                        // Configure texture bindings (3 to 7, 跳过binding 8和9)
                        for (uint32_t i = 3; i < 8; ++i) {
                            uint32_t textureIndex = (i-3) % renderObject.textureImageViews.size();
                            
                            // Add null pointer checks for texture resources
                            if (!renderObject.textureImageViews[textureIndex]) {
                                LOG_ERROR("[setupModelDescriptorSet] Texture image view {} is null for object {} frame {}", 
                                         textureIndex, objIndex, frameIndex);
                                continue;
                            }
                            if (!renderObject.textureSamplers[textureIndex]) {
                                LOG_ERROR("[setupModelDescriptorSet] Texture sampler {} is null for object {} frame {}", 
                                         textureIndex, objIndex, frameIndex);
                                continue;
                            }
                            
                            // Setup image info
                            objectImageInfos[i - 3].imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            objectImageInfos[i - 3].imageView = renderObject.textureImageViews[textureIndex];
                            objectImageInfos[i - 3].sampler = renderObject.textureSamplers[textureIndex];
                            
                            // Setup descriptor write
                            object_descriptor_writes_info[i].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                            object_descriptor_writes_info[i].pNext = nullptr;
                            object_descriptor_writes_info[i].dstSet = renderObject.descriptorSets[frameIndex];
                            object_descriptor_writes_info[i].dstBinding = i;
                            object_descriptor_writes_info[i].dstArrayElement = 0;
                            object_descriptor_writes_info[i].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                            object_descriptor_writes_info[i].descriptorCount = 1;
                            object_descriptor_writes_info[i].pBufferInfo = nullptr;
                            object_descriptor_writes_info[i].pImageInfo = &objectImageInfos[i - 3];
                            object_descriptor_writes_info[i].pTexelBufferView = nullptr;
                        }
                    } else {
                        LOG_WARN("[setupModelDescriptorSet] Object {} has no textures, skipping texture bindings", objIndex);
                        // 只更新UBO绑定
                        m_rhi->updateDescriptorSets(1, object_descriptor_writes_info.data(), 0, nullptr);
                        continue;
                    }
                    
                    // 更新当前对象的描述符集
                    m_rhi->updateDescriptorSets(layout_size, object_descriptor_writes_info.data(), 0, nullptr);
                    LOG_INFO("[setupModelDescriptorSet] Successfully updated descriptor set for object {} frame {}", objIndex, frameIndex);
                }
            } else {
                LOG_ERROR("[setupModelDescriptorSet] No render resource or loaded render objects available");
                allSuccess = false;
            }
        }
        
        if (allSuccess) {
            LOG_INFO("[setupModelDescriptorSet] All model descriptor sets created successfully");
        } else {
            LOG_ERROR("[setupModelDescriptorSet] Some model descriptor sets failed to create");
        }
        
        return allSuccess;
    }
    
    /**
     * @brief Draw all loaded models using the model rendering pipeline
     * This method renders all RenderObjects loaded by RenderResource
     */
    void MainCameraPass::drawModels(RHICommandBuffer* command_buffer)
    {
        if (!m_render_resource) {
            LOG_WARN("[MainCameraPass::drawModels] No render resource available");
            return;
        }
        
        // Use stored render objects instead of repeatedly accessing render resource
        if (m_loaded_render_objects.empty()) {
            LOG_WARN("[MainCameraPass::drawModels] No loaded render objects available for rendering");
            return;
        }
        
        // LOG_INFO("[MainCameraPass::drawModels] Starting to render {} models", m_loaded_render_objects.size());
        
        // Check if model pipeline is available
        if (m_render_pipelines.size() < 3 || !m_render_pipelines[2].graphicsPipeline) {
            LOG_ERROR("[MainCameraPass::drawModels] Model rendering pipeline not available");
            return;
        }
        
        
        
        // Bind model rendering pipeline
        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[2].graphicsPipeline);

        
        // 获取当前时间用于动画计算
        float currentTime = static_cast<float>(glfwGetTime());
        
        // Render each loaded model using stored data
        for (size_t i = 0; i < m_loaded_render_objects.size(); ++i) {
            const auto& renderObject = m_loaded_render_objects[i];
            
            // 计算每个模型的独立变换矩阵
            glm::mat4 modelMatrix = glm::mat4(1.0f);
            
            // 应用位置变换
            modelMatrix = glm::translate(modelMatrix, renderObject.animationParams.position);
            
            // 应用旋转变换（平台模型保持静止）
            if (renderObject.animationParams.enableAnimation && !renderObject.animationParams.isPlatform) {
                // 非平台模型的动画旋转
                float rotationAngle = currentTime * renderObject.animationParams.rotationSpeed;
                modelMatrix = glm::rotate(modelMatrix, rotationAngle, renderObject.animationParams.rotationAxis);
            }
            // 应用静态旋转
            modelMatrix = glm::rotate(modelMatrix, renderObject.animationParams.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, renderObject.animationParams.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, renderObject.animationParams.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
            
            // 应用缩放变换
            modelMatrix = glm::scale(modelMatrix, renderObject.animationParams.scale);
            
            // 通过Push Constants传递model矩阵到着色器
            // LOG_DEBUG("[MainCameraPass::drawModels] About to call cmdPushConstantsPFN for model {}", i);
            m_rhi->cmdPushConstantsPFN(command_buffer, m_render_pipelines[2].pipelineLayout, 
                                     RHI_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &modelMatrix);
            // LOG_DEBUG("[MainCameraPass::drawModels] cmdPushConstantsPFN completed for model {}", i);
            
            // Bind vertex buffer
            // LOG_DEBUG("[MainCameraPass::drawModels] About to bind vertex buffer for model {}", i);
            if (renderObject.vertexBuffer) {
                RHIBuffer* vertex_buffers[] = {renderObject.vertexBuffer};
                RHIDeviceSize offsets[] = {0};
                m_rhi->cmdBindVertexBuffersPFN(command_buffer, 0, 1, vertex_buffers, offsets);
                // LOG_DEBUG("[MainCameraPass::drawModels] Vertex buffer bound for model {}", i);
            } else {
                LOG_ERROR("[MainCameraPass::drawModels] Model {} has no vertex buffer", i);
                continue;
            }
            
            // Bind index buffer
            // LOG_DEBUG("[MainCameraPass::drawModels] About to bind index buffer for model {}", i);
            if (renderObject.indexBuffer) {
                m_rhi->cmdBindIndexBufferPFN(command_buffer, renderObject.indexBuffer, 0, RHI_INDEX_TYPE_UINT32);
                // LOG_DEBUG("[MainCameraPass::drawModels] Index buffer bound for model {}", i);
            } else {
                LOG_ERROR("[MainCameraPass::drawModels] Model {} has no index buffer", i);
                continue;
            }
            
            // 检查描述符集是否有效
            uint32_t currentFrameIndex = m_rhi->getCurrentFrameIndex();
            if (currentFrameIndex >= renderObject.descriptorSets.size() || 
                renderObject.descriptorSets[currentFrameIndex] == nullptr) {
                LOG_WARN("[MainCameraPass::drawModels] Model {} has invalid descriptor set for frame {}, skipping", i, currentFrameIndex);
                continue;
            }
            
            // 绑定模型渲染的描述符集
            // LOG_DEBUG("[MainCameraPass::drawModels] About to bind descriptor sets for model {}", i);
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, 
                                          m_render_pipelines[2].pipelineLayout, 0, 1, 
                                          &renderObject.descriptorSets[currentFrameIndex], 0, nullptr);
            // LOG_DEBUG("[MainCameraPass::drawModels] Descriptor sets bound for model {}", i);
            
            // Draw the model
            // LOG_DEBUG("[MainCameraPass::drawModels] About to draw model {} with {} indices", i, renderObject.indices.size());
            if (!renderObject.indices.empty()) {
                m_rhi->cmdDrawIndexedPFN(command_buffer, static_cast<uint32_t>(renderObject.indices.size()), 1, 0, 0, 0);
                // LOG_DEBUG("[MainCameraPass::drawModels] Model {} drawn successfully", i);
            } else {
                LOG_WARN("[MainCameraPass::drawModels] Model {} has no indices to render", i);
            }
        }
        

    }

    void MainCameraPass::setupFramebufferDescriptorSet(){
        

        // 分配帧缓冲区的描述符集
        // RHIDescriptorSetAllocateInfo allocInfo{};
        // allocInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        // allocInfo.descriptorPool = m_rhi->getDescriptorPoor();
        // allocInfo.pNext = NULL;
        // allocInfo.descriptorSetCount = 1;
        // allocInfo.pSetLayouts = &m_descriptor_infos[0].layout;

        // if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&allocInfo, m_descriptor_infos[0].descriptor_set))
        // {
        //     throw std::runtime_error("allocate framebuffer descriptor set");
        // }
    }
    /**
     * @brief 设置背景三角形的描述符集。
     * 尽管背景三角形着色器不需要任何特定的描述符（例如纹理或Uniform Buffer），
     * 但为了保持渲染管线的一致性，仍然会分配一个空的描述符集。
     * 此函数确保描述符集已分配，但不需要进行任何更新操作，因为其布局是空的。
     */
   
    //帧缓冲
    void MainCameraPass::setupSwapchainFramebuffers()
    {
        



        size_t imageViewCount = m_rhi->getSwapchainInfo().imageViews.size();

        m_swapchain_framebuffers.resize(imageViewCount);

        // create frame buffer for every imageview，包含颜色和深度附件
        for (size_t i = 0; i < imageViewCount; i++)
        {
            RHIImageView* framebuffer_attachments_for_image_view[2] = {
                m_rhi->getSwapchainInfo().imageViews[i],  // 颜色附件
                m_rhi->getDepthImageInfo().depth_image_view  // 使用全局深度缓冲
            };
            RHIFramebufferCreateInfo framebuffer_create_info {};
            framebuffer_create_info.sType      = RHI_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.flags      = 0U;
            framebuffer_create_info.renderPass = m_framebuffer.render_pass;
            framebuffer_create_info.attachmentCount = 2;  // 颜色 + 深度
            framebuffer_create_info.pAttachments = framebuffer_attachments_for_image_view;
            framebuffer_create_info.width = m_rhi->getSwapchainInfo().extent.width;
        framebuffer_create_info.height = m_rhi->getSwapchainInfo().extent.height;

            framebuffer_create_info.layers       = 1;

            m_swapchain_framebuffers[i] = new RHIFramebuffer();
            
            if (RHI_SUCCESS != RenderPassBase::m_rhi->createFramebuffer(&framebuffer_create_info, m_swapchain_framebuffers[i]))
            {
                LOG_ERROR("Failed to create framebuffer for image view " + std::to_string(i));
                throw std::runtime_error("create main camera framebuffer");
            }
        }
    }
    //图片资源
    void MainCameraPass::setupBackgroundTexture(){

            createTextureImage();		// 创建贴图资源
            // createTextureImageView();	// 创建着色器中引用的贴图View
            createTextureSampler();		// 创建着色器中引用的贴图采样器
    }
    void MainCameraPass::createTextureImage(){		// 创建贴图资源
        // 使用资产管理器解析纹理路径
        std::string texturePath = AssetManager::getInstance().resolveAssetPath("textures/background.png");
        
        if (texturePath.empty()) {
            LOG_WARN("Background texture not found, using default white texture");
        } else {
            LOG_DEBUG("Background texture resolved to: {}", texturePath);
        }
        
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = nullptr;
        
        if (!texturePath.empty()) {
            pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        }
        
        if (!pixels) {
           LOG_ERROR("Failed to load texture: {}", texturePath.empty() ? "textures/background.png" : texturePath);
           // 创建一个默认的1x1白色纹理
           texWidth = 1;
           texHeight = 1;
           uint8_t defaultPixels[4] = {255, 255, 255, 255}; // 白色RGBA
           
           VmaAllocation textureImageAllocation;
           m_rhi->createGlobalImage(textureImage, textureImageView, textureImageAllocation,
                                   static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 
                                   defaultPixels, RHI_FORMAT_R8G8B8A8_SRGB, 1);
        } else {
            // 成功加载图片，使用实际图片数据
            
            
            VmaAllocation textureImageAllocation;
            m_rhi->createGlobalImage(textureImage, textureImageView, textureImageAllocation,
                                    static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 
                                    pixels, RHI_FORMAT_R8G8B8A8_SRGB, 1);
            
            // 释放stb_image分配的内存
            stbi_image_free(pixels);
        }
        
        // 注意：createGlobalImage已经创建了textureImageView，所以不需要单独调用createTextureImageView
        // textureImageMemory在使用VMA时不需要手动管理
        textureImageMemory = nullptr;
    }
    void MainCameraPass::createTextureImageView(){	// 创建着色器中引用的贴图View
        // 注意：当使用createGlobalImage时，textureImageView已经被创建
        // 如果textureImageView为空，则手动创建
        if (textureImageView == nullptr) {
            m_rhi->createImageView(textureImage, 
                                  RHI_FORMAT_R8G8B8A8_SRGB, 
                                  RHI_IMAGE_ASPECT_COLOR_BIT, 
                                  RHI_IMAGE_VIEW_TYPE_2D,
                                  1, 1,
                                  textureImageView);
        }
    }
    void MainCameraPass::createTextureSampler(){		// 创建着色器中引用的贴图采样器
        // 创建纹理采样器
        RHISamplerCreateInfo samplerInfo{};
        samplerInfo.sType = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = RHI_FILTER_LINEAR;
        samplerInfo.minFilter = RHI_FILTER_LINEAR;
        samplerInfo.addressModeU = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = RHI_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = RHI_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = RHI_FALSE;
        samplerInfo.compareEnable = RHI_FALSE;
        samplerInfo.compareOp = RHI_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        
        m_rhi->createSampler(&samplerInfo, textureSampler);
    }
    // 直接绘制方式不需要顶点缓冲区
    /*
    void MainCameraPass::createVertexBuffer(){		// 创建VertexBuffer顶点缓存区
        // 获取当前窗口尺寸以计算纹理坐标的平铺比例
        auto swapchainInfo = m_rhi->getSwapchainInfo();
        float windowWidth = static_cast<float>(swapchainInfo.extent.width);
        float windowHeight = static_cast<float>(swapchainInfo.extent.height);
        float aspectRatio = windowWidth / windowHeight;
        
        // 假设背景图片的原始长宽比为16:9（可以根据实际图片调整）
        float imageAspectRatio = 16.0f / 9.0f;
        
        // 计算纹理坐标的平铺比例
        float texScaleX = 1.0f;
        float texScaleY = 1.0f;
        
        if (aspectRatio > imageAspectRatio) {
            // 窗口比图片更宽，需要在X方向平铺
            texScaleX = aspectRatio / imageAspectRatio;
        } else {
            // 窗口比图片更高，需要在Y方向平铺
            texScaleY = imageAspectRatio / aspectRatio;
        }
        
        // 定义覆盖整个屏幕的正方形顶点数据
        const std::vector<Vertex> vertices = {
            // 位置                    颜色                纹理坐标（带平铺）      法线
            {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}}, // 左下
            {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {texScaleX, 0.0f}}, // 右下
            {{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {texScaleX, texScaleY}}, // 右上
            {{-1.0f,  1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, texScaleY}}  // 左上
        };
        
        RHIDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        
        // 创建暂存缓冲区
        RHIBuffer* stagingBuffer = nullptr;
        RHIDeviceMemory* stagingBufferMemory = nullptr;
        
        m_rhi->createBuffer(bufferSize,
                           RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           stagingBuffer,
                           stagingBufferMemory);
        
        // 将顶点数据复制到暂存缓冲区
        void* data;
        m_rhi->mapMemory(stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        m_rhi->unmapMemory(stagingBufferMemory);
        
        // 创建顶点缓冲区
        m_rhi->createBuffer(bufferSize,
                           RHI_BUFFER_USAGE_TRANSFER_DST_BIT | RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           vertexBuffer,
                           vertexBufferMemory);
        
        // 从暂存缓冲区复制到顶点缓冲区
        m_rhi->copyBuffer(stagingBuffer, vertexBuffer, 0, 0, bufferSize);
        
        // 清理暂存缓冲区
        m_rhi->destroyBuffer(stagingBuffer);
        m_rhi->freeMemory(stagingBufferMemory);
	}
    */
    // 直接绘制方式不需要索引缓冲区
    /*
	void MainCameraPass::createIndexBuffer(){
        // 定义正方形的索引数据（两个三角形）
        const std::vector<uint16_t> indices = {
            0, 1, 2, 2, 3, 0  // 第一个三角形：0,1,2  第二个三角形：2,3,0
        };
        
        RHIDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
        
        // 创建暂存缓冲区
        RHIBuffer* stagingBuffer = nullptr;
        RHIDeviceMemory* stagingBufferMemory = nullptr;
        
        m_rhi->createBuffer(bufferSize,
                           RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           stagingBuffer,
                           stagingBufferMemory);
        
        // 将索引数据复制到暂存缓冲区
        void* data;
        m_rhi->mapMemory(stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
        m_rhi->unmapMemory(stagingBufferMemory);
        
        // 创建索引缓冲区
        m_rhi->createBuffer(bufferSize,
                           RHI_BUFFER_USAGE_TRANSFER_DST_BIT | RHI_BUFFER_USAGE_INDEX_BUFFER_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           indexBuffer,
                           indexBufferMemory);
        
        // 从暂存缓冲区复制到索引缓冲区
        m_rhi->copyBuffer(stagingBuffer, indexBuffer, 0, 0, bufferSize);
        
        // 清理暂存缓冲区
        m_rhi->destroyBuffer(stagingBuffer);
        m_rhi->freeMemory(stagingBufferMemory);
    };		// 创建IndexBuffer顶点点序缓存区
    */    
    void MainCameraPass::createUniformBuffers(){
       
        RHIDeviceSize bufferSizeOfMesh = sizeof(UniformBufferObject);
        
        // 为每个飞行中的帧创建独立的uniform缓冲区，确保帧之间完全隔离
        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        uniformBuffers.resize(maxFramesInFlight);
        uniformBuffersMemory.resize(maxFramesInFlight);
        
        for (size_t i = 0; i < maxFramesInFlight; i++) {
            // 为每个帧创建专属的uniform buffer和内存
            m_rhi->createBuffer(bufferSizeOfMesh,
                               RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               uniformBuffers[i],
                               uniformBuffersMemory[i]);
        };
         RHIDeviceSize bufferSizeOfView = sizeof(UniformBufferObjectView);
        
        // 为每个飞行中的帧创建独立的uniform缓冲区，确保帧之间完全隔离
        viewUniformBuffers.resize(maxFramesInFlight);
        viewUniformBuffersMemory.resize(maxFramesInFlight);
        for (size_t i = 0; i < maxFramesInFlight; i++) {
            // 为每个帧创建专属的uniform buffer和内存
            m_rhi->createBuffer(bufferSizeOfView,
                               RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               viewUniformBuffers[i],
                               viewUniformBuffersMemory[i]);
        }
        
        // 创建光源投影视图矩阵uniform buffer
        RHIDeviceSize lightSpaceMatrixBufferSize = sizeof(glm::mat4);
        lightSpaceMatrixBuffers.resize(maxFramesInFlight);
        lightSpaceMatrixBuffersMemory.resize(maxFramesInFlight);
        for (size_t i = 0; i < maxFramesInFlight; i++) {
            m_rhi->createBuffer(lightSpaceMatrixBufferSize,
                               RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               lightSpaceMatrixBuffers[i],
                               lightSpaceMatrixBuffersMemory[i]);
        }
    };		// 创建UnifromBuffer统一缓存区，为每个飞行中的帧分配独立缓冲区
    
    void MainCameraPass::updateUniformBuffer(uint32_t currentFrameIndex) {
        // 添加基本的空指针检查
        if (!m_rhi) {
            LOG_ERROR("[MainCameraPass::updateUniformBuffer] RHI is null");
            return;
        }
        
        // 确保帧索引有效
        if (currentFrameIndex >= uniformBuffers.size()) {
            LOG_WARN("[MainCameraPass::updateUniformBuffer] Invalid frame index: {} >= {}", currentFrameIndex, uniformBuffers.size());
            return;
        }
        
        // 检查uniform buffer是否有效
        if (currentFrameIndex >= uniformBuffersMemory.size() || !uniformBuffersMemory[currentFrameIndex]) {
            LOG_ERROR("[MainCameraPass::updateUniformBuffer] Uniform buffer memory is null for frame {}", currentFrameIndex);
            return;
        }
        
        // 创建VP矩阵 (View-Projection, model矩阵现在通过Push Constants传递)
        UniformBufferObject ubo{};
        // 获取当前窗口尺寸
        auto swapchainInfo = m_rhi->getSwapchainInfo();
        float windowWidth = static_cast<float>(swapchainInfo.extent.width);
        float windowHeight = static_cast<float>(swapchainInfo.extent.height);
        float aspectRatio = windowWidth / windowHeight;

        // 使用RenderCamera计算视图矩阵和投影矩阵
        if (m_camera) {
            // 从相机获取视图矩阵和投影矩阵
            ubo.view = m_camera->getViewMatrix();
            ubo.proj = m_camera->getPersProjMatrix();
        } else {
            // 如果相机未初始化，使用默认矩阵
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 10.0f);
        }
        // ubo.proj=glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);

        // 注意：RenderCamera的getPersProjMatrix()已经处理了Vulkan的Y轴翻转
        // 因此这里不需要再次翻转Y轴
        // ubo.proj[1][1] *= -1; // 已在RenderCamera中处理

         // 将数据复制到当前帧的uniform buffer
        void* data = nullptr;
        if (m_rhi->mapMemory(uniformBuffersMemory[currentFrameIndex], 0, sizeof(ubo), 0, &data) != RHI_SUCCESS || !data) {
            LOG_ERROR("[MainCameraPass::updateUniformBuffer] Failed to map uniform buffer memory for frame {}", currentFrameIndex);
            return;
        }
        memcpy(data, &ubo, sizeof(ubo));
        m_rhi->unmapMemory(uniformBuffersMemory[currentFrameIndex]);

        UniformBufferObjectView ubv{};
        Light light;
        
        // 🔧 直接从RenderResource获取方向光源数据
        glm::vec3 light_position;
        glm::vec3 light_direction;
        glm::vec3 light_color;
        float light_intensity;
        
        // 从RenderResource获取主要方向光源
        auto primary_light = m_render_resource->getPrimaryDirectionalLight();
        if (primary_light)
        {
            auto gpu_data = primary_light->toGPUData();
            light_position = glm::vec3(gpu_data.position);
            light_direction = glm::vec3(gpu_data.direction);
            light_color = glm::vec3(gpu_data.color);
            light_intensity = gpu_data.color.w; // 强度存储在颜色的w分量中
            // LOG_DEBUG("[MainCamera] Using light from RenderResource: Position ({:.2f}, {:.2f}, {:.2f}), Intensity {:.2f}", 
            //          light_position.x, light_position.y, light_position.z, light_intensity);
        }
        else
        {
            // 回退到默认光源
            light_position = glm::vec3(2.0f, 4.0f, -1.0f);
            light_direction = glm::vec3(-0.2f, -1.0f, -0.3f);
            light_color = glm::vec3(1.0f, 1.0f, 1.0f);
            light_intensity = 3.0f;
            LOG_WARN("[MainCamera] No directional light found in RenderResource, using default light");
        }
        
        light.position = glm::vec4(light_position, 0.0); // w=0表示方向光
        light.color = glm::vec4(light_color, light_intensity);
        light.direction = glm::vec4(light_direction, 0.0);
        light.info = glm::vec4(0.0, 0.0, 0.0, 0.0);
        ubv.directional_lights[0] = light;
        ubv.lights_count = glm::ivec4(1, 0, 0, 1);//todo
        
        // 使用RenderCamera的实际位置
        if (m_camera) {
            glm::vec3 camera_pos = m_camera->position();
            glm::vec2 fov = m_camera->getFOV();
            ubv.camera_position = glm::vec4(camera_pos.x, camera_pos.y, camera_pos.z, fov.x);
        } else {
            ubv.camera_position = glm::vec4(2.0, 2.0, 2.0, 45.0);
        }

        // 检查view uniform buffer是否有效
        if (currentFrameIndex >= viewUniformBuffersMemory.size() || !viewUniformBuffersMemory[currentFrameIndex]) {
            LOG_ERROR("[MainCameraPass::updateUniformBuffer] View uniform buffer memory is null for frame {}", currentFrameIndex);
            return;
        }
        
        void* data_view = nullptr;
        if (m_rhi->mapMemory(viewUniformBuffersMemory[currentFrameIndex], 0, sizeof(ubv), 0, &data_view) != RHI_SUCCESS || !data_view) {
            LOG_ERROR("[MainCameraPass::updateUniformBuffer] Failed to map view uniform buffer memory for frame {}", currentFrameIndex);
            return;
        }
        memcpy(data_view, &ubv, sizeof(ubv));
        m_rhi->unmapMemory(viewUniformBuffersMemory[currentFrameIndex]);
        
        // 更新光源投影视图矩阵uniform buffer
        if (m_directional_light_shadow_pass && m_render_resource) {
            // 🔧 阴影通道现在直接从 RenderResource 获取光源数据
            // 重新计算光源矩阵以反映新的光源位置
            try {
                m_directional_light_shadow_pass->updateLightMatrix(m_render_resource);
                
                glm::mat4 lightSpaceMatrix = m_directional_light_shadow_pass->getLightProjectionViewMatrix();
                
                // 检查light space matrix buffer是否有效
                if (currentFrameIndex >= lightSpaceMatrixBuffersMemory.size() || !lightSpaceMatrixBuffersMemory[currentFrameIndex]) {
                    LOG_ERROR("[MainCameraPass::updateUniformBuffer] Light space matrix buffer memory is null for frame {}", currentFrameIndex);
                    return;
                }
                
                void* lightSpaceData = nullptr;
                if (m_rhi->mapMemory(lightSpaceMatrixBuffersMemory[currentFrameIndex], 0, sizeof(lightSpaceMatrix), 0, &lightSpaceData) != RHI_SUCCESS || !lightSpaceData) {
                    LOG_ERROR("[MainCameraPass::updateUniformBuffer] Failed to map light space matrix buffer memory for frame {}", currentFrameIndex);
                    return;
                }
                memcpy(lightSpaceData, &lightSpaceMatrix, sizeof(lightSpaceMatrix));
                m_rhi->unmapMemory(lightSpaceMatrixBuffersMemory[currentFrameIndex]);
            } catch (const std::exception& e) {
                LOG_ERROR("[MainCameraPass::updateUniformBuffer] Exception in shadow pass update: {}", e.what());
            }
        } else {
            if (!m_directional_light_shadow_pass) {
                LOG_WARN("[MainCameraPass::updateUniformBuffer] Directional light shadow pass is null");
            }
            if (!m_render_resource) {
                LOG_WARN("[MainCameraPass::updateUniformBuffer] Render resource is null");
            }
        }
       
    }
     void MainCameraPass::updateAfterFramebufferRecreate()
    {
        // 记录当前交换链信息
        auto swapchainInfo = m_rhi->getSwapchainInfo();
        auto depthInfo = m_rhi->getDepthImageInfo();
            
        // 只清理颜色附件，深度附件由VulkanRHI管理
        for (size_t i = 0; i < m_framebuffer.attachments.size(); i++)
        {
            if (m_framebuffer.attachments[i].image != nullptr)
            {
                m_rhi->destroyImage(m_framebuffer.attachments[i].image);
                m_framebuffer.attachments[i].image = nullptr;
            }
            if (m_framebuffer.attachments[i].view != nullptr)
            {
                m_rhi->destroyImageView(m_framebuffer.attachments[i].view);
                m_framebuffer.attachments[i].view = nullptr;
            }
            if (m_framebuffer.attachments[i].mem != nullptr)
            {
                m_rhi->freeMemory(m_framebuffer.attachments[i].mem);
                m_framebuffer.attachments[i].mem = nullptr;
            }
        }


        for (auto framebuffer : m_swapchain_framebuffers)
        {
            if (framebuffer != nullptr)
            {
                m_rhi->destroyFramebuffer(framebuffer);
            }
        }
        m_swapchain_framebuffers.clear();


        setupAttachments();


        setupBackgroundDescriptorSet();


        setupSwapchainFramebuffers();

        
        // 验证重建后的资源
        swapchainInfo = m_rhi->getSwapchainInfo();
        depthInfo = m_rhi->getDepthImageInfo();
    }

    /**
     * @brief 绘制UI内容（在UI子通道中执行）
     * @param command_buffer 命令缓冲区
     */
    /**
     * @brief 在UI子通道中绘制UI内容
     * @details 此函数在主相机渲染通道的UI子通道中被调用，确保UI在正确的渲染通道内渲染
     * @param command_buffer 当前的命令缓冲区
     */
    void MainCameraPass::drawUI(RHICommandBuffer* command_buffer)
    {
        // 获取UI Pass并执行绘制
        auto render_system = g_runtime_global_context.m_render_system;
        if (render_system)
        {
            auto render_pipeline_base = render_system->getRenderPipeline();
            if (render_pipeline_base)
            {
                // 将基类指针转换为具体的RenderPipeline类型
                auto render_pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_pipeline_base);
                if (render_pipeline)
                {
                    auto ui_pass = render_pipeline->getUIPass();
                    if (ui_pass)
                    {
                        // 在UI子通道中绘制UI内容
                        // 注意：此时已经在活跃的渲染通道内，ImGui可以正常渲染
                        ui_pass->draw(command_buffer);
                    }
                    else
                    {
                        LOG_WARN("[MainCameraPass] UI Pass is null, skipping UI rendering");
                    }
                }
                else
                {
                    LOG_WARN("[MainCameraPass] Failed to cast render pipeline, skipping UI rendering");
                }
            }
            else
            {
                LOG_WARN("[MainCameraPass] Render pipeline is null, skipping UI rendering");
            }
        }
        else
        {
            LOG_WARN("[MainCameraPass] Render system is null, skipping UI rendering");
        }

        // 更新相机的宽高比以适应新的窗口尺寸
        if (m_camera) {
            auto swapchainInfo = m_rhi->getSwapchainInfo();
            float windowWidth = static_cast<float>(swapchainInfo.extent.width);
            float windowHeight = static_cast<float>(swapchainInfo.extent.height);
            float aspectRatio = windowWidth / windowHeight;
            m_camera->setAspect(aspectRatio);
        }
    }
    
    // 旧的光源系统方法已移除，现在使用 RenderResource 管理光源
    
    // 旧的兼容性光源接口已移除

    /**
     * @brief 设置背景绘制启用状态
     * @param enabled 是否启用背景绘制
     */
    void MainCameraPass::setBackgroundEnabled(bool enabled)
    {
        m_enable_background = enabled;
    }

    /**
     * @brief 获取背景绘制启用状态
     * @return 背景绘制是否启用
     */
    bool MainCameraPass::isBackgroundEnabled() const
    {
        return m_enable_background;
    }

    /**
     * @brief 设置天空盒绘制启用状态
     * @param enabled 是否启用天空盒绘制
     */
    void MainCameraPass::setSkyboxEnabled(bool enabled)
    {
        if (m_enable_skybox != enabled) {
            LOG_INFO("[Skybox] Skybox rendering state changed: {} -> {}", 
                     m_enable_skybox ? "enabled" : "disabled", 
                     enabled ? "enabled" : "disabled");
        }
        m_enable_skybox = enabled;
    }

    /**
     * @brief 获取天空盒绘制启用状态
     * @return 天空盒绘制是否启用
     */
    bool MainCameraPass::isSkyboxEnabled() const
    {
        return m_enable_skybox;
    }
}