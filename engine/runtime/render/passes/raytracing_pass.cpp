#include "raytracing_pass.h"
#include "../../core/base/macro.h"
#include "../../render/interface/rhi.h"
#include "../../render/interface/vulkan/vulkan_rhi_resource.h"
#include "../../render/interface/vulkan/vulkan_rhi.h"
#include "../../render/interface/vulkan/vulkan_util.h"
#include "../../render/render_system.h"
#include "../render_resource.h"
#include "../render_camera.h"
#include "../../global/global_context.h"

#include <vector>
#include <cstring>
#include <string>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 包含生成的光线追踪着色器头文件
#include "../../shader/generated/cpp/raytracing_rgen.h"
#include "../../shader/generated/cpp/raytracing_rchit.h"
#include "../../shader/generated/cpp/raytracing_rmiss.h"
#include "../../shader/generated/cpp/shadow_rmiss.h"

namespace Elish
{
    /**
     * @brief 构造函数
     */
    RayTracingPass::RayTracingPass()
        : m_ray_tracing_enabled(false)
        , m_is_initialized(false)
        , m_max_ray_depth(10)
        , m_samples_per_pixel(1)
        , m_output_image(nullptr)
        , m_output_image_view(nullptr)
        , m_output_image_memory(nullptr)
        , m_raygen_shader_binding_table(nullptr)
        , m_miss_shader_binding_table(nullptr)
        , m_hit_shader_binding_table(nullptr)
        , m_raygen_sbt_memory(nullptr)
        , m_miss_sbt_memory(nullptr)
        , m_hit_sbt_memory(nullptr)
        , m_ray_tracing_pipeline(nullptr)
        , m_ray_tracing_pipeline_layout(nullptr)
    {
    }

    /**
     * @brief 析构函数
     */
    RayTracingPass::~RayTracingPass()
    {
        // 清理光线追踪资源
        if (m_rhi)
        {
            // 清理输出图像
            if (m_output_image_view)
            {
                m_rhi->destroyImageView(m_output_image_view);
                m_output_image_view = nullptr;
            }
            if (m_output_image)
            {
                m_rhi->destroyImage(m_output_image);
                m_output_image = nullptr;
            }
            if (m_output_image_memory)
            {
                m_rhi->freeMemory(m_output_image_memory);
                m_output_image_memory = nullptr;
            }

            // 清理着色器绑定表
            if (m_raygen_shader_binding_table)
            {
                m_rhi->destroyBuffer(m_raygen_shader_binding_table);
                m_raygen_shader_binding_table = nullptr;
            }
            if (m_miss_shader_binding_table)
            {
                m_rhi->destroyBuffer(m_miss_shader_binding_table);
                m_miss_shader_binding_table = nullptr;
            }
            if (m_hit_shader_binding_table)
            {
                m_rhi->destroyBuffer(m_hit_shader_binding_table);
                m_hit_shader_binding_table = nullptr;
            }

            // 清理内存
            if (m_raygen_sbt_memory)
            {
                m_rhi->freeMemory(m_raygen_sbt_memory);
                m_raygen_sbt_memory = nullptr;
            }
            if (m_miss_sbt_memory)
            {
                m_rhi->freeMemory(m_miss_sbt_memory);
                m_miss_sbt_memory = nullptr;
            }
            if (m_hit_sbt_memory)
            {
                m_rhi->freeMemory(m_hit_sbt_memory);
                m_hit_sbt_memory = nullptr;
            }

            // 清理管线
            // Note: Pipeline and pipeline layout resources are managed by RHI internally
            // No explicit destroy methods available in RHI interface
            if (m_ray_tracing_pipeline)
            {
                // m_rhi->destroyPipeline(m_ray_tracing_pipeline); // Method not available
                m_ray_tracing_pipeline = nullptr;
            }
            if (m_ray_tracing_pipeline_layout)
            {
                // m_rhi->destroyPipelineLayout(m_ray_tracing_pipeline_layout); // Method not available
                m_ray_tracing_pipeline_layout = nullptr;
            }

            // 清理uniform缓冲区
            for (size_t i = 0; i < m_uniform_buffers.size(); ++i)
            {
                if (m_uniform_buffers_mapped[i])
                {
                    m_rhi->unmapMemory(m_uniform_buffers_memory[i]);
                }
                if (m_uniform_buffers[i])
                {
                    m_rhi->destroyBuffer(m_uniform_buffers[i]);
                }
                if (m_uniform_buffers_memory[i])
                {
                    m_rhi->freeMemory(m_uniform_buffers_memory[i]);
                }
            }
            m_uniform_buffers.clear();
            m_uniform_buffers_memory.clear();
            m_uniform_buffers_mapped.clear();
        }
    }

    /**
     * @brief 初始化光线追踪渲染通道
     */
    void RayTracingPass::initialize()
    {
        LOG_INFO("[RayTracingPass] Initializing ray tracing pass");

        // 检查光线追踪支持
        if (!m_rhi->isRayTracingSupported())
        {
            LOG_WARN("[RayTracingPass] Ray tracing is not supported on this device");
            return;
        }

        try
        {
            // 设置描述符集布局
            setupDescriptorSetLayout();

            // 设置光线追踪管线
            setupRayTracingPipeline();

            // 创建输出图像
            createOutputImage();

            // 设置描述符集
            setupDescriptorSet();

            // 创建着色器绑定表
            createShaderBindingTable();

            // 创建uniform缓冲区
            uint32_t max_frames_in_flight = m_rhi->getMaxFramesInFlight();
            m_uniform_buffers.resize(max_frames_in_flight);
            m_uniform_buffers_memory.resize(max_frames_in_flight);
            m_uniform_buffers_mapped.resize(max_frames_in_flight);

            for (uint32_t i = 0; i < max_frames_in_flight; ++i)
            {
                RHIBufferCreateInfo buffer_create_info{};
                buffer_create_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                buffer_create_info.size = sizeof(RayTracingUniformData);
                buffer_create_info.usage = RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                buffer_create_info.sharingMode = RHI_SHARING_MODE_EXCLUSIVE;

                if (m_rhi->createBuffer(&buffer_create_info, m_uniform_buffers[i]) != RHI_SUCCESS)
                {
                    throw std::runtime_error("[RayTracingPass] Failed to create uniform buffer");
                }

                RHIMemoryRequirements mem_requirements;
                m_rhi->getBufferMemoryRequirements(m_uniform_buffers[i], &mem_requirements);

                RHIMemoryAllocateInfo alloc_info{};
                alloc_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                alloc_info.allocationSize = mem_requirements.size;
                alloc_info.memoryTypeIndex = m_rhi->findMemoryType(mem_requirements.memoryTypeBits,
                    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                if (m_rhi->allocateMemory(&alloc_info, m_uniform_buffers_memory[i]) != RHI_SUCCESS)
                {
                    throw std::runtime_error("[RayTracingPass] Failed to allocate uniform buffer memory");
                }

                m_rhi->bindBufferMemory(m_uniform_buffers[i], m_uniform_buffers_memory[i], 0);
                m_rhi->mapMemory(m_uniform_buffers_memory[i], 0, sizeof(RayTracingUniformData), 0, &m_uniform_buffers_mapped[i]);
            }

            m_is_initialized = true;
            LOG_INFO("[RayTracingPass] Ray tracing pass initialized successfully");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[RayTracingPass] Failed to initialize ray tracing pass: {}", e.what());
            throw;
        }
    }

    /**
     * @brief 准备光线追踪渲染通道的数据
     */
    void RayTracingPass::preparePassData(std::shared_ptr<RenderResource> render_resource)
    {
        m_render_resource = render_resource;

        if (!m_is_initialized || !m_ray_tracing_enabled)
        {
            return;
        }

        // 更新加速结构
        updateAccelerationStructures();

        // 更新描述符集
        updateDescriptorSet();
    }

    /**
     * @brief 执行光线追踪渲染
     */
    void RayTracingPass::draw(RHICommandBuffer* command_buffer)
    {
        // 默认实现，调用带索引的版本
        drawRayTracing(0);
    }

    /**
     * @brief 执行光线追踪渲染（带交换链图像索引）
     */
    void RayTracingPass::drawRayTracing(uint32_t swapchain_image_index)
    {
        if (!m_is_initialized || !m_ray_tracing_enabled || !m_render_resource)
        {
            return;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (!command_buffer)
        {
            LOG_ERROR("[RayTracingPass] Failed to get current command buffer");
            return;
        }

        // 更新uniform数据
        uint32_t current_frame = m_rhi->getCurrentFrameIndex();
        RayTracingUniformData uniform_data{};
        
        // 获取相机数据
        auto camera = m_render_resource->getCamera();
        if (camera)
        {
            uniform_data.view_inverse = glm::inverse(camera->getViewMatrix());
            uniform_data.proj_inverse = glm::inverse(camera->getPersProjMatrix());
        }
        else
        {
            uniform_data.view_inverse = glm::mat4(1.0f);
            uniform_data.proj_inverse = glm::mat4(1.0f);
        }

        // 设置光源数据
        uniform_data.light_position = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
        uniform_data.light_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        uniform_data.max_depth = m_max_ray_depth;
        uniform_data.samples_per_pixel = m_samples_per_pixel;
        uniform_data.time = static_cast<float>(std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());

        // 更新uniform缓冲区
        memcpy(m_uniform_buffers_mapped[current_frame], &uniform_data, sizeof(RayTracingUniformData));

        // 绑定光线追踪管线
        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_ray_tracing_pipeline);

        // 绑定描述符集
        if (!m_descriptor_infos.empty() && m_descriptor_infos[0].descriptor_set)
        {
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                m_ray_tracing_pipeline_layout, 0, 1, &m_descriptor_infos[0].descriptor_set, 0, nullptr);
        }

        // 调度光线追踪
        auto swapchain_info = m_rhi->getSwapchainInfo();
        uint32_t width = swapchain_info.extent.width;
        uint32_t height = swapchain_info.extent.height;

        // 设置光线追踪信息结构体
        RHITraceRaysInfo trace_rays_info = {};
        trace_rays_info.sType = RHI_STRUCTURE_TYPE_TRACE_RAYS_INFO_KHR;
        trace_rays_info.pNext = nullptr;
        
        // 获取着色器绑定表地址并设置
        if (m_raygen_shader_binding_table)
        {
            trace_rays_info.raygenShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_raygen_shader_binding_table);
            trace_rays_info.raygenShaderBindingTableSize = m_rhi->getRayTracingShaderGroupHandleSize();
            trace_rays_info.raygenShaderBindingTableStride = m_rhi->getRayTracingShaderGroupHandleSize();
        }

        if (m_miss_shader_binding_table)
        {
            trace_rays_info.missShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_miss_shader_binding_table);
            trace_rays_info.missShaderBindingTableSize = m_rhi->getRayTracingShaderGroupHandleSize() * 2; // 两个miss着色器
            trace_rays_info.missShaderBindingTableStride = m_rhi->getRayTracingShaderGroupHandleSize();
        }

        if (m_hit_shader_binding_table)
        {
            trace_rays_info.hitShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_hit_shader_binding_table);
            trace_rays_info.hitShaderBindingTableSize = m_rhi->getRayTracingShaderGroupHandleSize();
            trace_rays_info.hitShaderBindingTableStride = m_rhi->getRayTracingShaderGroupHandleSize();
        }
        
        // Callable 着色器绑定表（未使用）
        trace_rays_info.callableShaderBindingTableAddress = 0;
        trace_rays_info.callableShaderBindingTableSize = 0;
        trace_rays_info.callableShaderBindingTableStride = 0;
        
        // 光线追踪分辨率
        trace_rays_info.width = width;
        trace_rays_info.height = height;
        trace_rays_info.depth = 1;

        // 调度光线追踪
        m_rhi->cmdTraceRays(command_buffer, &trace_rays_info);
    }

    /**
     * @brief 更新加速结构
     */
    void RayTracingPass::updateAccelerationStructures()
    {
        if (!m_render_resource)
        {
            return;
        }

        // 更新光线追踪资源中的加速结构
        m_render_resource->updateRayTracingAccelerationStructures();
    }

    /**
     * @brief 设置光线追踪输出图像
     */
    void RayTracingPass::setOutputImage(RHIImage* output_image)
    {
        m_output_image = output_image;
    }

    /**
     * @brief 设置光线追踪参数
     */
    void RayTracingPass::setRayTracingParams(uint32_t max_depth, uint32_t samples_per_pixel)
    {
        m_max_ray_depth = max_depth;
        m_samples_per_pixel = samples_per_pixel;
    }

    /**
     * @brief 在交换链重建后更新资源
     */
    void RayTracingPass::updateAfterFramebufferRecreate()
    {
        if (!m_is_initialized)
        {
            return;
        }

        // 重新创建输出图像
        createOutputImage();

        // 更新描述符集
        updateDescriptorSet();
    }

    /**
     * @brief 设置光线追踪描述符集布局
     */
    void RayTracingPass::setupDescriptorSetLayout()
    {
        // 创建描述符集布局绑定
        std::vector<RHIDescriptorSetLayoutBinding> bindings;

        // 绑定0: 加速结构 (TLAS)
        RHIDescriptorSetLayoutBinding tlas_binding{};
        tlas_binding.binding = 0;
        tlas_binding.descriptorType = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        tlas_binding.descriptorCount = 1;
        tlas_binding.stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        bindings.push_back(tlas_binding);

        // 绑定1: 输出图像
        RHIDescriptorSetLayoutBinding output_image_binding{};
        output_image_binding.binding = 1;
        output_image_binding.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        output_image_binding.descriptorCount = 1;
        output_image_binding.stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;
        bindings.push_back(output_image_binding);

        // 绑定2: Uniform缓冲区
        RHIDescriptorSetLayoutBinding uniform_binding{};
        uniform_binding.binding = 2;
        uniform_binding.descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_binding.descriptorCount = 1;
        uniform_binding.stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | RHI_SHADER_STAGE_MISS_BIT_KHR;
        bindings.push_back(uniform_binding);

        // 绑定3: 顶点缓冲区
        RHIDescriptorSetLayoutBinding vertex_binding{};
        vertex_binding.binding = 3;
        vertex_binding.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertex_binding.descriptorCount = 1;
        vertex_binding.stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        bindings.push_back(vertex_binding);

        // 绑定4: 索引缓冲区
        RHIDescriptorSetLayoutBinding index_binding{};
        index_binding.binding = 4;
        index_binding.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        index_binding.descriptorCount = 1;
        index_binding.stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        bindings.push_back(index_binding);

        // 创建描述符集布局
        RHIDescriptorSetLayoutCreateInfo layout_create_info{};
        layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_create_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_create_info.pBindings = bindings.data();

        m_descriptor_infos.resize(1);
        if (m_rhi->createDescriptorSetLayout(&layout_create_info, m_descriptor_infos[0].layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create descriptor set layout");
        }
    }

    /**
     * @brief 设置光线追踪管线
     */
    void RayTracingPass::setupRayTracingPipeline()
    {
        // 创建管线布局
        RHIPipelineLayoutCreateInfo pipeline_layout_create_info{};
        pipeline_layout_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = &m_descriptor_infos[0].layout;
        pipeline_layout_create_info.pushConstantRangeCount = 0;
        pipeline_layout_create_info.pPushConstantRanges = nullptr;

        if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_ray_tracing_pipeline_layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create pipeline layout");
        }

        // 测试 std::vector 是否正常工作
        std::vector<RHIPipelineShaderStageCreateInfo> my_shader_stages_vector;
        std::vector<RHIRayTracingShaderGroupCreateInfo> my_shader_groups_vector;

        // Raygen着色器
        RHIShader* raygen_shader_module = m_rhi->createShaderModule(RAYTRACING_RGEN);
        if (!raygen_shader_module)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create raygen shader module");
        }

        RHIPipelineShaderStageCreateInfo raygen_stage{};
        raygen_stage.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        raygen_stage.stage = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;
        raygen_stage.module = raygen_shader_module;
        raygen_stage.pName = "main";
        my_shader_stages_vector.push_back(raygen_stage);
        
        // 创建raygen组
        RHIRayTracingShaderGroupCreateInfo raygen_group{};
        raygen_group.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        raygen_group.type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        raygen_group.generalShader = 0; // raygen着色器在stages数组中的索引
        raygen_group.closestHitShader = RHI_SHADER_UNUSED_KHR;
        raygen_group.anyHitShader = RHI_SHADER_UNUSED_KHR;
        raygen_group.intersectionShader = RHI_SHADER_UNUSED_KHR;
        my_shader_groups_vector.push_back(raygen_group);

        // Miss着色器
        RHIShader* miss_shader_module = m_rhi->createShaderModule(RAYTRACING_RMISS);
        if (!miss_shader_module)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create miss shader module");
        }

        RHIPipelineShaderStageCreateInfo miss_stage{};
        miss_stage.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        miss_stage.stage = RHI_SHADER_STAGE_MISS_BIT_KHR;
        miss_stage.module = miss_shader_module;
        miss_stage.pName = "main";
        my_shader_stages_vector.push_back(miss_stage);
        
        // Miss着色器组
        RHIRayTracingShaderGroupCreateInfo miss_group{};
        miss_group.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        miss_group.type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        miss_group.generalShader = 1; // miss着色器在stages数组中的索引
        miss_group.closestHitShader = RHI_SHADER_UNUSED_KHR;
        miss_group.anyHitShader = RHI_SHADER_UNUSED_KHR;
        miss_group.intersectionShader = RHI_SHADER_UNUSED_KHR;
        my_shader_groups_vector.push_back(miss_group);

        // Shadow miss着色器
        RHIShader* shadow_miss_shader_module = m_rhi->createShaderModule(SHADOW_RMISS);
        if (!shadow_miss_shader_module)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create shadow miss shader module");
        }

        RHIPipelineShaderStageCreateInfo shadow_miss_stage{};
        shadow_miss_stage.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shadow_miss_stage.stage = RHI_SHADER_STAGE_MISS_BIT_KHR;
        shadow_miss_stage.module = shadow_miss_shader_module;
        shadow_miss_stage.pName = "main";
        my_shader_stages_vector.push_back(shadow_miss_stage);
        
        // Shadow miss着色器组
        RHIRayTracingShaderGroupCreateInfo shadow_miss_group{};
        shadow_miss_group.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shadow_miss_group.type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shadow_miss_group.generalShader = 2; // shadow miss着色器在stages数组中的索引
        shadow_miss_group.closestHitShader = RHI_SHADER_UNUSED_KHR;
        shadow_miss_group.anyHitShader = RHI_SHADER_UNUSED_KHR;
        shadow_miss_group.intersectionShader = RHI_SHADER_UNUSED_KHR;
        my_shader_groups_vector.push_back(shadow_miss_group);

        // Closest hit着色器
        RHIShader* closesthit_shader_module = m_rhi->createShaderModule(RAYTRACING_RCHIT);
        if (!closesthit_shader_module)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create closest hit shader module");
        }

        RHIPipelineShaderStageCreateInfo closesthit_stage{};
        closesthit_stage.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        closesthit_stage.stage = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        closesthit_stage.module = closesthit_shader_module;
        closesthit_stage.pName = "main";
        my_shader_stages_vector.push_back(closesthit_stage);
        
        // Closest hit着色器组
        RHIRayTracingShaderGroupCreateInfo closesthit_group{};
        closesthit_group.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        closesthit_group.type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        closesthit_group.generalShader = RHI_SHADER_UNUSED_KHR;
        closesthit_group.closestHitShader = 3; // closest hit着色器在stages数组中的索引
        closesthit_group.anyHitShader = RHI_SHADER_UNUSED_KHR;
        closesthit_group.intersectionShader = RHI_SHADER_UNUSED_KHR;
        my_shader_groups_vector.push_back(closesthit_group);

        // 创建光线追踪管线
        RHIRayTracingPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipeline_create_info.stageCount = static_cast<uint32_t>(my_shader_stages_vector.size());
        pipeline_create_info.pStages = my_shader_stages_vector.data();
        pipeline_create_info.groupCount = static_cast<uint32_t>(my_shader_groups_vector.size());
        pipeline_create_info.pGroups = my_shader_groups_vector.data();
        pipeline_create_info.maxPipelineRayRecursionDepth = m_max_ray_depth;
        pipeline_create_info.layout = m_ray_tracing_pipeline_layout;

        if (m_rhi->createRayTracingPipelinesKHR(1, &pipeline_create_info, m_ray_tracing_pipeline) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create ray tracing pipeline");
        }

        // 清理着色器模块
        m_rhi->destroyShaderModule(raygen_shader_module);
        m_rhi->destroyShaderModule(miss_shader_module);
        m_rhi->destroyShaderModule(shadow_miss_shader_module);
        m_rhi->destroyShaderModule(closesthit_shader_module);
    }

    /**
     * @brief 设置光线追踪描述符集
     */
    void RayTracingPass::setupDescriptorSet()
    {
        // 分配描述符集
        if (m_rhi->allocateDescriptorSets(m_descriptor_infos[0].layout, m_descriptor_infos[0].descriptor_set) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate descriptor set");
        }
    }

    /**
     * @brief 创建光线追踪输出图像
     */
    void RayTracingPass::createOutputImage()
    {
        auto swapchain_info = m_rhi->getSwapchainInfo();
        uint32_t width = swapchain_info.extent.width;
        uint32_t height = swapchain_info.extent.height;

        // 清理旧的输出图像
        if (m_output_image_view)
        {
            m_rhi->destroyImageView(m_output_image_view);
            m_output_image_view = nullptr;
        }
        if (m_output_image)
        {
            m_rhi->destroyImage(m_output_image);
            m_output_image = nullptr;
        }
        if (m_output_image_memory)
        {
            m_rhi->freeMemory(m_output_image_memory);
            m_output_image_memory = nullptr;
        }

        // 创建输出图像
        RHIImageCreateInfo image_create_info{};
        image_create_info.sType = RHI_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.imageType = RHI_IMAGE_TYPE_2D;
        image_create_info.extent.width = width;
        image_create_info.extent.height = height;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.format = RHI_FORMAT_R8G8B8A8_UNORM;
        image_create_info.tiling = RHI_IMAGE_TILING_OPTIMAL;
        image_create_info.initialLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        image_create_info.usage = RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image_create_info.samples = RHI_SAMPLE_COUNT_1_BIT;
        image_create_info.sharingMode = RHI_SHARING_MODE_EXCLUSIVE;

        if (m_rhi->createImage(&image_create_info, m_output_image) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create output image");
        }

        // 分配图像内存
        RHIMemoryRequirements mem_requirements;
        m_rhi->getImageMemoryRequirements(m_output_image, &mem_requirements);

        RHIMemoryAllocateInfo alloc_info{};
        alloc_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = m_rhi->findMemoryType(mem_requirements.memoryTypeBits, RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (m_rhi->allocateMemory(&alloc_info, m_output_image_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate output image memory");
        }

        m_rhi->bindImageMemory(m_output_image, m_output_image_memory, 0);

        // 创建图像视图
        RHIImageViewCreateInfo view_create_info{};
        view_create_info.sType = RHI_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = m_output_image;
        view_create_info.viewType = RHI_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = RHI_FORMAT_R8G8B8A8_UNORM;
        view_create_info.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = 1;

        if (m_rhi->createImageView(&view_create_info, m_output_image_view) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create output image view");
        }
    }

    /**
     * @brief 更新描述符集
     */
    void RayTracingPass::updateDescriptorSet()
    {
        if (!m_render_resource || !m_descriptor_infos[0].descriptor_set)
        {
            return;
        }

        std::vector<RHIWriteDescriptorSet> descriptor_writes;
        uint32_t current_frame = m_rhi->getCurrentFrameIndex();

        // 更新加速结构绑定
        auto& ray_tracing_resource = m_render_resource->getRayTracingResource();
        if (ray_tracing_resource.tlas)
        {
            RHIWriteDescriptorSetAccelerationStructureKHR tlas_info{};
            tlas_info.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            tlas_info.accelerationStructureCount = 1;
            tlas_info.pAccelerationStructures = &ray_tracing_resource.tlas;

            RHIWriteDescriptorSet tlas_write{};
            tlas_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            tlas_write.pNext = &tlas_info;
            tlas_write.dstSet = m_descriptor_infos[0].descriptor_set;
            tlas_write.dstBinding = 0;
            tlas_write.dstArrayElement = 0;
            tlas_write.descriptorType = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            tlas_write.descriptorCount = 1;
            descriptor_writes.push_back(tlas_write);
        }

        // 更新输出图像绑定
        if (m_output_image_view)
        {
            RHIDescriptorImageInfo image_info{};
            image_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;
            image_info.imageView = m_output_image_view;
            image_info.sampler = nullptr;

            RHIWriteDescriptorSet image_write{};
            image_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            image_write.dstSet = m_descriptor_infos[0].descriptor_set;
            image_write.dstBinding = 1;
            image_write.dstArrayElement = 0;
            image_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            image_write.descriptorCount = 1;
            image_write.pImageInfo = &image_info;
            descriptor_writes.push_back(image_write);
        }

        // 更新uniform缓冲区绑定
        if (current_frame < m_uniform_buffers.size() && m_uniform_buffers[current_frame])
        {
            RHIDescriptorBufferInfo buffer_info{};
            buffer_info.buffer = m_uniform_buffers[current_frame];
            buffer_info.offset = 0;
            buffer_info.range = sizeof(RayTracingUniformData);

            RHIWriteDescriptorSet buffer_write{};
            buffer_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            buffer_write.dstSet = m_descriptor_infos[0].descriptor_set;
            buffer_write.dstBinding = 2;
            buffer_write.dstArrayElement = 0;
            buffer_write.descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            buffer_write.descriptorCount = 1;
            buffer_write.pBufferInfo = &buffer_info;
            descriptor_writes.push_back(buffer_write);
        }

        // 更新顶点和索引缓冲区绑定
        if (ray_tracing_resource.vertex_buffer && ray_tracing_resource.index_buffer)
        {
            // 顶点缓冲区
            RHIDescriptorBufferInfo vertex_buffer_info{};
            vertex_buffer_info.buffer = ray_tracing_resource.vertex_buffer;
            vertex_buffer_info.offset = 0;
            vertex_buffer_info.range = RHI_WHOLE_SIZE;

            RHIWriteDescriptorSet vertex_write{};
            vertex_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertex_write.dstSet = m_descriptor_infos[0].descriptor_set;
            vertex_write.dstBinding = 3;
            vertex_write.dstArrayElement = 0;
            vertex_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertex_write.descriptorCount = 1;
            vertex_write.pBufferInfo = &vertex_buffer_info;
            descriptor_writes.push_back(vertex_write);

            // 索引缓冲区
            RHIDescriptorBufferInfo index_buffer_info{};
            index_buffer_info.buffer = ray_tracing_resource.index_buffer;
            index_buffer_info.offset = 0;
            index_buffer_info.range = RHI_WHOLE_SIZE;

            RHIWriteDescriptorSet index_write{};
            index_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            index_write.dstSet = m_descriptor_infos[0].descriptor_set;
            index_write.dstBinding = 4;
            index_write.dstArrayElement = 0;
            index_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            index_write.descriptorCount = 1;
            index_write.pBufferInfo = &index_buffer_info;
            descriptor_writes.push_back(index_write);
        }

        // 更新描述符集
        if (!descriptor_writes.empty())
        {
            m_rhi->updateDescriptorSets(static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
        }
    }

    /**
     * @brief 创建着色器绑定表
     */
    void RayTracingPass::createShaderBindingTable()
    {
        uint32_t handle_size = m_rhi->getRayTracingShaderGroupHandleSize();
        uint32_t handle_alignment = m_rhi->getRayTracingShaderGroupBaseAlignment();
        uint32_t aligned_handle_size = (handle_size + handle_alignment - 1) & ~(handle_alignment - 1);

        // 获取着色器组句柄
        uint32_t group_count = 4; // raygen, miss, shadow_miss, closesthit
        std::vector<uint8_t> shader_handle_storage(group_count * handle_size);
        if (m_rhi->getRayTracingShaderGroupHandlesKHR(m_ray_tracing_pipeline, 0, group_count, 
            shader_handle_storage.size(), shader_handle_storage.data()) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to get ray tracing shader group handles");
        }

        // 创建raygen着色器绑定表
        RHIBufferCreateInfo sbt_buffer_create_info{};
        sbt_buffer_create_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbt_buffer_create_info.size = aligned_handle_size;
        sbt_buffer_create_info.usage = RHI_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        sbt_buffer_create_info.sharingMode = RHI_SHARING_MODE_EXCLUSIVE;

        if (m_rhi->createBuffer(&sbt_buffer_create_info, m_raygen_shader_binding_table) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create raygen shader binding table");
        }

        // 分配内存并绑定
        RHIMemoryRequirements mem_requirements;
        m_rhi->getBufferMemoryRequirements(m_raygen_shader_binding_table, &mem_requirements);

        RHIMemoryAllocateInfo alloc_info{};
        alloc_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = m_rhi->findMemoryType(mem_requirements.memoryTypeBits,
            RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (m_rhi->allocateMemory(&alloc_info, m_raygen_sbt_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate raygen SBT memory");
        }

        m_rhi->bindBufferMemory(m_raygen_shader_binding_table, m_raygen_sbt_memory, 0);

        // 复制句柄数据
        void* mapped_memory;
        m_rhi->mapMemory(m_raygen_sbt_memory, 0, aligned_handle_size, 0, &mapped_memory);
        memcpy(mapped_memory, shader_handle_storage.data(), handle_size);
        m_rhi->unmapMemory(m_raygen_sbt_memory);

        // 创建miss着色器绑定表（包含两个miss着色器）
        sbt_buffer_create_info.size = aligned_handle_size * 2;
        if (m_rhi->createBuffer(&sbt_buffer_create_info, m_miss_shader_binding_table) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create miss shader binding table");
        }

        m_rhi->getBufferMemoryRequirements(m_miss_shader_binding_table, &mem_requirements);
        alloc_info.allocationSize = mem_requirements.size;

        if (m_rhi->allocateMemory(&alloc_info, m_miss_sbt_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate miss SBT memory");
        }

        m_rhi->bindBufferMemory(m_miss_shader_binding_table, m_miss_sbt_memory, 0);

        m_rhi->mapMemory(m_miss_sbt_memory, 0, aligned_handle_size * 2, 0, &mapped_memory);
        // 复制miss着色器句柄
        memcpy(mapped_memory, shader_handle_storage.data() + handle_size, handle_size);
        // 复制shadow miss着色器句柄
        memcpy(static_cast<uint8_t*>(mapped_memory) + aligned_handle_size, 
               shader_handle_storage.data() + handle_size * 2, handle_size);
        m_rhi->unmapMemory(m_miss_sbt_memory);

        // 创建hit着色器绑定表
        sbt_buffer_create_info.size = aligned_handle_size;
        if (m_rhi->createBuffer(&sbt_buffer_create_info, m_hit_shader_binding_table) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create hit shader binding table");
        }

        m_rhi->getBufferMemoryRequirements(m_hit_shader_binding_table, &mem_requirements);
        alloc_info.allocationSize = mem_requirements.size;

        if (m_rhi->allocateMemory(&alloc_info, m_hit_sbt_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate hit SBT memory");
        }

        m_rhi->bindBufferMemory(m_hit_shader_binding_table, m_hit_sbt_memory, 0);

        m_rhi->mapMemory(m_hit_sbt_memory, 0, aligned_handle_size, 0, &mapped_memory);
        memcpy(mapped_memory, shader_handle_storage.data() + handle_size * 3, handle_size);
        m_rhi->unmapMemory(m_hit_sbt_memory);
    }

} // namespace Elish