#pragma once

#include "../core/base/macro.h"
#include "interface/vulkan/vulkan_rhi_resource.h"
#include "../../3rdparty/json11/json11.hpp"
#include <vector>
#include <memory>
#include <string>
#include <array>
#include <unordered_map>
#include <glm/glm.hpp>

    

// 将哈希函数定义移到Elish命名空间内，避免扩展std命名空间
namespace Elish {
    class RHI;
    class RenderCamera;
    
    struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	glm::vec3 normal;

	static RHIVertexInputBindingDescription getBindingDescription() {
		RHIVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = RHI_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescription;
	}

	static std::vector<RHIVertexInputAttributeDescription> getAttributeDescriptions()
        {
            std::vector<RHIVertexInputAttributeDescription> attributeDescriptions{};
            attributeDescriptions.resize(4);

            // PBR shader expects: position(0), normal(1), color(2), texCoord(3)
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = RHI_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = RHI_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, normal);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = RHI_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, color);

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = RHI_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }

	bool operator==(const Vertex& other) const {
		return pos == other.pos && color == other.color && texCoord == other.texCoord && normal == other.normal;
	}
};
	// 为glm::vec2定义哈希函数
	struct Vec2Hash {
		size_t operator()(glm::vec2 const& v) const {
			return std::hash<float>()(v.x) ^ (std::hash<float>()(v.y) << 1);
		}
	};
	
	// 为glm::vec3定义哈希函数
	struct Vec3Hash {
		size_t operator()(glm::vec3 const& v) const {
			return std::hash<float>()(v.x) ^ (std::hash<float>()(v.y) << 1) ^ (std::hash<float>()(v.z) << 2);
		}
	};
	
	// 为Vertex定义哈希函数
	struct VertexHash {
		size_t operator()(Vertex const& vertex) const {
			return Vec3Hash()(vertex.pos) ^ (Vec3Hash()(vertex.color) << 1) ^ (Vec2Hash()(vertex.texCoord) << 2);
		}
	};

    struct StorageBuffer//管理 GPU 存储缓冲区
    {
        // limits
        uint32_t _min_uniform_buffer_offset_alignment{ 256 };
        uint32_t _min_storage_buffer_offset_alignment{ 256 };
        uint32_t _max_storage_buffer_range{ 1 << 27 };
        uint32_t _non_coherent_atom_size{ 256 };

        RHIBuffer* _global_upload_ringbuffer;
        RHIDeviceMemory* _global_upload_ringbuffer_memory;
        void* _global_upload_ringbuffer_memory_pointer;
        std::vector<uint32_t> _global_upload_ringbuffers_begin;
        std::vector<uint32_t> _global_upload_ringbuffers_end;
        std::vector<uint32_t> _global_upload_ringbuffers_size;

        RHIBuffer* _global_null_descriptor_storage_buffer;
        RHIDeviceMemory* _global_null_descriptor_storage_buffer_memory;

        // axis
        RHIBuffer* _axis_inefficient_storage_buffer;
        RHIDeviceMemory* _axis_inefficient_storage_buffer_memory;
        void* _axis_inefficient_storage_buffer_memory_pointer;
    };

    //（基于图像的光照）
    struct IBLResource//IBL资源对象（GPU端）
    {
        RHIImage* _brdfLUT_texture_image;
        RHIImageView* _brdfLUT_texture_image_view;
        RHISampler* _brdfLUT_texture_sampler;
        VmaAllocation _brdfLUT_texture_image_allocation;
    };

    struct IBLResourceData//IBL资源数据（CPU端原始数据）
    {
       
        std::array<void*, 6> _irradiance_texture_image_pixels;
        uint32_t             _irradiance_texture_image_width;
        uint32_t             _irradiance_texture_image_height;
        RHIFormat   _irradiance_texture_image_format;
        
    };


    struct GlobalRenderResource
    {
        StorageBuffer        _storage_buffer;
    };    


    /** 模型动画参数结构 */
    struct ModelAnimationParams {
        glm::vec3 position = glm::vec3(0.0f);       // 位置偏移
        glm::vec3 rotation = glm::vec3(0.0f);       // 旋转角度 (弧度)
        glm::vec3 scale = glm::vec3(1.0f);          // 缩放比例
        glm::vec3 rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);  // 旋转轴
        float rotationSpeed = 1.0f;                 // 旋转速度倍数
        bool enableAnimation = false;               // 是否启用动画（默认禁用，保持静止）
        bool isPlatform = false;                    // 是否为平台（保持静止）
        
        /**
         * @brief 将动画参数序列化为JSON对象
         * @return JSON对象
         */
        json11::Json toJson() const;
        
        /**
         * @brief 从JSON对象反序列化动画参数
         * @param json JSON对象
         * @return 成功返回true，失败返回false
         */
        bool fromJson(const json11::Json& json);
    };
    


    /** 渲染一个模型需要的所有RHI资源*/
	struct RenderObject {
		std::string name;					// 模型名称
		std::string modelName;				// 模型标识名称，用于识别和管理
		std::vector<Vertex> vertices;		// 顶点
		std::vector<uint32_t> indices;		// 点序
		RHIBuffer* vertexBuffer;			// 顶点缓存
		RHIDeviceMemory* vertexBufferMemory;// 顶点缓存内存
		RHIBuffer* indexBuffer;				// 点序缓存
		RHIDeviceMemory* indexBufferMemory;	// 点序缓存内存

		std::vector<RHIImage*> textureImages;				// 贴图
		std::vector<RHIDeviceMemory*> textureImageMemorys;	// 贴图内存
		std::vector<RHIImageView*> textureImageViews;		// 贴图视口
		std::vector<RHISampler*> textureSamplers;			// 贴图采样器

		RHIDescriptorPool* descriptorPool;				// 描述符池
		std::vector<RHIDescriptorSet*> descriptorSets;		// 描述符集合
		RHIDescriptorSet* textureDescriptorSet;			// 纹理描述符集合
        
        // 新增：每个模型的独立动画参数
        ModelAnimationParams animationParams;
	};


    /** 构建一个渲染管线需要的RHI资源*/
	struct RenderPipelineResource {
		RHIDescriptorSetLayout* descriptorSetLayout;  // 描述符集合布局
		RHIPipelineLayout* pipelineLayout;            // 渲染管线布局
		RHIPipeline* graphicsPipeline;                // 渲染管线
	};

    /**
     * @brief 光线追踪管线资源结构
     * @details 包含光线追踪所需的所有资源，包括描述符集布局、管线布局和光线追踪管线
     */
    struct RayTracingPipelineResource {
        RHIDescriptorSetLayout* descriptorSetLayout;  // 光线追踪描述符集布局
        RHIPipelineLayout* pipelineLayout;            // 光线追踪管线布局
        RHIPipeline* rayTracingPipeline;              // 光线追踪管线
        RHIBuffer* shaderBindingTable;                // 着色器绑定表
        VmaAllocation shaderBindingTableAllocation;   // 着色器绑定表内存分配
    };

    /**
     * @brief 光线追踪资源结构
     * @details 包含光线追踪所需的加速结构和相关资源
     */
    struct RayTracingResource {
        RHIAccelerationStructure* topLevelAS;         // 顶层加速结构（TLAS）
        RHIBuffer* topLevelASBuffer;                  // TLAS缓冲区
        VmaAllocation topLevelASAllocation;           // TLAS内存分配
        
        std::vector<RHIAccelerationStructure*> bottomLevelAS;  // 底层加速结构数组（BLAS）
        std::vector<RHIBuffer*> bottomLevelASBuffers;          // BLAS缓冲区数组
        std::vector<VmaAllocation> bottomLevelASAllocations;   // BLAS内存分配数组
        
        RHIBuffer* instanceBuffer;                    // 实例缓冲区
        VmaAllocation instanceAllocation;             // 实例缓冲区内存分配
        
        RHIImage* rayTracingOutputImage;             // 光线追踪输出图像
        RHIImageView* rayTracingOutputImageView;     // 光线追踪输出图像视图
        VmaAllocation rayTracingOutputImageAllocation; // 输出图像内存分配
        
        // 兼容性成员变量（用于raytracing_pass.cpp）
        RHIAccelerationStructure* tlas;              // TLAS别名，指向topLevelAS
        RHIBuffer* vertex_buffer;                     // 顶点缓冲区
        RHIBuffer* index_buffer;                      // 索引缓冲区
    };
    /** 全局常量*/
    struct GlobalConstants {
        float time;
        float roughness;
        float metallic;
    };

    /**
     * @brief GPU光源数据结构
     * 用于传递给着色器的光源数据格式
     */
    struct LightGPUData {
        glm::vec4 position;    // 位置（w=0表示方向光，w=1表示点光源）
        glm::vec4 color;       // 颜色和强度（rgb=颜色，a=强度）
        glm::vec4 direction;   // 方向（归一化）
        glm::vec4 info;        // 附加信息（x=是否投射阴影，y=阴影偏移，z,w=保留）
    };

    /**
     * @brief 方向光源数据结构
     * @details 存储方向光源的所有参数，用于统一管理和GPU数据传输
     */
    struct DirectionalLightData {
        glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);  // 光源方向（归一化）
        float intensity = 1.0f;                              // 光源强度
        glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);      // 光源颜色
        float distance = 10.0f;                              // 光源距离（用于阴影贴图投影）
        bool cast_shadows = true;                             // 是否投射阴影
        float shadow_bias = 0.005f;                          // 阴影偏移
        bool enabled = true;                                  // 是否启用
        
        /**
         * @brief 转换为GPU数据格式
         * @return LightGPUData结构
         */
        LightGPUData toGPUData() const {
            LightGPUData gpu_data;
            gpu_data.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // w=0表示方向光
            gpu_data.color = glm::vec4(color * intensity, 1.0f);
            gpu_data.direction = glm::vec4(glm::normalize(direction), 0.0f);
            gpu_data.info = glm::vec4(cast_shadows ? 1.0f : 0.0f, shadow_bias, 0.0f, 0.0f);
            return gpu_data;
        }
    };
    /**
     * @brief 渲染资源管理类，负责管理模型数据和相关的渲染资源
     * 这个类封装了模型加载、缓冲区管理、纹理管理等功能
     */
    class RenderResource 
    {
    public:
        RenderResource();
        ~RenderResource() ;

        GlobalConstants global;
        
        /**
         * @brief 初始化渲染资源
         * @param rhi RHI接口指针
         */
        void initialize(std::shared_ptr<RHI> rhi) ;
        /**
         * @brief 获取已加载的模型列表
         * @return 模型列表的常量引用
         */
        const std::vector<RenderObject>& getLoadedRenderObjects() const { return m_RenderObjects; }
         /**
         * @brief 获取模型数量
         * @return 已加载的模型数量
         */
        size_t getRenderObjectCount() const { return m_RenderObjects.size(); }
        /**
         * @brief 检查是否有已加载的模型
         * @return 如果有模型返回true，否则返回false
         */
        bool hasRenderObjects() const { return !m_RenderObjects.empty(); }
         /**
         * @brief 清理所有资源
         */
        void cleanup();
        /**
         * @brief 添加一个渲染对象到资源管理器中
         * @param renderObject 要添加的渲染对象
         */
        void addRenderObject(const RenderObject& renderObject);
        
        /**
         * @brief 清空所有渲染对象
         */
        void clearAllRenderObjects();
        
        /**
         * @brief 更新指定渲染对象的动画参数
         * @param objectIndex 渲染对象的索引
         * @param newParams 新的动画参数
         * @return 更新是否成功
         */
        bool updateRenderObjectAnimationParams(size_t objectIndex, const ModelAnimationParams& newParams);
        // 创建一个渲染对象，包括对应的顶点和纹理
        bool createRenderObjectResource(RenderObject& outRenderObject, const std::string& objfile, const std::vector<std::string>& pngfiles);
        
        /**
         * @brief 获取模型渲染管线资源
         * @return 模型渲染管线资源的常量引用
         */
        const RenderPipelineResource& getModelPipelineResource() const {
            return m_modelPipelineResource;
        }
        
        /**
         * @brief 创建模型渲染管线资源
         * @param renderPass 渲染通道
         * @return 创建是否成功
         */
        bool createModelPipelineResource(RHIRenderPass* renderPass);
        
        /**
         * @brief 检查模型渲染管线资源是否已创建
         * @return 如果已创建返回true，否则返回false
         */
        bool isModelPipelineResourceCreated() const {
            return m_modelPipelineResourceCreated;
        }

        
        /**
         * @brief Loads a cubemap texture from specified file paths.
         * @param cubemapFiles An array of 6 file paths for the cubemap faces (e.g., +X, -X, +Y, -Y, +Z, -Z).
         * @return True if the cubemap is loaded successfully, false otherwise.
         */
        bool loadCubemapTexture(const std::array<std::string, 6>& cubemapFiles);
        /**
         * @brief Gets the cubemap image.
         * @return Pointer to the RHIImage object for the cubemap.
         */
        RHIImage* getCubemapImage()  { return m_cubemapImage; }
        /**
         * @brief Gets the cubemap image view.
         * @return Pointer to the RHIImageView object for the cubemap.
         */
        RHIImageView* getCubemapImageView()  { return m_cubemapImageView; }
        /**
         * @brief Gets the cubemap image sampler.
         * @return Pointer to the RHISampler object for the cubemap.
         */
        RHISampler* getCubemapImageSampler()  { return cubemapSampler; }

        /**
         * @brief Gets the directional light shadow map image view.
         * @return Pointer to the RHIImageView object for the shadow map.
         */
        RHIImageView* getDirectionalLightShadowImageView() { return m_directionalLightShadowImageView; }
        
        /**
         * @brief Gets the directional light shadow map image sampler.
         * @return Pointer to the RHISampler object for the shadow map.
         */
        RHISampler* getDirectionalLightShadowImageSampler() { return m_directionalLightShadowSampler; }
        
        /**
         * @brief Sets the directional light shadow map resources.
         * @param shadowImageView The shadow map image view from shadow pass.
         * @param shadowSampler The shadow map sampler from shadow pass.
         */
        void setDirectionalLightShadowResources(RHIImageView* shadowImageView, RHISampler* shadowSampler) {
            m_directionalLightShadowImageView = shadowImageView;
            m_directionalLightShadowSampler = shadowSampler;
        }
        
        // ========================================================================
        // 方向光源管理接口
        // ========================================================================
        
        /**
         * @brief 添加方向光源
         * @param lightData 光源数据
         * @return 光源索引
         */
        size_t addDirectionalLight(const DirectionalLightData& lightData);
        
        /**
         * @brief 更新方向光源
         * @param index 光源索引
         * @param lightData 新的光源数据
         * @return 成功返回true
         */
        bool updateDirectionalLight(size_t index, const DirectionalLightData& lightData);
        
        /**
         * @brief 移除方向光源
         * @param index 光源索引
         * @return 成功返回true
         */
        bool removeDirectionalLight(size_t index);
        
        /**
         * @brief 获取方向光源数据
         * @param index 光源索引
         * @return 光源数据指针，无效索引返回nullptr
         */
        const DirectionalLightData* getDirectionalLight(size_t index) const;
        
        /**
         * @brief 获取所有方向光源数据
         * @return 光源数据数组
         */
        const std::vector<DirectionalLightData>& getDirectionalLights() const { return m_directionalLights; }
        
        /**
         * @brief 获取方向光源数量
         * @return 光源数量
         */
        size_t getDirectionalLightCount() const { return m_directionalLights.size(); }
        
        /**
         * @brief 清空所有方向光源
         */
        void clearDirectionalLights();
        
        /**
         * @brief 获取主要方向光源（第一个启用的光源）
         * @return 光源数据指针，无光源返回nullptr
         */
        const DirectionalLightData* getPrimaryDirectionalLight() const;
        
        /**
         * @brief 更新主要方向光源的方向
         * @param direction 新的光源方向
         * @return 更新是否成功
         */
        bool updateDirectionalLightDirection(const glm::vec3& direction);
        
        /**
         * @brief 更新主要方向光源的强度
         * @param intensity 新的光源强度
         * @return 更新是否成功
         */
        bool updateDirectionalLightIntensity(float intensity);
        
        /**
         * @brief 更新方向光源颜色
         * @param color 新的光源颜色
         * @return 更新是否成功
         */
        bool updateDirectionalLightColor(const glm::vec3& color);
        
        /**
         * @brief 更新方向光距离
         * @param distance 新的距离值
         * @return 更新是否成功
         */
        bool updateDirectionalLightDistance(float distance);
        
        // 光线追踪相关方法
        /**
         * @brief 创建光线追踪管线资源
         * @return 创建是否成功
         */
        bool createRayTracingPipelineResource();
        
        /**
         * @brief 创建光线追踪资源（加速结构等）
         * @return 创建是否成功
         */
        bool createRayTracingResource();
        
        /**
         * @brief 获取光线追踪管线资源
         * @return 光线追踪管线资源的常量引用
         */
        const RayTracingPipelineResource& getRayTracingPipelineResource() const {
            return m_rayTracingPipelineResource;
        }
        
        /**
         * @brief 获取光线追踪资源
         * @return 光线追踪资源的常量引用
         */
        const RayTracingResource& getRayTracingResource() const {
            return m_rayTracingResource;
        }
        
        /**
         * @brief 检查光线追踪管线资源是否已创建
         * @return 如果已创建返回true，否则返回false
         */
        bool isRayTracingPipelineResourceCreated() const {
            return m_rayTracingPipelineResourceCreated;
        }
        
        /**
         * @brief 检查光线追踪资源是否已创建
         * @return 如果已创建返回true，否则返回false
         */
        bool isRayTracingResourceCreated() const {
            return m_rayTracingResourceCreated;
        }
        
        /**
         * @brief 更新光线追踪加速结构
         * @return 成功返回true，失败返回false
         */
        bool updateRayTracingAccelerationStructures();
        
        /**
         * @brief 获取相机对象
         * @return 相机对象指针，如果未设置则返回nullptr
         */
        class RenderCamera* getCamera() const { return m_camera; }
        
        /**
         * @brief 设置相机对象
         * @param camera 相机对象指针
         */
        void setCamera(class RenderCamera* camera) { m_camera = camera; }
        
        
    private:
        std::shared_ptr<RHI> m_rhi;
        
        std::vector<RenderObject> m_RenderObjects;               ///< 存储加载的模型
        RenderPipelineResource m_modelPipelineResource;          ///< 模型渲染管线资源
        bool m_modelPipelineResourceCreated = false;             ///< 模型渲染管线资源是否已创建
        
        class RenderCamera* m_camera = nullptr;                 ///< 相机对象指针

        RHIImage* m_cubemapImage;
        RHIImageView* m_cubemapImageView;
        RHISampler* cubemapSampler;		
        VmaAllocation m_cubemapImageAllocation;
        
        // 阴影相关资源
        RHIImageView* m_directionalLightShadowImageView = nullptr;
        RHISampler* m_directionalLightShadowSampler = nullptr;
        
        // 方向光源数据
        std::vector<DirectionalLightData> m_directionalLights;  ///< 方向光源数组

        // 光线追踪资源管理
        RayTracingPipelineResource m_rayTracingPipelineResource;    ///< 光线追踪管线资源
        RayTracingResource m_rayTracingResource;                    ///< 光线追踪资源（加速结构等）
        bool m_rayTracingPipelineResourceCreated = false;          ///< 光线追踪管线资源是否已创建
        bool m_rayTracingResourceCreated = false;                   ///< 光线追踪资源是否已创建
        
        /**
         * @brief 加载OBJ模型文件
         * @param objPath OBJ文件路径
         * @return 是否加载成功
         */
        bool loadOBJ(const std::string& objPath); 
        /**
         * @brief 解析OBJ文件
         * @param objPath OBJ文件路径
         * @param RenderObject 输出的模型数据
         * @return 是否解析成功
         */
        bool parseOBJFile(const std::string& objPath, RenderObject& RenderObject);
        
        /**
         * @brief 为模型创建Vulkan缓冲区
         * @param RenderObject 模型数据
         * @return 是否创建成功
         */
        bool createRenderObjectBuffers(RenderObject& RenderObject);
        /**
         * @brief 从文件创建纹理资源
         * @param renderObject 渲染对象
         * @param textureFiles 纹理文件路径数组
         * @return 是否创建成功
         */
        bool createTexturesFromFiles(RenderObject& renderObject, const std::vector<std::string>& textureFiles);
        /**
         * @brief 创建单个默认纹理
         * @param renderObject 渲染对象
         * @param index 纹理索引
         * @return 是否创建成功
         */
        bool createSingleDefaultTexture(RenderObject& renderObject, size_t index);

        
    };
}
 // namespace Elish