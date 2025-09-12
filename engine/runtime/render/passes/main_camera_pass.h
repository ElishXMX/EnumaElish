#pragma once
#include <glm/glm.hpp>

#include "../render_pass.h"
#include "../render_resource.h"
#include "../render_camera.h"

// Vector3类型别名定义
using Vector3 = glm::vec3;

namespace Elish
{
    class DirectionalLightShadowPass; // 前向声明
}


namespace Elish
{
    class MainCameraPass : public RenderPass
    {
    public:
        // 核心渲染接口
        void initialize() override;
        void draw() override;
        void drawForward(uint32_t swapchain_image_index);
        void preparePassData(std::shared_ptr<RenderResource> render_resource) override;
        
        // 渲染管线设置
        void setupAttachments();
        void setupRenderPass();
        void setupDescriptorSetLayout();
        void setupPipelines();
        void setupBackgroundDescriptorSet();
        void setupSkyboxDescriptorSet();  // 新增：天空盒描述符集设置
        void setupModelDescriptorSet();
        void setupFramebufferDescriptorSet();
        void setupSwapchainFramebuffers();

        void updateAfterFramebufferRecreate();
        
        // 设置阴影渲染pass引用
        void setDirectionalLightShadowPass(std::shared_ptr<DirectionalLightShadowPass> shadow_pass) { m_directional_light_shadow_pass = shadow_pass; }
        
        // 光源管理系统接口（已移除，现在使用 RenderResource 管理光源）
        
        /**
         * @brief 设置背景绘制启用状态
         * @param enabled 是否启用背景绘制
         */
        void setBackgroundEnabled(bool enabled);
        
        /**
         * @brief 获取背景绘制启用状态
         * @return 背景绘制是否启用
         */
        bool isBackgroundEnabled() const;
        
        /**
         * @brief 设置天空盒绘制启用状态
         * @param enabled 是否启用天空盒绘制
         */
        void setSkyboxEnabled(bool enabled);
        
        /**
         * @brief 获取天空盒绘制启用状态
         * @return 天空盒绘制是否启用
         */
        bool isSkyboxEnabled() const;
        
        // 旧的兼容性接口已移除，现在使用 RenderResource 管理光源

        

    private:
        // 帧缓冲相关
        std::vector<RHIFramebuffer*> m_swapchain_framebuffers;
        
        // 相机系统
        std::shared_ptr<RenderCamera> m_camera;
        
        // 阴影渲染pass引用
        std::shared_ptr<DirectionalLightShadowPass> m_directional_light_shadow_pass;
        
        // 光源管理系统（已移除，现在使用 RenderResource 管理光源）
        
        // 兼容性光源位置（用于回退模式）
        Vector3 m_fallback_light_position = Vector3(0.0f, 0.0f, 3.0f);
        
        // Uniform Buffer VP相关 (View-Projection, model矩阵通过Push Constants传递)
        struct UniformBufferObject {
            glm::mat4 view;
            glm::mat4 proj;
        };
        UniformBufferObject m_uniform_buffer_objects[2];
        std::vector<RHIBuffer*> uniformBuffers;                 // 为每个飞行中的帧创建的统一缓存区
        std::vector<RHIDeviceMemory*> uniformBuffersMemory;     // 每个uniform buffer对应的内存地址
        std::vector<RHIBuffer*> viewUniformBuffers;                 // 为每个飞行中的帧创建的统一缓存区
        std::vector<RHIDeviceMemory*> viewUniformBuffersMemory;     // 每个uniform buffer对应的内存地址
        std::vector<RHIBuffer*> lightSpaceMatrixBuffers;            // 光源投影视图矩阵uniform buffer
        std::vector<RHIDeviceMemory*> lightSpaceMatrixBuffersMemory; // 光源投影视图矩阵buffer内存

        struct Light
        {
            glm::vec4 position;
            glm::vec4 color;
            glm::vec4 direction;
            glm::vec4 info;
        };

        /** 场景灯光信息*/
        struct UniformBufferObjectView {
            Light directional_lights[4];
            Light point_lights[4];
            Light spot_lights[4];
            glm::ivec4 lights_count; // [0] for directional_lights, [1] for point_lights, [2] for spot_lights, [3] for cubemap max miplevels
            glm::vec4 camera_position;
        };
        
      
        // 直接绘制方式不需要顶点和索引缓冲区
        // RHIBuffer* vertexBuffer = nullptr;                      // 顶点缓存区
        // RHIBuffer* indexBuffer = nullptr;                       // 顶点点序缓存区
        // RHIDeviceMemory* vertexBufferMemory = nullptr;          // 顶点缓存区分配的内存
        // RHIDeviceMemory* indexBufferMemory = nullptr;           // 顶点点序缓存区分配的内存
        
        // 纹理资源
        RHIImage* textureImage = nullptr;                       // 纹理资源
        RHIDeviceMemory* textureImageMemory = nullptr;          // 纹理资源内存
        RHIImageView* textureImageView = nullptr;               // 纹理资源对应的视口
        RHISampler* textureSampler = nullptr;                   // 纹理采样器
        
        // 背景纹理数据
        RHIImage* m_background_texture = nullptr;
        RHIImageView* m_background_texture_view = nullptr;
        RHIDeviceMemory* m_background_texture_memory = nullptr;
        RHISampler* m_background_sampler = nullptr;
        
        // 背景图片实际尺寸
        int m_background_image_width = 0;
        int m_background_image_height = 0;
        
        // 天空盒渲染相关资源
        bool m_enable_background = false;  // 背景绘制开关（用于UI控制）
        bool m_enable_skybox = true;  // 天空盒渲染开关（用于UI控制）
        bool m_skybox_descriptor_sets_initialized = false;  // 天空盒描述符集初始化标志
        std::vector<RHIDescriptorSet*> m_skybox_descriptor_sets;  // 天空盒渲染描述符集
        RHIDescriptorSetLayout* m_skybox_descriptor_layout = nullptr;  // 天空盒专用描述符布局
        
        uint32_t layout_size = 10;//定义模型的描述符集布局大小，即需要几个绑定点  五张纹理，一张cubmap，三个缓冲区，一张阴影贴图
        // Render resource
        std::shared_ptr<RenderResource> m_render_resource = nullptr;
        
        // 模型渲染的描述符集信息（独立于背景渲染）
        std::vector<Descriptor> m_model_descriptor_infos;
        
        // 新增：存储加载的模型对象，避免重复获取
        std::vector<RenderObject> m_loaded_render_objects;
        
        // 描述符集状态标志
        bool m_model_descriptor_sets_initialized = false;
        
        // 私有方法
        void setupBackgroundTexture();

        void createTextureImage();		// 创建贴图资源
        void createTextureImageView();	// 创建着色器中引用的贴图View
        void createTextureSampler();		// 创建着色器中引用的贴图采样器
        // 直接绘制方式不需要顶点和索引缓冲区
        // void createVertexBuffer();		// 创建VertexBuffer顶点缓存区
		// void createIndexBuffer();		// 创建IndexBuffer顶点点序缓存区
		void createUniformBuffers();		// 创建UnifromBuffer统一缓存区
        
        // 私有方法 - 绘制相关
        void drawBackground(RHICommandBuffer* command_buffer);
        void drawSkybox(RHICommandBuffer* command_buffer);  // 新增：天空盒绘制方法
        void drawModels(RHICommandBuffer* command_buffer);
        void drawUI(RHICommandBuffer* command_buffer);
        void updateUniformBuffer(uint32_t currentFrameIndex);
         
        // 静态方法 - 顶点输入描述
        static std::vector<RHIVertexInputBindingDescription> getVertexBindingDescriptions();
        static std::vector<RHIVertexInputAttributeDescription> getVertexAttributeDescriptions();
    };
} // namespace Elish
