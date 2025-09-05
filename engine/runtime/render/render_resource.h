#pragma once

#include "render_system.h"
#include "../core/base/macro.h"
#include "interface/vulkan/vulkan_rhi_resource.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

    

// 将哈希函数定义移到Elish命名空间内，避免扩展std命名空间
namespace Elish {
    class RHI;
    
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


    /** 渲染一个模型需要的所有RHI资源*/
	struct RenderObject {
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
	};


    /** 构建一个渲染管线需要的RHI资源*/
	struct RenderPipelineResource {
		RHIDescriptorSetLayout* descriptorSetLayout;  // 描述符集合布局
		RHIPipelineLayout* pipelineLayout;            // 渲染管线布局
		RHIPipeline* graphicsPipeline;                // 渲染管线
	};
    /** 全局常量*/
    struct GlobalConstants {
        float time;
        float roughness;
        float metallic;
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

        
        
    private:
        std::shared_ptr<RHI> m_rhi;
        
        std::vector<RenderObject> m_RenderObjects;               ///< 存储加载的模型
        RenderPipelineResource m_modelPipelineResource;          ///< 模型渲染管线资源
        bool m_modelPipelineResourceCreated = false;             ///< 模型渲染管线资源是否已创建

        RHIImage* m_cubemapImage;
        RHIImageView* m_cubemapImageView;
        RHISampler* cubemapSampler;		
        VmaAllocation m_cubemapImageAllocation;
        
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