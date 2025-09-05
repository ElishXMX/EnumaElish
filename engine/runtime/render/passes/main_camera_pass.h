#pragma once
#include <glm/glm.hpp>

#include "../render_pass.h"
#include "../render_resource.h"
#include "../render_camera.h"


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
        void setupModelDescriptorSet();
        void setupFramebufferDescriptorSet();
        void setupSwapchainFramebuffers();

        void updateAfterFramebufferRecreate();

        

    private:
        // 帧缓冲相关
        std::vector<RHIFramebuffer*> m_swapchain_framebuffers;
        
        // 相机系统
        std::shared_ptr<RenderCamera> m_camera;
        
        // Uniform Buffer MVP相关
        struct UniformBufferObject {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        };
        UniformBufferObject m_uniform_buffer_objects[2];
        std::vector<RHIBuffer*> uniformBuffers;                 // 为每个飞行中的帧创建的统一缓存区
        std::vector<RHIDeviceMemory*> uniformBuffersMemory;     // 每个uniform buffer对应的内存地址
        std::vector<RHIBuffer*> viewUniformBuffers;                 // 为每个飞行中的帧创建的统一缓存区
        std::vector<RHIDeviceMemory*> viewUniformBuffersMemory;     // 每个uniform buffer对应的内存地址

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
        
        uint32_t layout_size = 8;//定义模型的描述符集布局大小，即需要几个绑定点  五张纹理，一张cubmap，两个缓冲区
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
        void drawModels(RHICommandBuffer* command_buffer);
        void updateUniformBuffer(uint32_t currentFrameIndex);
         
        // 静态方法 - 顶点输入描述
        static std::vector<RHIVertexInputBindingDescription> getVertexBindingDescriptions();
        static std::vector<RHIVertexInputAttributeDescription> getVertexAttributeDescriptions();
    };
} // namespace Elish
