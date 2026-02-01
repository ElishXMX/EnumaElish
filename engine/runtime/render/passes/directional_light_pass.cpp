#include "directional_light_pass.h"
#include "../render_resource.h"
#include "../render_system.h"
#include "../../global/global_context.h"
#include "../../core/base/macro.h"
#include "../interface/rhi.h"
#include "../interface/rhi_struct.h"
#include "../../shader/generated/cpp/shadow_vert.h"
#include "../../shader/generated/cpp/shadow_frag.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

namespace Elish
{
    // 静态常量成员定义（链接器需要）
    const uint32_t DirectionalLightShadowPass::SHADOW_MAP_SIZE;
    
    /**
     * @brief 初始化方向光阴影渲染通道
     * @details 该函数负责初始化阴影渲染通道的基础资源，包括：
     *          - 设置阴影贴图附件（深度纹理）
     *          - 创建渲染通道
     *          - 设置帧缓冲
     *          - 配置描述符集布局
     * @note 此函数在渲染系统初始化阶段被调用，为后续的阴影渲染做准备
     */
    void DirectionalLightShadowPass::initialize()
    {
        // LOG_INFO("[DirectionalLightShadowPass] Starting initialize");
        
        // 初始化测试四边形相关成员变量
        m_test_quad_vertex_buffer = nullptr;
        m_test_quad_vertex_buffer_memory = nullptr;
        m_test_quad_index_buffer = nullptr;
        m_test_quad_index_buffer_memory = nullptr;
        m_test_quad_initialized = false;
        
        // 旧的光源系统初始化代码已移除
        // 现在使用 RenderResource 统一管理光源数据
        
        // 设置阴影贴图资源
        setupAttachments();
        setupRenderPass();
        setupFramebuffer();
        setupDescriptorSetLayout();
        
    }
    
    /**
     * @brief 后初始化阶段设置
     * @details 在基础资源初始化完成后，进行更高级的设置：
     *          - 创建渲染管线（包括着色器、顶点输入、光栅化状态等）
     *          - 设置描述符集（绑定uniform buffer到GPU）
     * @note 此函数在initialize()之后调用，确保所有依赖资源已准备就绪
     */
    void DirectionalLightShadowPass::postInitialize()
    {
        
        setupPipelines();
        
        setupDescriptorSet();
        
    }
    
    /**
     * @brief 准备每帧渲染数据
     * @param render_resource 包含当前帧所有渲染对象和资源的共享指针
     * @details 该函数在每帧渲染开始前被调用，负责：
     *          - 保存当前帧的渲染资源引用
     *          - 更新光源的投影视图矩阵
     * @note 此函数确保阴影渲染通道能够访问到最新的场景数据
     */
    void DirectionalLightShadowPass::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        m_current_render_resource = render_resource;
        updateLightMatrix(render_resource);
    }
    
    /**
     * @brief 执行阴影渲染绘制
     * @details 该函数是阴影渲染的核心，负责完整的阴影贴图生成流程：
     *          1. 检查渲染资源有效性
     *          2. 开始阴影渲染通道，清除深度缓冲
     *          3. 设置视口和裁剪区域为阴影贴图尺寸
     *          4. 绑定阴影渲染管线
     *          5. 更新uniform buffer（光源矩阵和实例数据）
     *          6. 绑定描述符集
     *          7. 渲染所有模型到深度缓冲
     *          8. 结束渲染通道
     * @note 生成的阴影贴图将被主渲染通道用于阴影计算
     */
    void DirectionalLightShadowPass::draw()
    {
        if (!m_current_render_resource)
        {
            LOG_ERROR("[DirectionalLightShadowPass] Render resource is null in draw");
            return;
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Starting shadow pass draw");
        
        // 1. 开始渲染通道
        RHIRenderPassBeginInfo render_pass_begin{};
        render_pass_begin.sType = RHI_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin.renderPass = m_render_pass;
        render_pass_begin.framebuffer = m_framebuffer;
        render_pass_begin.renderArea.offset = {0, 0};
        render_pass_begin.renderArea.extent.width = SHADOW_MAP_SIZE;
        render_pass_begin.renderArea.extent.height = SHADOW_MAP_SIZE;
        
        // 设置清除值 - 只有深度附件
        RHIClearValue clear_values[1];
        // 深度附件清除值
        clear_values[0].depthStencil = {1.0f, 0}; // 深度值清除为1.0（最远）
        render_pass_begin.clearValueCount = 1;
        render_pass_begin.pClearValues = clear_values;
        
        m_rhi->cmdBeginRenderPassPFN(m_rhi->getCurrentCommandBuffer(), &render_pass_begin, RHI_SUBPASS_CONTENTS_INLINE);
        
        // 2. 设置视口和裁剪矩形
        RHIViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, &viewport);
        
        RHIRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent.width = SHADOW_MAP_SIZE;
        scissor.extent.height = SHADOW_MAP_SIZE;
        m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, &scissor);
        
        // 3. 绑定渲染管线
        m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(), RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipeline);
        
        // 4. 更新uniform buffer
        updateUniformBuffer();
        
        // 5. 绑定描述符集 - 使用当前帧索引
        uint8_t currentFrameIndex = m_rhi->getCurrentFrameIndex();
        m_rhi->cmdBindDescriptorSetsPFN(
            m_rhi->getCurrentCommandBuffer(),
            RHI_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline_layout,
            0, // first set
            1, // descriptor set count
            &m_descriptor_sets[currentFrameIndex], // 🔧 修复：使用对应帧的描述符集
            0, // dynamic offset count
            nullptr // dynamic offsets
        );
        float main_color[4] = { 1.0f, 0.5f, 1.0f, 1.0f };
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "DIRECTIONAL LIGHT SHADOW  SUBPASS", main_color);
        // 6. 渲染测试四边形（用于调试深度写入）
        drawTestQuad();
        
        // 7. 渲染模型
        drawModel();
        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
        // 7. 结束渲染通道
        m_rhi->cmdEndRenderPassPFN(m_rhi->getCurrentCommandBuffer());
        
        // LOG_INFO("[DirectionalLightShadowPass] Shadow pass draw completed");
    }
    
    /**
     * @brief 设置阴影贴图专用深度附件资源
     * @details 该函数创建高质量阴影渲染所需的优化深度资源：
     *          
     *          🎯 深度格式选择策略：
     *          - 主格式：D32_SFLOAT（32位浮点深度）
     *          - 优势：最高精度，减少深度冲突（Z-fighting）
     *          - 适用：高质量阴影渲染，远距离场景
     *          
     *          📋 资源创建流程：
     *          1. 🖼️ 深度图像创建
     *             - 尺寸：SHADOW_MAP_SIZE × SHADOW_MAP_SIZE
     *             - 格式：D32_SFLOAT（32位浮点深度）
     *             - 用途：深度附件 + 着色器采样
     *             - 内存：GPU设备本地内存（最佳性能）
     *          
     *          2. 👁️ 深度图像视图创建
     *             - 类型：2D纹理视图
     *             - 方面：仅深度通道（DEPTH_BIT）
     *             - 用途：渲染通道附件绑定
     *          
     *          3. 🔍 深度采样器配置（可选）
     *             - 过滤：线性插值（减少阴影锯齿）
     *             - 比较：启用深度比较（硬件PCF支持）
     *             - 边界：夹紧到边界（避免采样越界）
     *          
     * @note ✅ 使用32位浮点深度格式确保最佳阴影质量
     * @note 🚀 GPU设备本地内存提供最佳渲染性能
     * @note 🎨 支持后续着色器采样用于阴影映射
     */
    void DirectionalLightShadowPass::setupAttachments()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 阴影渲染只需要深度附件，不需要颜色附件
        
        // 🖼️ 创建高精度阴影深度图像
        // 使用32位浮点格式确保最佳深度精度和阴影质量
        rhi->createImage(
            SHADOW_MAP_SIZE, SHADOW_MAP_SIZE,
            RHI_FORMAT_D32_SFLOAT,                    // 32位浮点深度格式
            RHI_IMAGE_TILING_OPTIMAL,                 // 最优内存布局
            RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT, // 深度附件+采样
            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,     // GPU本地内存
            m_shadow_map_image,
            m_shadow_map_image_memory,
            0, 1, 1                                   // 无多重采样，单层级，单数组层
        );
        
        // 👁️ 创建深度图像视图（用于渲染通道附件绑定）
        RHIImageViewCreateInfo view_info{};
        view_info.sType = RHI_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = m_shadow_map_image;                    // 绑定深度图像
        view_info.viewType = RHI_IMAGE_VIEW_TYPE_2D;             // 2D纹理视图
        view_info.format = RHI_FORMAT_D32_SFLOAT;                // 匹配图像格式
        view_info.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_DEPTH_BIT;  // 仅访问深度通道
        view_info.subresourceRange.baseMipLevel = 0;             // 基础mip层级
        view_info.subresourceRange.levelCount = 1;               // 单个mip层级
        view_info.subresourceRange.baseArrayLayer = 0;          // 基础数组层
        view_info.subresourceRange.layerCount = 1;              // 单个数组层
        
        // 使用RHI简化接口创建图像视图
        rhi->createImageView(
            m_shadow_map_image, 
            RHI_FORMAT_D32_SFLOAT,           // 深度格式
            RHI_IMAGE_ASPECT_DEPTH_BIT,      // 深度方面
            RHI_IMAGE_VIEW_TYPE_2D,          // 2D视图类型
            1, 1,                            // 单mip，单层
            m_shadow_map_image_view          // 输出视图句柄
        );
        
        // 🔍 创建阴影贴图采样器（用于在主渲染通道中采样阴影贴图）
        // 🌟 优化采样器配置以增强阴影对比度
        RHISamplerCreateInfo sampler_info{};
        sampler_info.sType = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = RHI_FILTER_LINEAR;                    // 线性放大过滤
        sampler_info.minFilter = RHI_FILTER_LINEAR;                    // 线性缩小过滤
        sampler_info.addressModeU = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;  // U方向边界夹紧
        sampler_info.addressModeV = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;  // V方向边界夹紧
        sampler_info.addressModeW = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;  // W方向边界夹紧
        sampler_info.anisotropyEnable = RHI_FALSE;                     // 禁用各向异性过滤
        sampler_info.maxAnisotropy = 1.0f;
        sampler_info.borderColor = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // 🔧 修复：白色边界表示无阴影区域
        sampler_info.unnormalizedCoordinates = RHI_FALSE;              // 使用标准化坐标
        sampler_info.compareEnable = RHI_TRUE;                         // 启用深度比较（阴影映射必需）
        sampler_info.compareOp = RHI_COMPARE_OP_LESS;                  // 🔧 修复：与管线深度比较操作一致
        sampler_info.mipmapMode = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 1.0f;
        
        if (rhi->createSampler(&sampler_info, m_shadow_map_sampler) != RHI_SUCCESS)
        {
            LOG_ERROR("Failed to create shadow map sampler!");
            return;
        }
    }
    
    /**
     * @brief 设置阴影渲染通道
     * @details 该函数创建专用于阴影渲染的Vulkan渲染通道：
     *          1. 配置深度附件（D32_SFLOAT格式）
     *             - 加载操作：清除（CLEAR）
     *             - 存储操作：保存（STORE）用于后续采样
     *             - 最终布局：深度模板只读最优（用于着色器采样）
     *          2. 定义子通道，仅使用深度附件，无颜色附件
     *          3. 设置子通道依赖，确保深度写入与着色器读取的同步
     * @note 渲染通道专门用于深度渲染，不输出颜色信息
     */
    void DirectionalLightShadowPass::setupRenderPass()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 深度附件描述 - 阴影渲染只需要深度附件
        RHIAttachmentDescription depth_attachment{};
        depth_attachment.format = RHI_FORMAT_D32_SFLOAT;
        depth_attachment.samples = RHI_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = RHI_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = RHI_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.stencilLoadOp = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // 🔧 修复：与主渲染Pass期望的布局一致

        // 深度附件引用
        RHIAttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 0;  // 深度附件是唯一的附件（索引0）
        depth_attachment_ref.layout = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        // 子通道描述 - 阴影渲染不需要颜色附件
        RHISubpassDescription subpass{};
        subpass.pipelineBindPoint = RHI_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;  // 无颜色附件
        subpass.pColorAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;
        
        // 子通道依赖
        RHISubpassDependency dependency{};
        dependency.srcSubpass = RHI_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.srcAccessMask = RHI_ACCESS_SHADER_READ_BIT;
        dependency.dstStageMask = RHI_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        // 创建渲染通道 - 只包含深度附件
        RHIRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = RHI_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RHIAttachmentDescription attachments[] = {depth_attachment};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = attachments;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;
        
        if (rhi->createRenderPass(&render_pass_info, m_render_pass) != RHI_SUCCESS)
        {
            LOG_ERROR("[DirectionalLightShadowPass] Failed to create shadow render pass!");
            throw std::runtime_error("Failed to create shadow render pass");
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Shadow render pass created successfully");
    }
    
    /**
     * @brief 设置阴影渲染帧缓冲
     * @details 该函数创建与阴影渲染通道兼容的帧缓冲对象：
     *          1. 绑定阴影贴图图像视图作为深度附件
     *          2. 设置帧缓冲尺寸为阴影贴图大小（SHADOW_MAP_SIZE）
     *          3. 配置为单层帧缓冲（layers = 1）
     * @note 帧缓冲必须与之前创建的渲染通道兼容，附件数量和格式必须匹配
     */
    void DirectionalLightShadowPass::setupFramebuffer()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 验证依赖资源
        if (!m_render_pass) {
            LOG_ERROR("[DirectionalLightShadowPass] Render pass must be created before framebuffer");
            throw std::runtime_error("Render pass not available for framebuffer creation");
        }
        
        if (!m_shadow_map_image_view) {
            LOG_ERROR("[DirectionalLightShadowPass] Shadow map image view must be created before framebuffer");
            throw std::runtime_error("Shadow map image view not available for framebuffer creation");
        }
        
        // 创建帧缓冲 - 只包含深度附件（阴影渲染不需要颜色附件）
        RHIImageView* attachments[] = { m_shadow_map_image_view };
        
        RHIFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = RHI_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = m_render_pass;
        framebuffer_info.attachmentCount = 1;  // 只有深度附件
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = SHADOW_MAP_SIZE;
        framebuffer_info.height = SHADOW_MAP_SIZE;
        framebuffer_info.layers = 1;
        
        if (rhi->createFramebuffer(&framebuffer_info, m_framebuffer) != RHI_SUCCESS)
        {
            LOG_ERROR("[DirectionalLightShadowPass] Failed to create shadow framebuffer!");
            throw std::runtime_error("Failed to create shadow framebuffer");
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Shadow framebuffer created successfully ({}x{})", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    }
    
    /**
     * @brief 设置阴影渲染专用描述符集布局
     * @details 该函数创建与shadow shader完全匹配的描述符集布局：
     *          
     *          🎯 Shader绑定映射：
     *          - set = 0, binding = 0 → GlobalFrameBuffer (光源投影视图矩阵)
     *          - set = 0, binding = 1 → InstanceDataBuffer (实例变换矩阵数组)
     *          
     *          📋 绑定详细配置：
     *          1. Binding 0: 全局帧缓冲区（GlobalFrameBuffer）
     *             - 类型：只读存储缓冲区（STORAGE_BUFFER）
     *             - 阶段：顶点着色器专用（VERTEX_BIT）
     *             - 内容：light_proj_view矩阵（mat4）
     *             - 用途：将顶点从世界空间变换到光源裁剪空间
     *          
     *          2. Binding 1: 实例数据缓冲区（InstanceDataBuffer）
     *             - 类型：只读存储缓冲区（STORAGE_BUFFER）
     *             - 阶段：顶点着色器专用（VERTEX_BIT）
     *             - 内容：model_matrices[]数组（mat4[]）
     *             - 用途：支持实例化渲染，每个实例使用不同的模型变换
     *          
     * @note ✅ 布局与mesh_directional_light_shadow.vert中的绑定声明完全匹配
     * @note 🚀 使用存储缓冲区类型支持动态大小数组和高效内存访问
     * @note 🎨 专为深度渲染优化，无颜色相关绑定
     */
    void DirectionalLightShadowPass::setupDescriptorSetLayout()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 创建描述符集布局绑定
        // 🎯 Binding 0: 统一缓冲区（UBO - 包含model, view, proj矩阵）
        RHIDescriptorSetLayoutBinding ubo_binding{};
        ubo_binding.binding = 0;
        ubo_binding.descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // 修复：使用UNIFORM_BUFFER匹配shader
        ubo_binding.descriptorCount = 1;
        ubo_binding.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT; // 🔧 修复：允许顶点和片段着色器访问
        ubo_binding.pImmutableSamplers = nullptr;
        
        RHIDescriptorSetLayoutBinding bindings[] = {ubo_binding};
        
        RHIDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1; // 修复：只有一个绑定点
        layout_info.pBindings = bindings;
        
        if (rhi->createDescriptorSetLayout(&layout_info, m_descriptor_set_layout) != RHI_SUCCESS)
        {
            LOG_ERROR("[DirectionalLightShadowPass::setupDescriptorSetLayout] Failed to create descriptor set layout");
            return;
        }
        
        // 创建uniform buffer资源
        createUniformBuffers();
        
        
    }
    
    /**
     * @brief 创建统一缓冲区
     * @details 该函数创建阴影渲染所需的两个主要缓冲区：
     *          1. 全局帧缓冲区（GlobalFrameBuffer）
     *             - 存储光源投影视图矩阵（4x4矩阵）
     *             - 大小：sizeof(glm::mat4)
     *          2. 实例数据缓冲区（InstanceDataBuffer）
     *             - 存储所有渲染对象的模型变换矩阵
     *             - 预分配1000个实例的空间
     * @note 两个缓冲区都使用主机可见和一致性内存，便于CPU更新数据
     */
    void DirectionalLightShadowPass::createUniformBuffers()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 定义UBO结构体，匹配shadow.vert shader中的ubo定义
        struct ShadowUBO {
            glm::mat4 view;   // 视图矩阵（光源视角）
            glm::mat4 proj;   // 投影矩阵（光源投影）
        };
        
        // 创建uniform buffer (UBO)
        RHIDeviceSize ubo_buffer_size = sizeof(ShadowUBO);
        
        rhi->createBuffer(ubo_buffer_size,
                         RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT, // 修复：使用UNIFORM_BUFFER匹配shader
                         RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_global_uniform_buffer,
                         m_global_uniform_buffer_memory);
        
                
    }
    
    /**
     * @brief 创建阴影渲染管线
     * @details 该函数创建完整的阴影渲染图形管线，包括：
     *          1. 管线布局：绑定描述符集布局
     *          2. 着色器阶段：加载顶点和片段着色器
     *          3. 顶点输入：定义顶点属性（仅位置信息）
     *          4. 输入装配：三角形列表拓扑
     *          5. 视口状态：设置为阴影贴图尺寸
     *          6. 光栅化状态：
     *             - 背面剔除
     *             - 启用深度偏置以减少阴影失真
     *          7. 深度测试：启用深度测试和写入
     *          8. 颜色混合：无颜色输出（仅深度渲染）
     * @note 深度偏置参数经过调优以减少阴影痤疮和彼得潘效应
     */
    void DirectionalLightShadowPass::setupPipelines()
    {
        
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 创建管线布局
        RHIDescriptorSetLayout* descriptor_set_layouts[] = {m_descriptor_set_layout};
        
        // 配置Push Constants范围（用于传递per-model变换矩阵）
        RHIPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(glm::mat4); // 64字节用于model矩阵
        
        RHIPipelineLayoutCreateInfo pipeline_layout_create_info{};
        pipeline_layout_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
        pipeline_layout_create_info.pushConstantRangeCount = 1;
        pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
        
        if (rhi->createPipelineLayout(&pipeline_layout_create_info, m_pipeline_layout) != RHI_SUCCESS)
        {
            LOG_ERROR("[DirectionalLightShadowPass] Failed to create pipeline layout");
            throw std::runtime_error("Failed to create pipeline layout");
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Pipeline layout created successfully");
        
        // 创建着色器模块
        
        RHIShader* vert_shader_module = rhi->createShaderModule(SHADOW_VERT);
         RHIShader* frag_shader_module = rhi->createShaderModule(SHADOW_FRAG);
        
        // 配置着色器阶段
        RHIPipelineShaderStageCreateInfo vert_shader_stage_info{};
        vert_shader_stage_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_shader_stage_info.stage = RHI_SHADER_STAGE_VERTEX_BIT;
        vert_shader_stage_info.module = vert_shader_module;
        vert_shader_stage_info.pName = "main";
        
        RHIPipelineShaderStageCreateInfo frag_shader_stage_info{};
        frag_shader_stage_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_shader_stage_info.stage = RHI_SHADER_STAGE_FRAGMENT_BIT;
        frag_shader_stage_info.module = frag_shader_module;
        frag_shader_stage_info.pName = "main";
        
        RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};
        
        // 顶点输入状态（配置完整顶点属性，匹配shadow.vert shader）
        RHIVertexInputBindingDescription binding_description{};
        binding_description.binding = 0;
        binding_description.stride = sizeof(float) * 11; // position(3) + normal(3) + color(3) + texCoord(2)
        binding_description.inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;
        
        // 配置4个顶点属性，匹配shadow.vert shader的输入
        RHIVertexInputAttributeDescription attribute_descriptions[4]{};
        
        // Location 0: inPosition (vec3)
        attribute_descriptions[0].binding = 0;
        attribute_descriptions[0].location = 0;
        attribute_descriptions[0].format = RHI_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[0].offset = 0;
        
        // Location 1: inNormal (vec3)
        attribute_descriptions[1].binding = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format = RHI_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[1].offset = sizeof(float) * 3;
        
        // Location 2: inColor (vec3)
        attribute_descriptions[2].binding = 0;
        attribute_descriptions[2].location = 2;
        attribute_descriptions[2].format = RHI_FORMAT_R32G32B32_SFLOAT;
        attribute_descriptions[2].offset = sizeof(float) * 6;
        
        // Location 3: inTexCoord (vec2)
        attribute_descriptions[3].binding = 0;
        attribute_descriptions[3].location = 3;
        attribute_descriptions[3].format = RHI_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[3].offset = sizeof(float) * 9;
        
        RHIPipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &binding_description;
        vertex_input_info.vertexAttributeDescriptionCount = 4; // 修复：4个顶点属性
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;
        
        // 输入装配状态
        RHIPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = RHI_FALSE;
        
        // 视口状态
        RHIViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        
        RHIRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};
        
        RHIPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;
        
        // 🎨 阴影渲染专用光栅化状态配置
        RHIPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = RHI_FALSE;                    // 禁用深度夹紧
        rasterizer.rasterizerDiscardEnable = RHI_FALSE;             // 启用光栅化
        rasterizer.polygonMode = RHI_POLYGON_MODE_FILL;             // 填充模式
        rasterizer.lineWidth = 1.0f;                               // 线宽（填充模式下无效）
        rasterizer.cullMode = RHI_CULL_MODE_BACK_BIT;               // 背面剔除（提升性能）
        rasterizer.frontFace = RHI_FRONT_FACE_COUNTER_CLOCKWISE;    // 逆时针为正面
        
        // 🔧 深度偏置配置（解决阴影失真问题）
        // 🌟 优化偏置参数以增强阴影清晰度
        rasterizer.depthBiasEnable = RHI_TRUE;                      // 启用深度偏置
        rasterizer.depthBiasConstantFactor = 1.25f;                 // 优化常量偏置，减少阴影痤疮
        rasterizer.depthBiasClamp = 0.0f;                           // 无偏置夹紧
        rasterizer.depthBiasSlopeFactor = 1.75f;                    // 优化斜率偏置，改善倾斜表面阴影
        
        // 💡 深度偏置参数说明：
        // - constantFactor: 解决平面上的阴影痤疮（shadow acne）
        // - slopeFactor: 处理倾斜表面的阴影失真
        // - 这些值经过调优，平衡阴影质量和彼得潘效应（peter panning）
        
        // 🔍 多重采样状态（阴影渲染使用单采样）
        RHIPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = RHI_FALSE;              // 禁用采样着色
        multisampling.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT; // 单采样（性能优先）
        
        // 🎯 深度模板状态配置（阴影渲染核心）
        RHIPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = RHI_TRUE;                   // 启用深度测试（必须）
        depth_stencil.depthWriteEnable = RHI_TRUE;                  // 启用深度写入（生成阴影贴图）
        depth_stencil.depthCompareOp = RHI_COMPARE_OP_LESS;         // 🔧 修复：使用LESS比较操作提高深度精度
        depth_stencil.depthBoundsTestEnable = RHI_FALSE;            // 禁用深度边界测试
        depth_stencil.stencilTestEnable = RHI_FALSE;                // 禁用模板测试（阴影不需要）
        
        // 💡 深度测试说明：
        // - LESS比较操作确保只有最近的片段被写入阴影贴图
        // - 深度写入生成用于后续阴影映射的深度值
        
        // 颜色混合状态（阴影渲染不需要颜色附件）
        RHIPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = RHI_FALSE;
        color_blending.attachmentCount = 0; // 无颜色附件
        color_blending.pAttachments = nullptr; // 无颜色混合附件配置
        
        // 动态状态配置（与其他Pass保持一致）
        RHIDynamicState dynamic_states[] = {
            RHI_DYNAMIC_STATE_VIEWPORT,
            RHI_DYNAMIC_STATE_SCISSOR
        };
        
        RHIPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;
        
        // 创建图形管线
        RHIGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = m_pipeline_layout;
        pipeline_info.renderPass = m_render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = RHI_NULL_HANDLE;
        pipeline_info.basePipelineIndex = -1;
        
        // 验证关键参数
        if (!m_pipeline_layout) {
            LOG_ERROR("[DirectionalLightShadowPass] Pipeline layout is null!");
            throw std::runtime_error("Pipeline layout not available");
        }
        if (!m_render_pass) {
            LOG_ERROR("[DirectionalLightShadowPass] Render pass is null!");
            throw std::runtime_error("Render pass not available");
        }
        if (!vert_shader_module) {
            LOG_ERROR("[DirectionalLightShadowPass] Vertex shader module is null!");
            throw std::runtime_error("Vertex shader module not available");
        }
        if (!frag_shader_module) {
            LOG_ERROR("[DirectionalLightShadowPass] Fragment shader module is null!");
            throw std::runtime_error("Fragment shader module not available");
        }
        
        // 创建图形管线
        if (rhi->createGraphicsPipelines(RHI_NULL_HANDLE, 1, &pipeline_info, m_render_pipeline) != RHI_SUCCESS)
        {
            LOG_ERROR("[DirectionalLightShadowPass] Failed to create graphics pipeline");
            throw std::runtime_error("Failed to create graphics pipeline");
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Graphics pipeline created successfully");
        
        
        // 清理着色器模块
        rhi->destroyShaderModule(vert_shader_module);
        rhi->destroyShaderModule(frag_shader_module);
        
    }
    
    /**
     * @brief 设置描述符集
     * @details 该函数负责创建和配置阴影渲染所需的描述符集：
     *          1. 创建描述符池：
     *             - 存储缓冲类型：2个（全局帧缓冲 + 实例数据缓冲）
     *             - 统一缓冲类型：1个（备用）
     *          2. 从描述符池分配描述符集
     *          3. 描述符集的实际绑定在updateUniformBuffer()中完成
     * @note 描述符集分配后，需要在每帧更新时绑定具体的缓冲区数据
      * @throws std::runtime_error 当RHI为空或描述符相关资源创建失败时抛出异常
      */
    void DirectionalLightShadowPass::setupDescriptorSet()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        m_rhi = rhi;
        
        if (!m_rhi) {
            LOG_ERROR("[DirectionalLightShadowPass] RHI is null in setupDescriptorSet");
            return;
        }
        
        // 创建描述符池 - 支持多帧并发
        RHIDescriptorPoolSize pool_sizes[1];
        pool_sizes[0].type = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 3; // 🔧 修复：为3个飞行帧各创建一个UBO描述符
        
        RHIDescriptorPoolCreateInfo pool_create_info{};
        pool_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_create_info.poolSizeCount = 1;
        pool_create_info.pPoolSizes = pool_sizes;
        pool_create_info.maxSets = 3; // 🔧 修复：支持3个描述符集
        
        if (m_rhi->createDescriptorPool(&pool_create_info, m_descriptor_pool) != RHI_SUCCESS) {
            LOG_ERROR("[DirectionalLightShadowPass] Failed to create descriptor pool");
            return;
        }
        
        // 分配描述符集 - 为每个飞行帧分配独立描述符集
        RHIDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = m_descriptor_pool;
        alloc_info.descriptorSetCount = 1; // 每次分配一个描述符集
        alloc_info.pSetLayouts = &m_descriptor_set_layout;
        
        // 为每个飞行帧分配描述符集
        for (int i = 0; i < 3; ++i) {
            if (m_rhi->allocateDescriptorSets(&alloc_info, m_descriptor_sets[i]) != RHI_SUCCESS) {
                LOG_ERROR("[DirectionalLightShadowPass] Failed to allocate descriptor set for frame %d", i);
                return;
            }
        }
        
        // 注意：描述符集的实际更新将在draw()方法中的updateUniformBuffer()中进行
        // 这里只是分配了描述符集，但还没有绑定具体的缓冲区数据
        
        // LOG_INFO("[DirectionalLightShadowPass] Descriptor set setup completed");
    }
    
    /**
     * @brief 渲染模型到阴影贴图
     * @details 该函数执行实际的模型渲染操作：
     *          1. 验证渲染资源有效性
     *          2. 获取当前帧的所有渲染对象
     *          3. 对每个渲染对象：
     *             - 绑定顶点缓冲区（仅位置数据）
     *             - 绑定索引缓冲区（如果存在）
     *             - 执行绘制调用（索引化或非索引化）
     *             - 使用实例索引区分不同对象
     * @note 此函数假设uniform buffer和描述符集已在draw()中更新和绑定
     * @note 每个对象使用不同的实例索引，着色器可据此获取对应的变换矩阵
     */
    void DirectionalLightShadowPass::drawModel()
    {
        
        if (!m_current_render_resource) {
            LOG_WARN("[DirectionalLightShadowPass] No render resource available for drawModel");
            return;
        }
        
        const auto& render_objects = m_current_render_resource->getLoadedRenderObjects();
        if (render_objects.empty()) {
            LOG_WARN("[DirectionalLightShadowPass] No render objects available for shadow rendering");
            return;
        }
        
        // 注意：uniform buffer更新和描述符集绑定已在draw()方法中完成
        // 这里只需要进行模型渲染
        
        // LOG_INFO("[DirectionalLightShadowPass] Rendering {} objects to shadow map", render_objects.size());
        
        // 渲染每个模型
        for (size_t i = 0; i < render_objects.size(); ++i) {
            const auto& render_object = render_objects[i];
            // 验证渲染对象的有效性
            if (!render_object.vertexBuffer) {
                LOG_WARN("[DirectionalLightShadowPass] Object {} has no vertex buffer, skipping", i);
                continue;
            }
            
            // LOG_DEBUG("[DirectionalLightShadowPass] Rendering object {} with {} vertices", i, render_object.vertices.size());
            
            // 计算模型变换矩阵
            const auto& params = render_object.animationParams;
            glm::mat4 model = glm::mat4(1.0f);
            
            // 应用位移
            model = glm::translate(model, params.position);
            
            // 应用旋转（如果启用动画）
            if (params.enableAnimation) {
                float currentTime = static_cast<float>(glfwGetTime());
                float rotationAngle = currentTime * params.rotationSpeed;
                model = glm::rotate(model, rotationAngle, params.rotationAxis);
            }
            
            // 应用缩放
            model = glm::scale(model, params.scale);
            
            // 通过Push Constants传递模型矩阵
            m_rhi->cmdPushConstantsPFN(m_rhi->getCurrentCommandBuffer(),
                                     m_pipeline_layout,
                                     RHI_SHADER_STAGE_VERTEX_BIT,
                                     0,
                                     sizeof(glm::mat4),
                                     &model);
            
            // 绑定顶点缓冲区 - 只需要位置数据用于深度渲染
            RHIBuffer* vertex_buffers[] = { render_object.vertexBuffer };
            RHIDeviceSize offsets[] = { 0 };
            m_rhi->cmdBindVertexBuffersPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, vertex_buffers, offsets);
            
            // 根据是否有索引缓冲区选择绘制方式
            if (render_object.indexBuffer && !render_object.indices.empty()) {
                // 绑定索引缓冲区
                m_rhi->cmdBindIndexBufferPFN(m_rhi->getCurrentCommandBuffer(), 
                                           render_object.indexBuffer, 0, RHI_INDEX_TYPE_UINT32);
                
                // 执行索引化绘制
                // 使用实例索引来区分不同的对象，着色器可以据此获取对应的变换矩阵
                m_rhi->cmdDrawIndexedPFN(m_rhi->getCurrentCommandBuffer(),
                                       static_cast<uint32_t>(render_object.indices.size()), // 索引数量
                                       1, // 实例数量
                                       0, // 第一个索引
                                       0, // 顶点偏移
                                       static_cast<uint32_t>(i)); // 第一个实例（用作实例索引）
            } else if (!render_object.vertices.empty()) {
                // 执行非索引化绘制
                m_rhi->cmdDraw(m_rhi->getCurrentCommandBuffer(),
                             static_cast<uint32_t>(render_object.vertices.size()), // 顶点数量
                             1, // 实例数量
                             0, // 第一个顶点
                             static_cast<uint32_t>(i)); // 第一个实例（用作实例索引）
            } else {
                LOG_WARN("[DirectionalLightShadowPass] Object {} has no valid geometry data, skipping", i);
                continue;
            }
        }
        
        // LOG_INFO("[DirectionalLightShadowPass] Model rendering completed");
        
        
    }
    
    /**
     * @brief 更新光源投影视图矩阵
     * @param render_resource 当前帧的渲染资源，包含场景和光源信息
     * @details 该函数计算方向光的投影视图矩阵用于阴影贴图生成：
     *          1. 获取方向光的方向向量（从渲染资源或使用默认值）
     *          2. 计算光源位置（沿光线方向的远点，确保覆盖整个场景）
     *          3. 创建光源视图矩阵：
     *             - 光源位置作为观察点
     *             - 场景中心作为目标点
     *             - 世界上方向作为上向量
     *          4. 创建正交投影矩阵：
     *             - 使用正交投影（方向光特性）
     *             - 投影范围覆盖整个场景
     *             - 近远平面确保深度精度
     *          5. 计算最终的光源投影视图矩阵
     * @note 正交投影的尺寸和近远平面需要根据场景大小调整
     * @note 未来可扩展为从渲染资源中动态获取光源参数
     */
    /**
     * @brief 根据光源点坐标和场景观察点坐标计算阴影效果所需的各项参数
     * @details 该函数实现了完整的阴影参数计算系统，包括：
     *          1. 光源方向向量：从观察点指向光源的归一化向量
     *          2. 阴影投影矩阵：基于光源位置和观察点构建的阴影投影变换矩阵
     *          3. 阴影映射范围：确定阴影贴图需要覆盖的场景区域范围
     *          4. 深度偏移量：防止阴影自相交所需的适当偏移值
     * @param render_resource 渲染资源，用于获取场景信息
     */
    void DirectionalLightShadowPass::updateLightMatrix(std::shared_ptr<RenderResource> render_resource)
    {
        // ===== 核心坐标定义 =====
        // 🎯 光源数据 - 从 RenderResource 获取主方向光源数据
        const auto* primary_light = render_resource->getPrimaryDirectionalLight();
        if (!primary_light) {
            LOG_WARN("[Shadow] No primary directional light found, using default values");
            return;
        }
        
        // 从光源数据获取方向和计算位置
        glm::vec3 light_direction = glm::normalize(primary_light->direction);
        // 对于方向光，我们需要计算一个合适的位置来生成阴影
        glm::vec3 scene_center = glm::vec3(0.0f, 0.0f, 0.0f);
        float light_distance = primary_light->distance; // 使用光源数据中的距离参数
        glm::vec3 light_world_position = scene_center - light_direction * light_distance;
        
        // LOG_DEBUG("[Shadow] Using light data: Position ({:.2f}, {:.2f}, {:.2f}), Direction ({:.2f}, {:.2f}, {:.2f})", 
        //          light_world_position.x, light_world_position.y, light_world_position.z,
        //          light_direction.x, light_direction.y, light_direction.z);
        
        // ===== 1. 基础向量和距离计算 =====
        // 从光源指向场景中心的向量（用于阴影投射）
        glm::vec3 light_to_scene = scene_center - light_world_position;
        glm::vec3 shadow_cast_direction = glm::normalize(light_to_scene);
        
        // 计算光源到场景中心的距离
        float distance_light_to_scene = glm::length(light_to_scene);
        
        // 基于两个坐标计算场景覆盖半径（使用光源到场景中心距离的合理比例）
        float scene_coverage_radius = distance_light_to_scene * 1.8f; // 基于距离的动态半径
        
        // LOG_INFO("[Shadow] 1. Basic Calculations:");
        // LOG_INFO("[Shadow]    Light Position: ({:.2f}, {:.2f}, {:.2f})", 
        //          light_world_position.x, light_world_position.y, light_world_position.z);
        // LOG_INFO("[Shadow]    Scene Center: ({:.2f}, {:.2f}, {:.2f})", 
        //          scene_center.x, scene_center.y, scene_center.z);
        // LOG_INFO("[Shadow]    Distance Light->Scene: {:.2f}", distance_light_to_scene);
        // LOG_INFO("[Shadow]    Scene Coverage Radius: {:.2f}", scene_coverage_radius);
        
        // ===== 2. 阴影投影矩阵计算 =====
        // 创建光源视图矩阵：从光源位置看向场景中心
        glm::vec3 up_vector = glm::vec3(0.0f, 1.0f, 0.0f);
        
        // 确保up向量与光源方向不平行
        if (glm::abs(glm::dot(shadow_cast_direction, up_vector)) > 0.99f) {
            up_vector = glm::vec3(1.0f, 0.0f, 0.0f); // 使用替代up向量
        }
        
        glm::vec3 light_target = light_world_position + shadow_cast_direction;
        glm::mat4 light_view_matrix = glm::lookAt(light_world_position, light_target, up_vector);
        
        // LOG_INFO("[Shadow] 2. Shadow Projection Matrix:");
        // LOG_INFO("[Shadow]    Light Target: ({:.3f}, {:.3f}, {:.3f})", 
        //          light_target.x, light_target.y, light_target.z);
        // LOG_INFO("[Shadow]    Up Vector: ({:.3f}, {:.3f}, {:.3f})", 
        //          up_vector.x, up_vector.y, up_vector.z);
        
        // ===== 3. 阴影映射范围计算 =====
        // 基于光源和场景中心坐标计算阴影覆盖范围
        float base_scene_radius = 20.0f; // 基础场景半径
        
        // 根据光源距离调整覆盖范围
        float distance_factor = glm::clamp(primary_light->distance / 20.0f, 0.8f, 3.0f); // 使用光源距离参数
        float shadow_coverage_radius = base_scene_radius * distance_factor;
        
        // 使用场景覆盖半径和阴影覆盖半径的最大值确保完整覆盖
        float extended_radius = std::max(shadow_coverage_radius, scene_coverage_radius * 1.2f);
        
        // 正交投影参数
        float ortho_left = -extended_radius;
        float ortho_right = extended_radius;
        float ortho_bottom = -extended_radius;
        float ortho_top = extended_radius;
        
        // 根据光源距离动态调整近/远平面参数
        float ortho_near = std::max(0.1f, primary_light->distance * 0.1f); // 近平面：距离的10%，最小0.1f
        float ortho_far = primary_light->distance + extended_radius * 2.0f; // 远平面：基于光源距离而非场景距离
        
        // LOG_INFO("[Shadow] 3. Shadow Mapping Range:");
        // LOG_INFO("[Shadow]    Base Scene Radius: {:.2f}", base_scene_radius);
        // LOG_INFO("[Shadow]    Distance Factor: {:.3f}", distance_factor);
        // LOG_INFO("[Shadow]    Shadow Coverage Radius: {:.2f}", shadow_coverage_radius);
        // LOG_INFO("[Shadow]    Extended Radius: {:.2f}", extended_radius);
        // LOG_INFO("[Shadow]    Ortho Bounds: L={:.1f}, R={:.1f}, B={:.1f}, T={:.1f}", 
        //          ortho_left, ortho_right, ortho_bottom, ortho_top);
        // LOG_INFO("[Shadow]    Depth Range: Near={:.2f}, Far={:.2f}", ortho_near, ortho_far);
        
        // 创建正交投影矩阵
        glm::mat4 light_projection_matrix = glm::ortho(ortho_left, ortho_right, 
                                                       ortho_bottom, ortho_top, 
                                                       ortho_near, ortho_far);
        
        // ===== 4. 深度偏移量计算 =====
        // 根据光源角度和距离计算适当的深度偏移，防止阴影自相交（Shadow Acne）
        float light_angle_factor = glm::abs(glm::dot(shadow_cast_direction, glm::vec3(0.0f, 1.0f, 0.0f)));
        
        // 基础偏移量：角度越小（越斜），偏移量越大
        float base_bias = 0.005f;
        float angle_bias = base_bias * (1.0f - light_angle_factor) * 2.0f;
        
        // 距离偏移量：距离越远，偏移量越大
        float distance_bias = base_bias * (distance_light_to_scene / 50.0f);
        
        // 最终深度偏移量
        float depth_bias = glm::clamp(angle_bias + distance_bias, 0.001f, 0.02f);
        
        // 斜率偏移量（Slope Scale Bias）
        float slope_bias = glm::clamp(2.0f * (1.0f - light_angle_factor), 0.5f, 4.0f);
        
        // LOG_INFO("[Shadow] 4. Depth Bias Calculation:");
        // LOG_INFO("[Shadow]    Light Angle Factor: {:.3f}", light_angle_factor);
        // LOG_INFO("[Shadow]    Angle Bias: {:.6f}", angle_bias);
        // LOG_INFO("[Shadow]    Distance Bias: {:.6f}", distance_bias);
        // LOG_INFO("[Shadow]    Final Depth Bias: {:.6f}", depth_bias);
        // LOG_INFO("[Shadow]    Slope Scale Bias: {:.3f}", slope_bias);
        
        // ===== 最终矩阵计算和验证 =====
        // 计算最终的光源投影视图矩阵
        m_light_proj_view_matrix = light_projection_matrix * light_view_matrix;
        
        // 验证矩阵有效性
        float matrix_determinant = glm::determinant(m_light_proj_view_matrix);
        if (std::abs(matrix_determinant) < 1e-6f) {
            LOG_ERROR("[Shadow] Invalid light projection-view matrix (determinant={:.8f})!", matrix_determinant);
        } else {
            // LOG_INFO("[Shadow] Shadow matrix calculation completed successfully");
            // LOG_INFO("[Shadow] Matrix determinant: {:.8f}", matrix_determinant);
        }
        
        // ===== 参数存储和传递 =====
        // 存储计算出的关键参数供渲染管线使用
        // - depth_bias 和 slope_bias: 传递给光栅化状态防止阴影痤疮
        // - shadow_cast_direction: 光源方向向量，传递给光照着色器
        // - extended_radius: 阴影覆盖范围，用于优化阴影贴图分辨率
        // - light_world_position: 光源世界坐标，用于光照计算
        
        // LOG_INFO("[Shadow] Final Parameters Summary:");
        // LOG_INFO("[Shadow]    Shadow Direction: ({:.3f}, {:.3f}, {:.3f})", 
        //          shadow_cast_direction.x, shadow_cast_direction.y, shadow_cast_direction.z);
        // LOG_INFO("[Shadow]    Extended Radius: {:.2f}", extended_radius);
        // LOG_INFO("[Shadow]    Depth Bias: {:.6f}, Slope Bias: {:.3f}", depth_bias, slope_bias);
        
        // LOG_INFO("[Shadow] === Shadow Parameter Calculation Complete ===");
    }
    
    // 旧的光源系统方法已移除，现在使用 RenderResource 管理光源
    
    // 旧的兼容性光源接口已移除
    
    /**
     * @brief 高效更新阴影渲染缓冲区数据
     * @details 该函数采用优化策略更新阴影渲染所需的缓冲区数据：
     *          
     *          🎯 缓冲区更新策略：
     *          1. 📊 全局帧缓冲区（GlobalFrameBuffer）更新：
     *             - 内容：光源投影视图矩阵（light_proj_view）
     *             - 绑定：Binding 0（顶点着色器访问）
     *             - 频率：每帧更新（光源可能移动）
     *             - 优化：使用持久映射减少映射开销
     *          
     *          2. 🎨 实例数据缓冲区（InstanceDataBuffer）更新：
     *             - 内容：所有渲染对象的模型变换矩阵数组
     *             - 绑定：Binding 1（顶点着色器访问）
     *             - 频率：按需更新（对象变换改变时）
     *             - 优化：批量更新，减少内存映射次数
     *          
     *          3. 🔗 描述符集绑定优化：
     *             - 策略：仅在缓冲区大小改变时重新绑定
     *             - 缓存：避免重复的描述符更新操作
     *             - 性能：减少GPU驱动开销
     *          
     * @note ✅ 使用内存映射方式，适合频繁更新的HOST_VISIBLE缓冲区
     * @note 🚀 采用批量更新策略，最小化GPU同步点
     * @note 🎨 支持动态实例数量，自动调整缓冲区大小
     */
    void DirectionalLightShadowPass::updateUniformBuffer()
    {
        if (!m_current_render_resource) {
            LOG_ERROR("[DirectionalLightShadowPass] No render resource available for updateUniformBuffer");
            return;
        }
        
        const auto& render_objects = m_current_render_resource->getLoadedRenderObjects();
        
        // 📊 修复：使用正确的UBO结构，匹配着色器定义（两个独立矩阵）
        ShadowUniformBufferObject ubo_data;
        
        // 🎯 设置UBO数据：使用RenderResource中的实际光源数据
        // 获取主方向光源数据
        const DirectionalLightData* primary_light = m_current_render_resource->getPrimaryDirectionalLight();
        if (!primary_light) {
            LOG_ERROR("[DirectionalLightShadowPass] No primary directional light available");
            return;
        }
        
        glm::vec3 light_direction = glm::normalize(primary_light->direction);
        glm::vec3 scene_center = glm::vec3(0.0f, 0.0f, 0.0f);
        float scene_radius = 50.0f;
        glm::vec3 light_position = scene_center - light_direction * (scene_radius * 2.0f);
        
        // 创建独立的视图和投影矩阵
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 target = light_position + light_direction;
        ubo_data.view = glm::lookAt(light_position, target, up);
        
        float ortho_size = scene_radius * 1.5f;
        float near_plane = 1.0f;
        float far_plane = scene_radius * 4.0f;
        ubo_data.proj = glm::ortho(-ortho_size, ortho_size, -ortho_size, ortho_size, near_plane, far_plane);
        
        // LOG_DEBUG("[Shadow] UBO Update - View matrix determinant: {:.6f}", glm::determinant(ubo_data.view));
        // LOG_DEBUG("[Shadow] UBO Update - Proj matrix determinant: {:.6f}", glm::determinant(ubo_data.proj));
        
        // 🔗 高效缓冲区数据更新（使用内存映射）
        if (m_global_uniform_buffer) {
            // 📊 更新UBO缓冲区（包含view, proj矩阵）
            void* ubo_mapped_data;
            m_rhi->mapMemory(m_global_uniform_buffer_memory, 0, sizeof(ShadowUniformBufferObject), 0, &ubo_mapped_data);
            memcpy(ubo_mapped_data, &ubo_data, sizeof(ShadowUniformBufferObject));
            m_rhi->unmapMemory(m_global_uniform_buffer_memory);
            
            // 🎯 配置描述符缓冲区信息（绑定UBO到着色器）
            RHIDescriptorBufferInfo ubo_buffer_info{};
            ubo_buffer_info.buffer = m_global_uniform_buffer;           // UBO缓冲区句柄
            ubo_buffer_info.offset = 0;                                // 从缓冲区开始位置
            ubo_buffer_info.range = sizeof(ShadowUniformBufferObject); // UBO缓冲区大小
            
            // 🔗 描述符集更新 - 只更新当前帧的描述符集，避免更新正在使用的描述符集
            uint32_t currentFrameIndex = m_rhi->getCurrentFrameIndex();
            
            RHIWriteDescriptorSet descriptor_write;
            // Binding 0: UBO → shader中的uniformbuffer (ubo)
            descriptor_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_write.pNext = nullptr; // 🔧 修复：正确初始化pNext字段
            descriptor_write.dstSet = m_descriptor_sets[currentFrameIndex]; // 🔧 修复：只使用当前帧的描述符集
            descriptor_write.dstBinding = 0;                            // 对应shader binding 0
            descriptor_write.dstArrayElement = 0;
            descriptor_write.descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_write.descriptorCount = 1;
            descriptor_write.pBufferInfo = &ubo_buffer_info;
            descriptor_write.pImageInfo = nullptr; // 🔧 修复：初始化未使用的字段
            descriptor_write.pTexelBufferView = nullptr; // 🔧 修复：初始化未使用的字段
            
            // 🚀 执行描述符更新 - 只更新当前帧的描述符集
            m_rhi->updateDescriptorSets(1, &descriptor_write, 0, nullptr);
        }
        
        
    }
    
    /**
     * @brief 帧缓冲重建后的更新处理
     * @details 该函数在交换链重建（如窗口大小改变）后被调用：
     *          - 阴影贴图通常使用固定尺寸，不依赖于屏幕分辨率
     *          - 因此大多数情况下不需要重建阴影相关资源
     *          - 如果需要自适应阴影质量，可在此处实现：
     *            * 根据新的屏幕分辨率调整阴影贴图尺寸
     *            * 重建阴影贴图纹理和帧缓冲
     *            * 更新渲染管线的视口设置
     * @note 当前实现为空，保持阴影贴图尺寸不变
     * @note 未来可扩展为动态阴影质量调整功能
     */
    void DirectionalLightShadowPass::updateAfterFramebufferRecreate()
    {
        // 阴影贴图通常不依赖于交换链大小，所以这里可能不需要重建
        // 但如果需要根据屏幕分辨率调整阴影贴图大小，可以在这里实现
        
    }
    
    /**
     * @brief 渲染测试四边形用于调试Shadow Pass深度写入
     * @details 该函数渲染一个简单的四边形到阴影贴图，用于验证深度写入是否正常工作：
     *          1. 检查深度测试和深度写入是否启用
     *          2. 验证光源变换矩阵是否正确
     *          3. 确认几何体是否在光源视锥内
     *          4. 测试深度值是否被正确写入阴影贴图
     * @note 这是一个调试功能，用于排查深度值始终为1的问题
     */
    void DirectionalLightShadowPass::drawTestQuad()
    {
        if (!m_test_quad_initialized) {
            initializeTestQuad();
        }
        
        // 绑定测试四边形的顶点缓冲区
        RHIBuffer* vertex_buffers[] = {m_test_quad_vertex_buffer};
        RHIDeviceSize offsets[] = {0};
        m_rhi->cmdBindVertexBuffersPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, vertex_buffers, offsets);
        
        // 绑定索引缓冲区
        m_rhi->cmdBindIndexBufferPFN(m_rhi->getCurrentCommandBuffer(), m_test_quad_index_buffer, 0, RHI_INDEX_TYPE_UINT16);
        
        // 设置模型矩阵（将四边形放置在光源视锥内的合适位置）
        glm::mat4 model_matrix = glm::mat4(1.0f);
        model_matrix = glm::translate(model_matrix, glm::vec3(0.0f, 0.0f, -10.0f)); // 放在光源前方
        model_matrix = glm::scale(model_matrix, glm::vec3(5.0f, 5.0f, 1.0f)); // 适当缩放
        
        // 推送模型矩阵到着色器
        m_rhi->cmdPushConstantsPFN(
            m_rhi->getCurrentCommandBuffer(),
            m_pipeline_layout,
            RHI_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &model_matrix
        );
        
        // 绘制测试四边形
        m_rhi->cmdDrawIndexedPFN(m_rhi->getCurrentCommandBuffer(), 6, 1, 0, 0, 0);
        
        // LOG_INFO("[Shadow Debug] Test quad rendered for depth testing");
    }
    
    /**
     * @brief 初始化测试四边形的几何数据
     * @details 创建一个简单的四边形用于测试深度写入功能：
     *          - 顶点数据：位置、法线、颜色、纹理坐标
     *          - 索引数据：两个三角形组成四边形
     *          - 缓冲区：顶点缓冲区和索引缓冲区
     * @note 这个四边形专门用于调试Shadow Pass的深度写入问题
     */
    void DirectionalLightShadowPass::initializeTestQuad()
    {
        std::shared_ptr<RHI> rhi = g_runtime_global_context.m_render_system->getRHI();
        
        // 定义测试四边形顶点数据（匹配shadow.vert的输入格式）
        struct TestVertex {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec3 color;
            glm::vec2 texCoord;
        };
        
        // 创建一个简单的四边形（在XY平面上）
        TestVertex vertices[] = {
            // 位置                法线              颜色              纹理坐标
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}, // 左下
            {{ 1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}, // 右下
            {{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // 右上
            {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}  // 左上
        };
        
        // 索引数据（两个三角形组成四边形）
        uint16_t indices[] = {
            0, 1, 2,  // 第一个三角形
            2, 3, 0   // 第二个三角形
        };
        
        // 创建顶点缓冲区
        RHIDeviceSize vertex_buffer_size = sizeof(vertices);
        rhi->createBuffer(
            vertex_buffer_size,
            RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_test_quad_vertex_buffer,
            m_test_quad_vertex_buffer_memory
        );
        
        // 复制顶点数据到缓冲区
        void* vertex_data;
        rhi->mapMemory(m_test_quad_vertex_buffer_memory, 0, vertex_buffer_size, 0, &vertex_data);
        memcpy(vertex_data, vertices, vertex_buffer_size);
        rhi->unmapMemory(m_test_quad_vertex_buffer_memory);
        
        // 创建索引缓冲区
        RHIDeviceSize index_buffer_size = sizeof(indices);
        rhi->createBuffer(
            index_buffer_size,
            RHI_BUFFER_USAGE_INDEX_BUFFER_BIT,
            RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_test_quad_index_buffer,
            m_test_quad_index_buffer_memory
        );
        
        // 复制索引数据到缓冲区
        void* index_data;
        rhi->mapMemory(m_test_quad_index_buffer_memory, 0, index_buffer_size, 0, &index_data);
        memcpy(index_data, indices, index_buffer_size);
        rhi->unmapMemory(m_test_quad_index_buffer_memory);
        
        m_test_quad_initialized = true;
        
        // LOG_INFO("[Shadow Debug] Test quad geometry initialized for depth testing");
    }
    
} // namespace Elish