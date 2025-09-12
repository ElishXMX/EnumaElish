#pragma once

#include "../render_pass.h"
#include "../render_resource.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Elish
{
    using Matrix4 = glm::mat4;
    using Vector3 = glm::vec3;

    /**
     * @brief 方向光阴影渲染通道类
     * 
     * 该类负责生成方向光的阴影贴图，为主渲染通道提供阴影信息。
     * 阴影贴图通过从光源视角渲染场景的深度信息来生成。
     */
    class DirectionalLightShadowPass : public RenderPass
    {
    public:
        /**
         * @brief 初始化阴影渲染通道
         * @param init_info 渲染通道初始化信息
         */
        void initialize() override;
        
        /**
         * @brief 后初始化，设置渲染管线和资源
         */
        void postInitialize();
        
        /**
         * @brief 准备每帧的渲染数据
         * @param render_resource 渲染资源
         */
        void preparePassData(std::shared_ptr<RenderResource> render_resource) override;
        
        /**
         * @brief 执行阴影渲染
         */
        void draw() override final;

        /**
         * @brief 交换链重建后更新帧缓冲区
         */
        void updateAfterFramebufferRecreate();

        /**
         * @brief 设置每个网格的描述符集布局
         * @param layout 描述符集布局
         */
        void setPerMeshLayout(RHIDescriptorSetLayout* layout) { m_per_mesh_layout = layout; }

        /**
         * @brief 获取阴影贴图纹理
         * @return 阴影贴图纹理
         */
        RHIImage* getShadowMap() const { return m_shadow_map_image; }
        
        /**
         * @brief 获取阴影贴图图像视图
         * @return 阴影贴图图像视图
         */
        RHIImageView* getShadowMapView() const { return m_shadow_map_image_view; }
        
        /**
         * @brief 获取阴影贴图采样器
         * @return 阴影贴图采样器
         */
        RHISampler* getShadowMapSampler() const { return m_shadow_map_sampler; }

        /**
         * @brief 获取光源投影视图矩阵
         * @return 光源投影视图矩阵
         */
        const Matrix4& getLightProjectionViewMatrix() const { return m_light_proj_view_matrix; }
        
        // 旧的光源管理系统接口已移除，现在使用 RenderResource 管理光源
        
        /**
         * @brief 更新光源矩阵（公有接口）
         * @param render_resource 渲染资源管理器
         */
        void updateLightMatrix(std::shared_ptr<RenderResource> render_resource);

    private:
        /**
         * @brief 设置渲染附件
         */
        void setupAttachments();
        
        /**
         * @brief 设置渲染通道
         */
        void setupRenderPass();
        
        /**
         * @brief 设置帧缓冲
         */
        void setupFramebuffer();
        
        /**
         * @brief 设置描述符集布局
         */
        void setupDescriptorSetLayout();
        
        /**
         * @brief 设置渲染管线
         */
        void setupPipelines();
        
        /**
         * @brief 设置描述符集
         */
        void setupDescriptorSet();
        
        /**
         * @brief 渲染模型到阴影贴图
         */
        void drawModel();
        

        
        /**
         * @brief 更新uniform buffer
         */
        void updateUniformBuffer();
        
        /**
         * @brief 创建uniform buffers
         */
        void createUniformBuffers();
        
        /**
         * @brief 渲染测试四边形用于调试深度写入
         */
        void drawTestQuad();
        
        /**
         * @brief 初始化测试四边形几何数据
         */
        void initializeTestQuad();

    private:
        // 描述符集布局
        RHIDescriptorSetLayout* m_per_mesh_layout;
        
        // 阴影贴图相关资源
        RHIImage*       m_shadow_map_image;
        RHIImageView*   m_shadow_map_image_view;
        RHIDeviceMemory* m_shadow_map_image_memory;
        RHISampler*     m_shadow_map_sampler;
        
        // 阴影贴图尺寸
        static const uint32_t SHADOW_MAP_SIZE = 2048;
        
        // 光源矩阵
        glm::mat4 m_light_proj_view_matrix;
        
        // 统一缓冲区对象
        struct ShadowUniformBufferObject
        {
            glm::mat4 view;  // 光源视图矩阵：世界空间 -> 光源视图空间
            glm::mat4 proj;  // 光源投影矩阵：光源视图空间 -> 光源裁剪空间
        };
        
        // 渲染管线
        RHIPipeline* m_render_pipeline;
        RHIPipelineLayout* m_pipeline_layout;
        
        // Uniform Buffer相关资源
        RHIBuffer* m_global_uniform_buffer;
        RHIDeviceMemory* m_global_uniform_buffer_memory;
        RHIBuffer* m_instance_buffer;
        RHIDeviceMemory* m_instance_buffer_memory;
        
        // 描述符相关资源
        RHIDescriptorPool* m_descriptor_pool;
        RHIDescriptorSet* m_descriptor_sets[3];  // 🔧 修复：为每个飞行帧创建独立描述符集
        RHIDescriptorSetLayout* m_descriptor_set_layout;
        
        // 渲染通道和帧缓冲
        RHIRenderPass* m_render_pass;
        RHIFramebuffer* m_framebuffer;
        
        // 测试四边形相关资源
        RHIBuffer* m_test_quad_vertex_buffer;
        RHIDeviceMemory* m_test_quad_vertex_buffer_memory;
        RHIBuffer* m_test_quad_index_buffer;
        RHIDeviceMemory* m_test_quad_index_buffer_memory;
        bool m_test_quad_initialized;
        
        // 当前渲染资源
        std::shared_ptr<RenderResource> m_current_render_resource;
    };
} // namespace Elish
