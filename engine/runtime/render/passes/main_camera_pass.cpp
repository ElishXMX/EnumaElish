#include "main_camera_pass.h"
#include "../../core/base/macro.h"
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
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>

#include "../../shader/generated/cpp/pic_vert.h"
#include "../../shader/generated/cpp/pic_frag.h"

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
        
        // Store loaded render objects to avoid repeated access
        if (m_render_resource) {
            m_loaded_render_objects = m_render_resource->getLoadedRenderObjects();
            
            
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
                if (m_render_pipelines.size() >= 2) {
                    const auto& modelPipelineResource = m_render_resource->getModelPipelineResource();
                    m_render_pipelines[1].pipelineLayout = modelPipelineResource.pipelineLayout;
                    m_render_pipelines[1].graphicsPipeline = modelPipelineResource.graphicsPipeline;
                    m_render_pipelines[1].descriptorSetLayout = modelPipelineResource.descriptorSetLayout;
                }
            }
        }
        
        // Setup model descriptor set now that textures are available (only once)
        if (!m_loaded_render_objects.empty() && !m_model_descriptor_sets_initialized) {
            setupModelDescriptorSet();
            m_model_descriptor_sets_initialized = true;
        }
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
        m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
        m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);
        
        // 渲染背景
        drawBackground(command_buffer);
        
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
        
        m_render_pipelines.resize(2);  // Resize to accommodate both background and model pipelines  

                
        
        
        
        RHIDescriptorSetLayout*      descriptorset_layouts[1] = {m_descriptor_infos[0].layout};

        RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
        pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

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

    void MainCameraPass::setupModelDescriptorSet()//分配模型描述符集
    {
        
        uint32_t maxFramesInFlight = m_rhi->getMaxFramesInFlight();
        
        
        
        for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; frameIndex++)
        {
            // 获取UBO信息 (所有对象共享)
            if (!uniformBuffers[frameIndex]) {
                LOG_ERROR("[setupModelDescriptorSet] Uniform buffer is null for frame {}", frameIndex);
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
            if (m_render_resource && !m_loaded_render_objects.empty()) {
                // 为每一个渲染对象创建独立的描述符集
                for (size_t objIndex = 0; objIndex < m_loaded_render_objects.size(); ++objIndex) {
                    auto& renderObject = m_loaded_render_objects[objIndex];
                    
                    // 分配当前对象的描述符集 (使用共享的模型描述符布局)
                    RHIDescriptorSetAllocateInfo object_descriptor_set_alloc_info{};
                    object_descriptor_set_alloc_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    object_descriptor_set_alloc_info.pNext = nullptr;
                    object_descriptor_set_alloc_info.descriptorPool = m_rhi->getDescriptorPoor();
                    object_descriptor_set_alloc_info.descriptorSetCount = 1;
                    object_descriptor_set_alloc_info.pSetLayouts = &m_render_pipelines[1].descriptorSetLayout;

                    // 确保渲染对象有描述符集数组
                    if (renderObject.descriptorSets.size() != maxFramesInFlight) {
                        renderObject.descriptorSets.resize(maxFramesInFlight);
                    }

                    if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&object_descriptor_set_alloc_info, 
                                                                     renderObject.descriptorSets[frameIndex])) {
                        LOG_ERROR("[setupModelDescriptorSet] Failed to allocate descriptor set for object {} frame {}", 
                                 objIndex, frameIndex);
                        continue;
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
                    
                    if (!cubemapImageView) {
                        LOG_ERROR("[setupModelDescriptorSet] Cubemap image view is null for object {} frame {}", objIndex, frameIndex);
                        continue;
                    }
                    
                    if (!cubemapSampler) {
                        LOG_ERROR("[setupModelDescriptorSet] Cubemap sampler is null for object {} frame {}", objIndex, frameIndex);
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

                    if (!renderObject.textureImageViews.empty() && !renderObject.textureSamplers.empty()) {
                        hasModelTextures = true;
                        
                        // Configure texture bindings (3 to layout_size-1)
                        for (uint32_t i = 3; i < layout_size; ++i) {
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
                }
            } else {
                LOG_ERROR("[setupModelDescriptorSet] No render resource or loaded render objects available");
            }
            
            if (!hasModelTextures) {
                LOG_ERROR("[setupModelDescriptorSet] No model textures available, this should not happen");
                throw std::runtime_error("Model textures not available when setting up model descriptor set");
            }
           
           
        }
        
        
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
        if (m_render_pipelines.size() < 2 || !m_render_pipelines[1].graphicsPipeline) {
            LOG_ERROR("[MainCameraPass::drawModels] Model rendering pipeline not available");
            return;
        }
        
        
        
        // Bind model rendering pipeline
        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[1].graphicsPipeline);

        
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
            m_rhi->cmdPushConstantsPFN(command_buffer, m_render_pipelines[1].pipelineLayout, 
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
            
            // 绑定模型渲染的描述符集
            // LOG_DEBUG("[MainCameraPass::drawModels] About to bind descriptor sets for model {}", i);
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, 
                                          m_render_pipelines[1].pipelineLayout, 0, 1, 
                                          &renderObject.descriptorSets[m_rhi->getCurrentFrameIndex()], 0, nullptr);
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
        // 尝试从content/textures目录加载图片
        std::string texturePath = "../engine/runtime/content/textures/background.png";
        
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        
        if (!pixels) {
           LOG_ERROR("Failed to load texture: " + texturePath);
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
    };		// 创建UnifromBuffer统一缓存区，为每个飞行中的帧分配独立缓冲区
    
    void MainCameraPass::updateUniformBuffer(uint32_t currentFrameIndex) {
        // 确保帧索引有效
        if (currentFrameIndex >= uniformBuffers.size()) {
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
        void* data;
        m_rhi->mapMemory(uniformBuffersMemory[currentFrameIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        m_rhi->unmapMemory(uniformBuffersMemory[currentFrameIndex]);

        UniformBufferObjectView ubv{};
        Light light;
        light.position = glm::vec4(2.0, 0.0, 2.0, 0.0);
        light.color = glm::vec4(1.0, 1.0, 1.0, 3.0);
        light.direction = glm::vec4(-2.0, 0.0, -2.0, 0.0);
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

        void* data_view;
         m_rhi->mapMemory(viewUniformBuffersMemory[currentFrameIndex], 0, sizeof(ubv), 0, &data_view);
        memcpy(data_view, &ubv, sizeof(ubv));
        m_rhi->unmapMemory(viewUniformBuffersMemory[currentFrameIndex]);
       
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
                        ui_pass->drawInSubpass(command_buffer);
                    }
                }
            }
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
}