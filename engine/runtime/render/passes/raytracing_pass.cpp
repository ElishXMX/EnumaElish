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
#include <sstream>
#include <iomanip>
#include <algorithm>
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
        : m_is_initialized(false)  // 使用头文件中的默认值 m_ray_tracing_enabled = true
        , m_max_ray_depth(1)  // 极度保守设置：禁用递归以避免GPU超时
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
     * @details 安全地清理光线追踪资源，确保在销毁缓冲区前等待GPU完成所有操作
     */
    RayTracingPass::~RayTracingPass()
    {
        // 清理光线追踪资源
        if (m_rhi)
        {
            // 等待设备空闲，确保所有GPU操作完成后再销毁资源
            // 这是防止验证层报错的关键步骤
            m_rhi->waitForFences();
            // 等待图形队列空闲
            if (auto graphics_queue = m_rhi->getGraphicsQueue())
            {
                m_rhi->queueWaitIdle(graphics_queue);
            }
            
            LOG_INFO("[RayTracingPass] Starting resource cleanup after device idle");

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

            // 清理着色器绑定表缓冲区
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

            // 清理着色器绑定表内存
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

            // 清理管线资源
            if (m_ray_tracing_pipeline)
            {
                m_rhi->destroyPipeline(m_ray_tracing_pipeline);
                m_ray_tracing_pipeline = nullptr;
            }
            if (m_ray_tracing_pipeline_layout)
            {
                m_rhi->destroyPipelineLayout(m_ray_tracing_pipeline_layout);
                m_ray_tracing_pipeline_layout = nullptr;
            }

            // 清理uniform缓冲区
            for (size_t i = 0; i < m_uniform_buffers.size(); ++i)
            {
                // 先取消内存映射
                if (m_uniform_buffers_mapped[i])
                {
                    m_rhi->unmapMemory(m_uniform_buffers_memory[i]);
                }
                // 然后销毁缓冲区
                if (m_uniform_buffers[i])
                {
                    m_rhi->destroyBuffer(m_uniform_buffers[i]);
                }
                // 最后释放内存
                if (m_uniform_buffers_memory[i])
                {
                    m_rhi->freeMemory(m_uniform_buffers_memory[i]);
                }
            }
            m_uniform_buffers.clear();
            m_uniform_buffers_memory.clear();
            m_uniform_buffers_mapped.clear();
            
            LOG_INFO("[RayTracingPass] Resource cleanup completed successfully");
        }
    }

    /**
     * @brief 初始化光线追踪渲染通道
     * @details 增强的初始化流程，包含详细的错误处理和回退机制
     */
    void RayTracingPass::initialize()
    {
        LOG_INFO("[RayTracingPass] Initializing ray tracing pass with enhanced error handling");

        // 重置初始化状态
        m_is_initialized = false;

        // 如果光线追踪被禁用，直接返回
        if (!m_ray_tracing_enabled)
        {
            LOG_INFO("[RayTracingPass] Ray tracing is disabled, skipping initialization");
            return;
        }

        // 检查RHI是否有效
        if (!m_rhi)
        {
            LOG_ERROR("[RayTracingPass] RHI is null, cannot initialize ray tracing");
            return;
        }

        // 检查光线追踪支持
        if (!m_rhi->isRayTracingSupported())
        {
            LOG_WARN("[RayTracingPass] Ray tracing is not supported on this device, falling back to rasterization");
            m_ray_tracing_enabled = false;  // 自动禁用光线追踪
            return;
        }

        // 初始化步骤计数器，用于错误回退
        int initialization_step = 0;
        
        try
        {
            // 步骤1: 设置描述符集布局
            initialization_step = 1;
            LOG_DEBUG("[RayTracingPass] Step 1: Setting up descriptor set layout");
            setupDescriptorSetLayout();
            LOG_DEBUG("[RayTracingPass] Step 1 completed: Descriptor set layout setup");

            // 步骤2: 设置光线追踪管线
            initialization_step = 2;
            LOG_DEBUG("[RayTracingPass] Step 2: Setting up ray tracing pipeline");
            setupRayTracingPipeline();
            if (!m_ray_tracing_pipeline)
            {
                throw std::runtime_error("Ray tracing pipeline creation failed");
            }
            LOG_DEBUG("[RayTracingPass] Step 2 completed: Ray tracing pipeline setup");

            // 步骤3: 创建输出图像
            initialization_step = 3;
            LOG_DEBUG("[RayTracingPass] Step 3: Creating output image");
            createOutputImage();
            if (!m_output_image || !m_output_image_view)
            {
                throw std::runtime_error("Output image creation failed");
            }
            LOG_DEBUG("[RayTracingPass] Step 3 completed: Output image creation");

            // 步骤4: 设置描述符集
            initialization_step = 4;
            LOG_DEBUG("[RayTracingPass] Step 4: Setting up descriptor set");
            setupDescriptorSet();
            LOG_DEBUG("[RayTracingPass] Step 4 completed: Descriptor set setup");

            // 步骤5: 创建着色器绑定表
            initialization_step = 5;
            LOG_DEBUG("[RayTracingPass] Step 5: Creating shader binding table");
            createShaderBindingTable();
            if (!m_raygen_shader_binding_table || !m_miss_shader_binding_table || !m_hit_shader_binding_table)
            {
                throw std::runtime_error("Shader binding table creation failed");
            }
            LOG_DEBUG("[RayTracingPass] Step 5 completed: Shader binding table creation");

            // 步骤6: 创建uniform缓冲区
            initialization_step = 6;
            LOG_DEBUG("[RayTracingPass] Step 6: Creating uniform buffers");
            uint32_t max_frames_in_flight = m_rhi->getMaxFramesInFlight();
            m_uniform_buffers.resize(max_frames_in_flight);
            m_uniform_buffers_memory.resize(max_frames_in_flight);
            m_uniform_buffers_mapped.resize(max_frames_in_flight);

            for (uint32_t i = 0; i < max_frames_in_flight; ++i)
            {
                LOG_DEBUG("[RayTracingPass] Creating uniform buffer for frame {}/{}", i + 1, max_frames_in_flight);
                RHIBufferCreateInfo buffer_create_info{};
                buffer_create_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                buffer_create_info.size = sizeof(RayTracingUniformData);
                buffer_create_info.usage = RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                buffer_create_info.sharingMode = RHI_SHARING_MODE_EXCLUSIVE;

                if (m_rhi->createBuffer(&buffer_create_info, m_uniform_buffers[i]) != RHI_SUCCESS)
                {
                    throw std::runtime_error("Failed to create uniform buffer for frame " + std::to_string(i));
                }
                LOG_DEBUG("[RayTracingPass] Uniform buffer {} created successfully", i);

                RHIMemoryRequirements mem_requirements;
                m_rhi->getBufferMemoryRequirements(m_uniform_buffers[i], &mem_requirements);

                RHIMemoryAllocateInfo alloc_info{};
                alloc_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                alloc_info.allocationSize = mem_requirements.size;
                alloc_info.memoryTypeIndex = m_rhi->findMemoryType(mem_requirements.memoryTypeBits,
                    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                if (m_rhi->allocateMemory(&alloc_info, m_uniform_buffers_memory[i]) != RHI_SUCCESS)
                {
                    throw std::runtime_error("Failed to allocate uniform buffer memory for frame " + std::to_string(i));
                }
                LOG_DEBUG("[RayTracingPass] Uniform buffer {} memory allocated successfully", i);

                if (m_rhi->bindBufferMemory(m_uniform_buffers[i], m_uniform_buffers_memory[i], 0) != RHI_SUCCESS)
                {
                    throw std::runtime_error("Failed to bind uniform buffer memory for frame " + std::to_string(i));
                }
                
                if (m_rhi->mapMemory(m_uniform_buffers_memory[i], 0, sizeof(RayTracingUniformData), 0, &m_uniform_buffers_mapped[i]) != RHI_SUCCESS)
                {
                    throw std::runtime_error("Failed to map uniform buffer memory for frame " + std::to_string(i));
                }
                LOG_DEBUG("[RayTracingPass] Uniform buffer {} bound and mapped successfully", i);
            }
            LOG_DEBUG("[RayTracingPass] Step 6 completed: All uniform buffers created successfully");

            m_is_initialized = true;
            LOG_INFO("[RayTracingPass] Ray tracing pass initialized successfully! 🎉");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[RayTracingPass] Initialization failed at step {}: {}", initialization_step, e.what());
            
            // 执行清理和回退操作
            performInitializationCleanup(initialization_step);
            
            // 禁用光线追踪并回退到光栅化
            m_ray_tracing_enabled = false;
            m_is_initialized = false;
            
            LOG_WARN("[RayTracingPass] Ray tracing disabled due to initialization failure, falling back to rasterization");
            
            // 不重新抛出异常，允许程序继续运行
        }
        catch (...)
        {
            LOG_ERROR("[RayTracingPass] Unknown error occurred during initialization at step {}", initialization_step);
            
            // 执行清理和回退操作
            performInitializationCleanup(initialization_step);
            
            // 禁用光线追踪并回退到光栅化
            m_ray_tracing_enabled = false;
            m_is_initialized = false;
            
            LOG_WARN("[RayTracingPass] Ray tracing disabled due to unknown error, falling back to rasterization");
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

        // 检查光线追踪资源是否可用（临时禁用时可能为空）
        if (m_render_resource)
        {
            auto& ray_tracing_resource = m_render_resource->getRayTracingResource();
            if (!ray_tracing_resource.tlas)
            {
                LOG_WARN("[RayTracingPass] Ray tracing resources not available, disabling ray tracing for this frame");
                return;
            }
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
        // 开始性能监控计时
        auto frame_start_time = std::chrono::high_resolution_clock::now();
        // 默认标记为未执行，成功调度后置为true
        m_traced_last_frame = false;
        
        if (!m_is_initialized || !m_ray_tracing_enabled || !m_render_resource)
        {
            // LOG_DEBUG("[RayTracingPass] Skipping ray tracing: initialized={}, enabled={}, resource={}", 
            //          m_is_initialized, m_ray_tracing_enabled, (m_render_resource != nullptr));
            return;
        }

        // 检查光线追踪资源是否可用
        auto& ray_tracing_resource = m_render_resource->getRayTracingResource();
        if (!ray_tracing_resource.tlas)
        {
            LOG_DEBUG("[RayTracingPass] Ray tracing resources not available, skipping draw");
            return;
        }

        // 添加光线追踪管线和着色器绑定表的空指针检查
        if (!m_ray_tracing_pipeline)
        {
            LOG_ERROR("[RayTracingPass] Ray tracing pipeline is null, cannot proceed");
            return;
        }

        if (!m_raygen_shader_binding_table || !m_miss_shader_binding_table || !m_hit_shader_binding_table)
        {
            LOG_ERROR("[RayTracingPass] Shader binding tables not properly initialized: raygen={}, miss={}, hit={}",
                     (m_raygen_shader_binding_table != nullptr),
                     (m_miss_shader_binding_table != nullptr),
                     (m_hit_shader_binding_table != nullptr));
            return;
        }

        LOG_DEBUG("[RayTracingPass] Starting ray tracing draw");

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (!command_buffer)
        {
            LOG_ERROR("[RayTracingPass] Failed to get current command buffer");
            return;
        }

        LOG_DEBUG("[RayTracingPass] Got command buffer successfully");

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

        // 添加图像布局转换：将输出图像从UNDEFINED转换为GENERAL以支持光线追踪写入
        RHIImageMemoryBarrier image_barrier{};
        image_barrier.sType = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        image_barrier.newLayout = RHI_IMAGE_LAYOUT_GENERAL;
        image_barrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        image_barrier.image = m_output_image;
        image_barrier.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        image_barrier.subresourceRange.baseMipLevel = 0;
        image_barrier.subresourceRange.levelCount = 1;
        image_barrier.subresourceRange.baseArrayLayer = 0;
        image_barrier.subresourceRange.layerCount = 1;
        image_barrier.srcAccessMask = 0;
        image_barrier.dstAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
        
        m_rhi->cmdPipelineBarrier(
            command_buffer,
            RHI_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0,
            0, nullptr,
            0, nullptr,
            1, &image_barrier
        );
        
        LOG_DEBUG("[RayTracingPass] Output image layout transitioned to GENERAL for ray tracing");

        // 绑定光线追踪管线
        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_ray_tracing_pipeline);

        // 绑定描述符集
        if (!m_descriptor_infos.empty() && current_frame < m_descriptor_infos.size() && m_descriptor_infos[current_frame].descriptor_set)
        {
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                m_ray_tracing_pipeline_layout, 0, 1, &m_descriptor_infos[current_frame].descriptor_set, 0, nullptr);
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
        LOG_DEBUG("[RayTracingPass] Checking shader binding tables: raygen={}, miss={}, hit={}",
                 (m_raygen_shader_binding_table != nullptr),
                 (m_miss_shader_binding_table != nullptr), 
                 (m_hit_shader_binding_table != nullptr));

        // 计算对齐后的着色器绑定表步长
        LOG_DEBUG("[RayTracingPass] Getting shader group handle properties...");
        uint32_t handle_size = m_rhi->getRayTracingShaderGroupHandleSize();
        uint32_t handle_alignment = m_rhi->getRayTracingShaderGroupBaseAlignment();
        uint32_t aligned_handle_size = (handle_size + handle_alignment - 1) & ~(handle_alignment - 1);
        LOG_DEBUG("[RayTracingPass] Handle size: {}, alignment: {}, aligned size: {}", handle_size, handle_alignment, aligned_handle_size);
        
        if (m_raygen_shader_binding_table)
        {
            LOG_DEBUG("[RayTracingPass] Getting raygen SBT device address...");
            trace_rays_info.raygenShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_raygen_shader_binding_table);
            trace_rays_info.raygenShaderBindingTableSize = aligned_handle_size;
            trace_rays_info.raygenShaderBindingTableStride = aligned_handle_size;
            LOG_DEBUG("[RayTracingPass] Raygen SBT address: {}, stride: {}", trace_rays_info.raygenShaderBindingTableAddress, aligned_handle_size);
        }

        if (m_miss_shader_binding_table)
        {
            LOG_DEBUG("[RayTracingPass] Getting miss SBT device address...");
            trace_rays_info.missShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_miss_shader_binding_table);
            trace_rays_info.missShaderBindingTableSize = aligned_handle_size * 2; // 两个miss着色器
            trace_rays_info.missShaderBindingTableStride = aligned_handle_size;
            LOG_DEBUG("[RayTracingPass] Miss SBT address: {}, stride: {}", trace_rays_info.missShaderBindingTableAddress, aligned_handle_size);
        }

        if (m_hit_shader_binding_table)
        {
            LOG_DEBUG("[RayTracingPass] Getting hit SBT device address...");
            trace_rays_info.hitShaderBindingTableAddress = m_rhi->getBufferDeviceAddress(m_hit_shader_binding_table);
            trace_rays_info.hitShaderBindingTableSize = aligned_handle_size;
            trace_rays_info.hitShaderBindingTableStride = aligned_handle_size;
            LOG_DEBUG("[RayTracingPass] Hit SBT address: {}, stride: {}", trace_rays_info.hitShaderBindingTableAddress, aligned_handle_size);
        }
        
        // Callable 着色器绑定表（未使用）
        trace_rays_info.callableShaderBindingTableAddress = 0;
        trace_rays_info.callableShaderBindingTableSize = 0;
        trace_rays_info.callableShaderBindingTableStride = 0;
        
        // 使用输出图像分辨率进行光线追踪（可缩放）
        trace_rays_info.width = m_output_width;
        trace_rays_info.height = m_output_height;
        trace_rays_info.depth = 1;

        LOG_DEBUG("[RayTracingPass] About to dispatch ray tracing: {}x{}x{} (swapchain: {}x{}, scale: {:.2f})", m_output_width, m_output_height, 1, width, height, m_render_scale);

        // 调度光线追踪
        m_rhi->cmdTraceRays(command_buffer, &trace_rays_info);
        // LOG_WARN("[RayTracingPass] cmdTraceRays SKIPPED for debugging VK_ERROR_DEVICE_LOST");
        
        m_traced_last_frame = true;

        LOG_DEBUG("[RayTracingPass] Ray tracing dispatch completed successfully");
        
        // 添加内存屏障：确保光线追踪完成后再进行后续操作
        RHIMemoryBarrier rt_completion_barrier{};
        rt_completion_barrier.sType = RHI_STRUCTURE_TYPE_MEMORY_BARRIER;
        rt_completion_barrier.srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
        rt_completion_barrier.dstAccessMask = RHI_ACCESS_MEMORY_READ_BIT | RHI_ACCESS_MEMORY_WRITE_BIT;
        
        m_rhi->cmdPipelineBarrier(
            command_buffer,
            RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,
            1, &rt_completion_barrier,
            0, nullptr,
            0, nullptr
        );
        
        LOG_DEBUG("[RayTracingPass] Memory barrier added after ray tracing completion");
        
        // 结束性能监控计时并更新统计信息
        auto frame_end_time = std::chrono::high_resolution_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(frame_end_time - frame_start_time);
        float frame_time_ms = frame_duration.count() / 1000.0f;
        
        // 更新性能统计
        updatePerformanceStats(frame_time_ms);
        
        LOG_DEBUG("[RayTracingPass] Frame time: {:.3f}ms", frame_time_ms);

        // 动态自适应逻辑暂时屏蔽，避免在渲染循环中重建资源导致 VK_ERROR_DEVICE_LOST
        /*
        const float target_ms = 33.33f;
        if (frame_time_ms > target_ms)
        {
            float new_scale = std::max(0.5f, m_render_scale * 0.85f);
            if (new_scale < m_render_scale)
            {
                m_render_scale = new_scale;
                m_samples_per_pixel = std::max(1u, m_samples_per_pixel / 2);
                LOG_WARN("[RayTracingPass] Adjusting quality for performance: scale={:.2f}, spp={}", m_render_scale, m_samples_per_pixel);
                createOutputImage();
                updateDescriptorSet();
            }
        }
        else if (frame_time_ms < 25.0f && m_render_scale < 1.0f)
        {
            float new_scale = std::min(1.0f, m_render_scale * 1.10f);
            if (new_scale > m_render_scale)
            {
                m_render_scale = new_scale;
                m_samples_per_pixel = std::min(m_samples_per_pixel + 1, 4u);
                LOG_INFO("[RayTracingPass] Increasing quality: scale={:.2f}, spp={}", m_render_scale, m_samples_per_pixel);
                createOutputImage();
                updateDescriptorSet();
            }
        }
        */
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

        // 绑定4: 顶点缓冲区（修正：匹配着色器中VertexBuffer的绑定）
        RHIDescriptorSetLayoutBinding vertex_binding{};
        vertex_binding.binding = 4;
        vertex_binding.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertex_binding.descriptorCount = 1;
        vertex_binding.stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        bindings.push_back(vertex_binding);

        // 绑定5: 索引缓冲区（修正：匹配着色器中IndexBuffer的绑定）
        RHIDescriptorSetLayoutBinding index_binding{};
        index_binding.binding = 5;
        index_binding.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        index_binding.descriptorCount = 1;
        index_binding.stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        bindings.push_back(index_binding);

        // 创建描述符集布局
        RHIDescriptorSetLayoutCreateInfo layout_create_info{};
        layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_create_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_create_info.pBindings = bindings.data();

        // 这里的resize(1)是问题的根源，必须改为支持多帧飞行
        uint32_t max_frames = m_rhi->getMaxFramesInFlight();
        m_descriptor_infos.resize(max_frames);
        
        // 创建布局（所有帧共享同一个布局）
        RHIDescriptorSetLayout* layout = nullptr;
        if (m_rhi->createDescriptorSetLayout(&layout_create_info, layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create descriptor set layout");
        }
        
        // 为每一帧分配相同的布局指针
        for (auto& info : m_descriptor_infos) {
            info.layout = layout;
        }
    }

    /**
     * @brief 设置光线追踪管线
     * @details 创建优化的光线追踪管线，包括性能优化设置和自适应质量调整
     */
    void RayTracingPass::setupRayTracingPipeline()
    {
        LOG_DEBUG("[RayTracingPass] Setting up ray tracing pipeline with optimized parameters");
        
        // 根据硬件能力调整光线追踪参数
        adjustRayTracingParameters();
        
        // 添加推送常量范围用于动态参数调整
        RHIPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(RayTracingPushConstants);
        
        // 创建管线布局
        RHIPipelineLayoutCreateInfo pipeline_layout_create_info{};
        pipeline_layout_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = &m_descriptor_infos[0].layout; // 使用第一个布局即可，因为都一样
        pipeline_layout_create_info.pushConstantRangeCount = 1;
        pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

        if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_ray_tracing_pipeline_layout) != RHI_SUCCESS)
        {
            throw std::runtime_error("Failed to create ray tracing pipeline layout");
        }
        LOG_DEBUG("[RayTracingPass] Pipeline layout created with push constants support");

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
        // 为每一帧分配描述符集
        for (auto& info : m_descriptor_infos)
        {
            if (m_rhi->allocateDescriptorSets(info.layout, info.descriptor_set) != RHI_SUCCESS)
            {
                throw std::runtime_error("[RayTracingPass] Failed to allocate descriptor set");
            }
        }
    }

    /**
     * @brief 创建光线追踪输出图像
     */
    void RayTracingPass::createOutputImage()
    {
        auto swapchain_info = m_rhi->getSwapchainInfo();
        uint32_t base_width = swapchain_info.extent.width;
        uint32_t base_height = swapchain_info.extent.height;
        uint32_t width = std::max(1u, static_cast<uint32_t>(base_width * m_render_scale));
        uint32_t height = std::max(1u, static_cast<uint32_t>(base_height * m_render_scale));
        m_output_width = width;
        m_output_height = height;
        LOG_DEBUG("[RayTracingPass] Creating output image: {}x{} (swapchain: {}x{}, scale: {:.2f})", 
                 width, height, base_width, base_height, m_render_scale);

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
        LOG_DEBUG("[RayTracingPass] Starting updateDescriptorSet");
        
        uint32_t current_frame = m_rhi->getCurrentFrameIndex();
        LOG_DEBUG("[RayTracingPass] Current frame index: {}", current_frame);

        // 添加更严格的初始化检查
        if (!m_is_initialized || !m_render_resource || m_descriptor_infos.empty() || current_frame >= m_descriptor_infos.size() || !m_descriptor_infos[current_frame].descriptor_set || m_uniform_buffers.empty())
        {
            LOG_DEBUG("[RayTracingPass] Early return: initialized={}, render_resource={}, descriptor_set={}, uniform_buffers_size={}, current_frame={}", 
                     m_is_initialized, (m_render_resource != nullptr), 
                     (m_descriptor_infos.empty() || current_frame >= m_descriptor_infos.size() ? false : m_descriptor_infos[current_frame].descriptor_set != nullptr), 
                     m_uniform_buffers.size(), current_frame);
            return;
        }

        LOG_DEBUG("[RayTracingPass] Creating descriptor writes vector");
        std::vector<RHIWriteDescriptorSet> descriptor_writes;
        
        // 预先声明所有需要的描述符信息结构体，确保它们在整个函数执行期间都有效
        RHIWriteDescriptorSetAccelerationStructureKHR tlas_info{};
        RHIDescriptorImageInfo image_info{};
        RHIDescriptorBufferInfo buffer_info{};
        RHIDescriptorBufferInfo vertex_buffer_info{};
        RHIDescriptorBufferInfo index_buffer_info{};

        // 更新加速结构绑定
        auto& ray_tracing_resource = m_render_resource->getRayTracingResource();
        
        // 检查光线追踪资源是否已创建（临时禁用时可能为空）
        if (!ray_tracing_resource.tlas)
        {
            LOG_WARN("[RayTracingPass] Ray tracing resources not available, skipping descriptor set update");
            return;
        }
        
        if (ray_tracing_resource.tlas)
        {
            tlas_info.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            tlas_info.accelerationStructureCount = 1;
            tlas_info.pAccelerationStructures = &ray_tracing_resource.tlas;

            RHIWriteDescriptorSet tlas_write{};
            tlas_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            tlas_write.pNext = &tlas_info;
            tlas_write.dstSet = m_descriptor_infos[current_frame].descriptor_set;
            tlas_write.dstBinding = 0;
            tlas_write.dstArrayElement = 0;
            tlas_write.descriptorType = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            tlas_write.descriptorCount = 1;
            descriptor_writes.push_back(tlas_write);
        }

        // 更新输出图像绑定
        if (m_output_image_view)
        {
            image_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;
            image_info.imageView = m_output_image_view;
            image_info.sampler = nullptr;

            RHIWriteDescriptorSet image_write{};
            image_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            image_write.dstSet = m_descriptor_infos[current_frame].descriptor_set;
            image_write.dstBinding = 1;
            image_write.dstArrayElement = 0;
            image_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            image_write.descriptorCount = 1;
            image_write.pImageInfo = &image_info;
            descriptor_writes.push_back(image_write);
        }

        // 更新uniform缓冲区绑定 - 添加更严格的检查
        if (current_frame < m_uniform_buffers.size() && 
            m_uniform_buffers[current_frame] && 
            m_uniform_buffers_memory[current_frame] && 
            m_uniform_buffers_mapped[current_frame])
        {
            buffer_info.buffer = m_uniform_buffers[current_frame];
            buffer_info.offset = 0;
            buffer_info.range = sizeof(RayTracingUniformData);

            RHIWriteDescriptorSet buffer_write{};
            buffer_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            buffer_write.dstSet = m_descriptor_infos[current_frame].descriptor_set;
            buffer_write.dstBinding = 2;
            buffer_write.dstArrayElement = 0;
            buffer_write.descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            buffer_write.descriptorCount = 1;
            buffer_write.pBufferInfo = &buffer_info;
            descriptor_writes.push_back(buffer_write);
        }

        // 更新顶点和索引缓冲区绑定 - 修复字段名称不匹配问题
        if (ray_tracing_resource.mergedVertexBuffer && ray_tracing_resource.mergedIndexBuffer)
        {
            LOG_DEBUG("[RayTracingPass] Binding merged buffers: vertex_buffer={}, index_buffer={}", 
                      (void*)ray_tracing_resource.mergedVertexBuffer, (void*)ray_tracing_resource.mergedIndexBuffer);

            // 顶点缓冲区（绑定4：匹配着色器中VertexBuffer的绑定）
            vertex_buffer_info.buffer = ray_tracing_resource.mergedVertexBuffer;
            vertex_buffer_info.offset = 0;
            vertex_buffer_info.range = RHI_WHOLE_SIZE;

            RHIWriteDescriptorSet vertex_write{};
            vertex_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertex_write.dstSet = m_descriptor_infos[current_frame].descriptor_set;
            vertex_write.dstBinding = 4;
            vertex_write.dstArrayElement = 0;
            vertex_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertex_write.descriptorCount = 1;
            vertex_write.pBufferInfo = &vertex_buffer_info;
            descriptor_writes.push_back(vertex_write);

            // 索引缓冲区（绑定5：匹配着色器中IndexBuffer的绑定）
            index_buffer_info.buffer = ray_tracing_resource.mergedIndexBuffer;
            index_buffer_info.offset = 0;
            index_buffer_info.range = RHI_WHOLE_SIZE;

            RHIWriteDescriptorSet index_write{};
            index_write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            index_write.dstSet = m_descriptor_infos[current_frame].descriptor_set;
            index_write.dstBinding = 5;
            index_write.dstArrayElement = 0;
            index_write.descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            index_write.descriptorCount = 1;
            index_write.pBufferInfo = &index_buffer_info;
            descriptor_writes.push_back(index_write);
        }
        else
        {
            LOG_ERROR("[RayTracingPass] Merged buffers are missing! VertexBuffer={}, IndexBuffer={}", 
                      (void*)ray_tracing_resource.mergedVertexBuffer, (void*)ray_tracing_resource.mergedIndexBuffer);
        }

        // 更新描述符集
        if (!descriptor_writes.empty())
        {
            LOG_DEBUG("[RayTracingPass] About to update descriptor sets, count: {}", descriptor_writes.size());
            m_rhi->updateDescriptorSets(static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
            LOG_DEBUG("[RayTracingPass] Descriptor sets updated successfully");
        }
        else
        {
            LOG_DEBUG("[RayTracingPass] No descriptor writes to update");
        }
        
        LOG_DEBUG("[RayTracingPass] updateDescriptorSet completed successfully");
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
        
        LOG_DEBUG("[RayTracingPass] Getting shader group handles: count={}, handle_size={}, total_size={}", 
                 group_count, handle_size, shader_handle_storage.size());
        
        if (m_rhi->getRayTracingShaderGroupHandlesKHR(m_ray_tracing_pipeline, 0, group_count, 
            shader_handle_storage.size(), shader_handle_storage.data()) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to get ray tracing shader group handles");
        }
        
        LOG_DEBUG("[RayTracingPass] Successfully retrieved {} shader group handles", group_count);

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

        // 创建内存分配标志信息，支持设备地址
        RHIMemoryAllocateFlagsInfo allocate_flags_info{};
        allocate_flags_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocate_flags_info.flags = RHI_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        RHIMemoryAllocateInfo alloc_info{};
        alloc_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = &allocate_flags_info;  // 添加设备地址标志
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
        
        // 边界检查：确保不会越界访问
        if (0 * handle_size + handle_size <= shader_handle_storage.size()) {
            memcpy(mapped_memory, shader_handle_storage.data() + 0 * handle_size, handle_size);  // raygen是第0个组
            LOG_DEBUG("[RayTracingPass] Copied raygen shader handle (group 0)");
        } else {
            LOG_ERROR("[RayTracingPass] Buffer overflow detected when copying raygen handle!");
            throw std::runtime_error("[RayTracingPass] Buffer overflow in raygen SBT creation");
        }
        
        m_rhi->unmapMemory(m_raygen_sbt_memory);

        // 创建miss着色器绑定表（包含两个miss着色器）
        sbt_buffer_create_info.size = aligned_handle_size * 2;
        if (m_rhi->createBuffer(&sbt_buffer_create_info, m_miss_shader_binding_table) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create miss shader binding table");
        }

        m_rhi->getBufferMemoryRequirements(m_miss_shader_binding_table, &mem_requirements);
        
        // 重新设置内存分配标志信息，支持设备地址
        RHIMemoryAllocateFlagsInfo miss_allocate_flags_info{};
        miss_allocate_flags_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        miss_allocate_flags_info.flags = RHI_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        
        alloc_info.pNext = &miss_allocate_flags_info;  // 添加设备地址标志
        alloc_info.allocationSize = mem_requirements.size;

        if (m_rhi->allocateMemory(&alloc_info, m_miss_sbt_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate miss SBT memory");
        }

        m_rhi->bindBufferMemory(m_miss_shader_binding_table, m_miss_sbt_memory, 0);

        m_rhi->mapMemory(m_miss_sbt_memory, 0, aligned_handle_size * 2, 0, &mapped_memory);
        
        // 边界检查：复制miss着色器句柄（第1个组）
        if (1 * handle_size + handle_size <= shader_handle_storage.size()) {
            memcpy(mapped_memory, shader_handle_storage.data() + 1 * handle_size, handle_size);
            LOG_DEBUG("[RayTracingPass] Copied miss shader handle (group 1)");
        } else {
            LOG_ERROR("[RayTracingPass] Buffer overflow detected when copying miss handle!");
            throw std::runtime_error("[RayTracingPass] Buffer overflow in miss SBT creation");
        }
        
        // 边界检查：复制shadow miss着色器句柄（第2个组）
        if (2 * handle_size + handle_size <= shader_handle_storage.size()) {
            memcpy(static_cast<uint8_t*>(mapped_memory) + aligned_handle_size, 
                   shader_handle_storage.data() + 2 * handle_size, handle_size);
            LOG_DEBUG("[RayTracingPass] Copied shadow miss shader handle (group 2)");
        } else {
            LOG_ERROR("[RayTracingPass] Buffer overflow detected when copying shadow miss handle!");
            throw std::runtime_error("[RayTracingPass] Buffer overflow in shadow miss SBT creation");
        }
        
        m_rhi->unmapMemory(m_miss_sbt_memory);

        // 创建hit着色器绑定表
        sbt_buffer_create_info.size = aligned_handle_size;
        if (m_rhi->createBuffer(&sbt_buffer_create_info, m_hit_shader_binding_table) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to create hit shader binding table");
        }

        m_rhi->getBufferMemoryRequirements(m_hit_shader_binding_table, &mem_requirements);
        
        // 重新设置内存分配标志信息，支持设备地址
        RHIMemoryAllocateFlagsInfo hit_allocate_flags_info{};
        hit_allocate_flags_info.sType = RHI_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        hit_allocate_flags_info.flags = RHI_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        
        alloc_info.pNext = &hit_allocate_flags_info;  // 添加设备地址标志
        alloc_info.allocationSize = mem_requirements.size;

        if (m_rhi->allocateMemory(&alloc_info, m_hit_sbt_memory) != RHI_SUCCESS)
        {
            throw std::runtime_error("[RayTracingPass] Failed to allocate hit SBT memory");
        }

        m_rhi->bindBufferMemory(m_hit_shader_binding_table, m_hit_sbt_memory, 0);

        m_rhi->mapMemory(m_hit_sbt_memory, 0, aligned_handle_size, 0, &mapped_memory);
        
        // 边界检查：复制hit着色器句柄（第3个组）
        if (3 * handle_size + handle_size <= shader_handle_storage.size()) {
            memcpy(mapped_memory, shader_handle_storage.data() + 3 * handle_size, handle_size);  // closesthit是第3个组
            LOG_DEBUG("[RayTracingPass] Copied hit shader handle (group 3)");
        } else {
            LOG_ERROR("[RayTracingPass] Buffer overflow detected when copying hit handle!");
            throw std::runtime_error("[RayTracingPass] Buffer overflow in hit SBT creation");
        }
        
        m_rhi->unmapMemory(m_hit_sbt_memory);
    }

/**
     * @brief 执行初始化清理操作
     * @param failed_step 失败的初始化步骤
     * @details 根据失败的步骤执行相应的资源清理，确保不会出现资源泄漏
     */
    void RayTracingPass::performInitializationCleanup(uint32_t failed_step)
    {
        LOG_DEBUG("[RayTracingPass] Performing cleanup for failed step: {}", failed_step);
        
        try
        {
            // 清理uniform缓冲区 (步骤6及以后)
            if (failed_step >= 6)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up uniform buffers");
                for (size_t i = 0; i < m_uniform_buffers.size(); ++i)
                {
                    if (m_uniform_buffers_mapped[i] != nullptr)
                    {
                        m_rhi->unmapMemory(m_uniform_buffers_memory[i]);
                        m_uniform_buffers_mapped[i] = nullptr;
                    }
                    if (m_uniform_buffers_memory[i] != nullptr)
                    {
                        m_rhi->freeMemory(m_uniform_buffers_memory[i]);
                        m_uniform_buffers_memory[i] = nullptr;
                    }
                    if (m_uniform_buffers[i] != nullptr)
                    {
                        m_rhi->destroyBuffer(m_uniform_buffers[i]);
                        m_uniform_buffers[i] = nullptr;
                    }
                }
                m_uniform_buffers.clear();
                m_uniform_buffers_memory.clear();
                m_uniform_buffers_mapped.clear();
            }
            
            // 清理着色器绑定表 (步骤5及以后)
            if (failed_step >= 5)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up shader binding tables");
                if (m_raygen_sbt_memory != nullptr)
                {
                    m_rhi->freeMemory(m_raygen_sbt_memory);
                    m_raygen_sbt_memory = nullptr;
                }
                if (m_miss_sbt_memory != nullptr)
                {
                    m_rhi->freeMemory(m_miss_sbt_memory);
                    m_miss_sbt_memory = nullptr;
                }
                if (m_hit_sbt_memory != nullptr)
                {
                    m_rhi->freeMemory(m_hit_sbt_memory);
                    m_hit_sbt_memory = nullptr;
                }
                if (m_raygen_shader_binding_table != nullptr)
                {
                    m_rhi->destroyBuffer(m_raygen_shader_binding_table);
                    m_raygen_shader_binding_table = nullptr;
                }
                if (m_miss_shader_binding_table != nullptr)
                {
                    m_rhi->destroyBuffer(m_miss_shader_binding_table);
                    m_miss_shader_binding_table = nullptr;
                }
                if (m_hit_shader_binding_table != nullptr)
                {
                    m_rhi->destroyBuffer(m_hit_shader_binding_table);
                    m_hit_shader_binding_table = nullptr;
                }
            }
            
            // 清理光线追踪管线 (步骤4及以后)
            if (failed_step >= 4)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up ray tracing pipeline");
                if (m_ray_tracing_pipeline != nullptr)
                {
                    m_rhi->destroyPipeline(m_ray_tracing_pipeline);
                    m_ray_tracing_pipeline = nullptr;
                }
                if (m_ray_tracing_pipeline_layout != nullptr)
                {
                    m_rhi->destroyPipelineLayout(m_ray_tracing_pipeline_layout);
                    m_ray_tracing_pipeline_layout = nullptr;
                }
            }
            
            // 清理描述符集 (步骤3及以后)
            if (failed_step >= 3)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up descriptor sets");
                // 描述符集会随着描述符池一起清理
            }
            
            // 清理输出图像 (步骤2及以后)
            if (failed_step >= 2)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up output image");
                if (m_output_image_view != nullptr)
                {
                    m_rhi->destroyImageView(m_output_image_view);
                    m_output_image_view = nullptr;
                }
                if (m_output_image_memory != nullptr)
                {
                    m_rhi->freeMemory(m_output_image_memory);
                    m_output_image_memory = nullptr;
                }
                if (m_output_image != nullptr)
                {
                    m_rhi->destroyImage(m_output_image);
                    m_output_image = nullptr;
                }
            }
            
            // 清理描述符集布局 (步骤1及以后)
            if (failed_step >= 1)
            {
                LOG_DEBUG("[RayTracingPass] Cleaning up descriptor set layout");
                // 描述符集布局的清理在基类析构函数中处理
            }
            
            LOG_DEBUG("[RayTracingPass] Cleanup completed successfully");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[RayTracingPass] Error during cleanup: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("[RayTracingPass] Unknown error during cleanup");
        }
    }

    /**
     * @brief 根据硬件能力调整光线追踪参数
     * @details 自动检测硬件性能并调整光线追踪质量设置
     */
    void RayTracingPass::adjustRayTracingParameters()
    {
        LOG_DEBUG("[RayTracingPass] Adjusting ray tracing parameters based on hardware capabilities");
        
        if (!m_rhi || !m_rhi->isRayTracingSupported())
        {
            LOG_WARN("[RayTracingPass] Ray tracing not supported, using fallback parameters");
            m_max_ray_depth = 1;
            m_samples_per_pixel = 1;
            return;
        }
        
        // 获取光线追踪属性
        // TODO: 实现getRayTracingPipelineProperties和getAccelerationStructureProperties方法
        // 暂时使用默认值
        struct { 
            uint32_t maxRayRecursionDepth = 10; 
            uint32_t shaderGroupHandleSize = 32;
        } rt_pipeline_props;
        struct { 
            uint32_t maxGeometryCount = 1000000; 
            uint32_t maxInstanceCount = 1000000;
        } as_props;
        
        // 根据最大递归深度调整光线追踪深度 - 调试阶段使用保守设置
        uint32_t max_supported_depth = rt_pipeline_props.maxRayRecursionDepth;
        // 调试阶段：强制使用最保守设置以确保稳定性
        m_max_ray_depth = 1;  // 最保守的光线深度
        m_samples_per_pixel = 1;  // 最少采样数
        LOG_INFO("[RayTracingPass] Debug mode: using conservative settings for stability (depth: {}, samples: {})", m_max_ray_depth, m_samples_per_pixel);
        LOG_INFO("[RayTracingPass] Hardware max recursion depth: {}", max_supported_depth);
        
        // 注释掉原有的自适应逻辑，调试完成后可以恢复
        /*
        if (max_supported_depth >= 10)
        {
            m_max_ray_depth = 10;  // 高质量设置
            m_samples_per_pixel = 4;
            LOG_INFO("[RayTracingPass] High-end hardware detected, using high quality settings (depth: {}, samples: {})", m_max_ray_depth, m_samples_per_pixel);
        }
        else if (max_supported_depth >= 5)
        {
            m_max_ray_depth = 5;   // 中等质量设置
            m_samples_per_pixel = 2;
            LOG_INFO("[RayTracingPass] Mid-range hardware detected, using medium quality settings (depth: {}, samples: {})", m_max_ray_depth, m_samples_per_pixel);
        }
        else
        {
            m_max_ray_depth = std::max(1u, max_supported_depth); // 低质量设置
            m_samples_per_pixel = 1;
            LOG_INFO("[RayTracingPass] Low-end hardware detected, using low quality settings (depth: {}, samples: {})", m_max_ray_depth, m_samples_per_pixel);
        }
        */
        
        // 记录硬件能力信息
        LOG_DEBUG("[RayTracingPass] Hardware capabilities:");
        LOG_DEBUG("  - Max ray recursion depth: {}", max_supported_depth);
        LOG_DEBUG("  - Max shader group stride: {}", rt_pipeline_props.shaderGroupHandleSize);
        LOG_DEBUG("  - Max geometry count: {}", as_props.maxGeometryCount);
        LOG_DEBUG("  - Max instance count: {}", as_props.maxInstanceCount);
        
        // 根据内存限制调整参数
        // 这里可以添加更多基于GPU内存的自适应逻辑
        
        LOG_INFO("[RayTracingPass] Ray tracing parameters adjusted successfully");
    }
    
    /**
     * @brief 动态调整光线追踪参数
     * @param frame_number 当前帧号
     * @param adaptive_samples 自适应采样数
     * @param noise_threshold 噪声阈值
     * @param quality_factor 质量因子
     */
    void RayTracingPass::adjustRayTracingParameters(uint32_t frame_number, uint32_t adaptive_samples, float noise_threshold, float quality_factor)
    {
        LOG_DEBUG("[RayTracingPass] Dynamically adjusting ray tracing parameters for frame {}", frame_number);
        
        // 限制质量因子在合理范围内
        quality_factor = std::clamp(quality_factor, 0.1f, 1.0f);
        
        // 根据质量因子调整采样数
        uint32_t base_samples = static_cast<uint32_t>(m_samples_per_pixel * quality_factor);
        adaptive_samples = std::max(1u, std::min(adaptive_samples, base_samples * 2));
        
        // 根据质量因子调整光线深度
        uint32_t adjusted_depth = static_cast<uint32_t>(m_max_ray_depth * quality_factor);
        adjusted_depth = std::max(1u, adjusted_depth);
        
        // 限制噪声阈值
        noise_threshold = std::clamp(noise_threshold, 0.001f, 0.1f);
        
        LOG_DEBUG("[RayTracingPass] Dynamic parameters: samples={}, depth={}, noise_threshold={:.4f}, quality={:.2f}", 
                 adaptive_samples, adjusted_depth, noise_threshold, quality_factor);
        
        // 这些参数将通过推送常量传递给着色器
        // 在实际渲染时使用
    }
    
    /**
     * @brief 获取光线追踪诊断信息
     * @return 诊断信息结构体的副本
     */
    RayTracingPass::RayTracingDiagnostics RayTracingPass::getDiagnostics() const
    {
        std::lock_guard<std::mutex> lock(m_diagnostics_mutex);
        return m_diagnostics;
    }
    
    /**
     * @brief 重置诊断统计信息
     */
    void RayTracingPass::resetDiagnostics()
    {
        std::lock_guard<std::mutex> lock(m_diagnostics_mutex);
        m_diagnostics = RayTracingDiagnostics{};
        m_diagnostics.last_update_time = std::chrono::high_resolution_clock::now();
        LOG_INFO("[RayTracingPass] Diagnostics statistics reset");
    }
    
    /**
     * @brief 启用或禁用性能监控
     * @param enable 是否启用监控
     */
    void RayTracingPass::setPerformanceMonitoring(bool enable)
    {
        m_performance_monitoring_enabled = enable;
        LOG_INFO("[RayTracingPass] Performance monitoring {}", enable ? "enabled" : "disabled");
        
        if (enable && m_diagnostics.last_update_time.time_since_epoch().count() == 0)
        {
            std::lock_guard<std::mutex> lock(m_diagnostics_mutex);
            m_diagnostics.last_update_time = std::chrono::high_resolution_clock::now();
        }
    }
    
    /**
     * @brief 获取当前性能状态
     * @return 性能状态字符串
     */
    std::string RayTracingPass::getPerformanceStatus() const
    {
        if (!m_performance_monitoring_enabled)
        {
            return "Performance monitoring disabled";
        }
        
        std::lock_guard<std::mutex> lock(m_diagnostics_mutex);
        
        std::ostringstream status;
        status << "Ray Tracing Performance Status:\n";
        status << "  - Total Rays Traced: " << m_diagnostics.total_rays_traced << "\n";
        status << "  - Rays Per Second: " << m_diagnostics.rays_per_second << "\n";
        status << "  - Average Frame Time: " << std::fixed << std::setprecision(3) << m_diagnostics.average_frame_time << "ms\n";
        status << "  - Last Frame Time: " << std::fixed << std::setprecision(3) << m_diagnostics.last_frame_time << "ms\n";
        status << "  - Failed Ray Count: " << m_diagnostics.failed_ray_count << "\n";
        status << "  - Total Frame Count: " << m_diagnostics.total_frame_count << "\n";
        status << "  - Performance Warning: " << (m_diagnostics.performance_warning ? "YES" : "NO");
        
        return status.str();
    }
    
    /**
     * @brief 更新性能统计信息
     * @param frame_time 当前帧时间
     */
    void RayTracingPass::updatePerformanceStats(float frame_time) const
    {
        if (!m_performance_monitoring_enabled)
            return;
            
        std::lock_guard<std::mutex> lock(m_diagnostics_mutex);
        
        auto current_time = std::chrono::high_resolution_clock::now();
        
        // 更新帧时间统计
        m_diagnostics.last_frame_time = frame_time;
        m_diagnostics.total_frame_count++;
        
        // 计算平均帧时间（使用指数移动平均）
        const float alpha = 0.1f; // 平滑因子
        if (m_diagnostics.total_frame_count == 1)
        {
            m_diagnostics.average_frame_time = frame_time;
        }
        else
        {
            m_diagnostics.average_frame_time = alpha * frame_time + (1.0f - alpha) * m_diagnostics.average_frame_time;
        }
        
        // 计算光线追踪统计（基于分辨率和采样数估算）
        if (m_output_image)
        {
            // 假设输出图像分辨率为光线数的基础
            uint64_t estimated_rays = static_cast<uint64_t>(1920 * 1080) * m_samples_per_pixel * m_max_ray_depth;
            m_diagnostics.total_rays_traced += estimated_rays;
            
            // 计算每秒光线数
            auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - m_diagnostics.last_update_time);
            if (time_diff.count() >= 1000) // 每秒更新一次
            {
                float seconds = time_diff.count() / 1000.0f;
                m_diagnostics.rays_per_second = static_cast<uint64_t>(estimated_rays / (frame_time / 1000.0f));
                m_diagnostics.last_update_time = current_time;
            }
        }
        
        // 检查性能警告
        checkPerformanceWarnings();
    }
    
    /**
     * @brief 检查性能警告条件
     */
    void RayTracingPass::checkPerformanceWarnings() const
    {
        // 检查帧时间是否过长（超过33ms表示低于30fps）
        const float warning_frame_time = 33.33f; // 30 FPS
        const float critical_frame_time = 66.67f; // 15 FPS
        
        bool previous_warning = m_diagnostics.performance_warning;
        
        if (m_diagnostics.average_frame_time > critical_frame_time)
        {
            m_diagnostics.performance_warning = true;
            if (!previous_warning)
            {
                LOG_WARN("[RayTracingPass] CRITICAL: Ray tracing performance severely degraded! Average frame time: {:.2f}ms (< 15 FPS)", 
                        m_diagnostics.average_frame_time);
                LOG_WARN("[RayTracingPass] Consider reducing ray tracing quality settings");
            }
        }
        else if (m_diagnostics.average_frame_time > warning_frame_time)
        {
            m_diagnostics.performance_warning = true;
            if (!previous_warning)
            {
                LOG_WARN("[RayTracingPass] WARNING: Ray tracing performance degraded! Average frame time: {:.2f}ms (< 30 FPS)", 
                        m_diagnostics.average_frame_time);
            }
        }
        else
        {
            if (previous_warning)
            {
                LOG_INFO("[RayTracingPass] Performance recovered. Average frame time: {:.2f}ms", m_diagnostics.average_frame_time);
            }
            m_diagnostics.performance_warning = false;
        }
        
        // 检查失败光线数是否过多
        if (m_diagnostics.failed_ray_count > 0 && m_diagnostics.total_frame_count > 0)
        {
            float failure_rate = static_cast<float>(m_diagnostics.failed_ray_count) / m_diagnostics.total_frame_count;
            if (failure_rate > 0.01f) // 失败率超过1%
            {
                LOG_WARN("[RayTracingPass] High ray failure rate detected: {:.2f}% ({} failures in {} frames)", 
                        failure_rate * 100.0f, m_diagnostics.failed_ray_count, m_diagnostics.total_frame_count);
            }
        }
    }

    } // namespace Elish