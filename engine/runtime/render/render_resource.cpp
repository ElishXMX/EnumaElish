#include "render_resource.h"
#include "../../3rdparty/tinyobjloader/tiny_obj_loader.h"
#include "../core/base/macro.h"
#include "../../3rdparty/stb/stb_image.h"
#include "../shader/generated/cpp/PBR_vert.h"
#include "../shader/generated/cpp/PBR_frag.h"
#include "../shader/generated/cpp/raytracing_rgen.h"
#include "../shader/generated/cpp/raytracing_rchit.h"
#include "../shader/generated/cpp/raytracing_rmiss.h"
#include "../shader/generated/cpp/shadow_rmiss.h"
#include "interface/vulkan/vulkan_util.h"
#include <unordered_map>

namespace Elish
{
    RenderResource::RenderResource()
        : m_cubemapImage(nullptr)
        , m_cubemapImageView(nullptr)
        , cubemapSampler(nullptr)
    {
        
    }
    
    RenderResource::~RenderResource()
    {
        
        cleanup();
    }
    void RenderResource::initialize(std::shared_ptr<RHI> rhi)
    {
        
        m_rhi = rhi;
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::initialize] RHI pointer is null!");
            return;
        }
    }
    void RenderResource::cleanup()
    {
        
        
        // 清理模型渲染管线资源
        if (m_modelPipelineResourceCreated && m_rhi) {
            if (m_modelPipelineResource.graphicsPipeline != nullptr) {
                // TODO: Add destroyPipeline method to RHI interface
                // m_rhi->destroyPipeline(m_modelPipelineResource.graphicsPipeline);
                delete m_modelPipelineResource.graphicsPipeline;
                m_modelPipelineResource.graphicsPipeline = nullptr;
            }
            if (m_modelPipelineResource.pipelineLayout != nullptr) {
                // TODO: Add destroyPipelineLayout method to RHI interface
                // m_rhi->destroyPipelineLayout(m_modelPipelineResource.pipelineLayout);
                delete m_modelPipelineResource.pipelineLayout;
                m_modelPipelineResource.pipelineLayout = nullptr;
            }
            if (m_modelPipelineResource.descriptorSetLayout != nullptr) {
                // TODO: Add destroyDescriptorSetLayout method to RHI interface
                // m_rhi->destroyDescriptorSetLayout(m_modelPipelineResource.descriptorSetLayout);
                delete m_modelPipelineResource.descriptorSetLayout;
                m_modelPipelineResource.descriptorSetLayout = nullptr;
            }
            m_modelPipelineResourceCreated = false;
        }
        
        // 清理cubemap资源
        if (m_rhi) {
            if (m_cubemapImageView != nullptr) {
                m_rhi->destroyImageView(m_cubemapImageView);
                m_cubemapImageView = nullptr;
            }
            if (m_cubemapImage != nullptr) {
                m_rhi->destroyImage(m_cubemapImage);
                m_cubemapImage = nullptr;
            }
            // Note: cubemapSampler is managed by RHI's default sampler system, no need to destroy manually
            cubemapSampler = nullptr;
        }
        
        // 清理所有模型数据
        m_RenderObjects.clear();
    }
    
    void RenderResource::addRenderObject(const RenderObject& renderObject)
    {
        m_RenderObjects.push_back(renderObject);
    }
    
    /**
     * @brief 清空所有渲染对象
     */
    void RenderResource::clearAllRenderObjects()
    {
        m_RenderObjects.clear();
        LOG_INFO("[RenderResource::clearAllRenderObjects] Cleared all render objects");
    }
    
    /**
     * @brief 更新指定渲染对象的动画参数
     * @param objectIndex 渲染对象的索引
     * @param newParams 新的动画参数
     * @return 更新是否成功
     */
    bool RenderResource::updateRenderObjectAnimationParams(size_t objectIndex, const ModelAnimationParams& newParams)
    {
        if (objectIndex >= m_RenderObjects.size()) {
            LOG_ERROR("[RenderResource::updateRenderObjectAnimationParams] Invalid object index: {}", objectIndex);
            return false;
        }
        
        // 更新动画参数
        m_RenderObjects[objectIndex].animationParams = newParams;
        
        // LOG_INFO("[RenderResource::updateRenderObjectAnimationParams] Updated animation params for object {} ({})", 
                 // objectIndex, m_RenderObjects[objectIndex].name);
        
        return true;
    }
    
    bool RenderResource::createRenderObjectResource(RenderObject& outRenderObject, const std::string& objfile, const std::vector<std::string>& pngfiles)
    {
        
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createRenderObjectResource] RHI pointer is null!");
            return false;
        }
        
        // Parse OBJ file
        if (!parseOBJFile(objfile, outRenderObject)) {
            LOG_ERROR("[RenderResource::createRenderObjectResource] Failed to parse OBJ file: {}", objfile.c_str());
            return false;
        }
        
        // Create vertex and index buffers
        if (!createRenderObjectBuffers(outRenderObject)) {
            LOG_ERROR("[RenderResource::createRenderObjectResource] Failed to create buffers for OBJ file: {}", objfile.c_str());
            return false;
        }
        
        // Create textures from files
        if (!pngfiles.empty()) {
            if (!createTexturesFromFiles(outRenderObject, pngfiles)) {
                LOG_ERROR("[RenderResource::createRenderObjectResource] Failed to create textures from files");
                return false;
            }
        } else {
            // Create default texture if no texture files provided
            outRenderObject.textureImages.resize(1);
            outRenderObject.textureImageMemorys.resize(1);
            outRenderObject.textureImageViews.resize(1);
            outRenderObject.textureSamplers.resize(1);
            
            if (!createSingleDefaultTexture(outRenderObject, 0)) {
                LOG_ERROR("[RenderResource::createRenderObjectResource] Failed to create default texture");
                return false;
            }
        }
        
        
        return true;
    }
    
    bool RenderResource::loadOBJ(const std::string& objPath)
    {
        
        
        RenderObject renderObject;
        if (!parseOBJFile(objPath, renderObject)) {
            LOG_ERROR("[RenderResource::loadOBJ] Failed to parse OBJ file: {}", objPath.c_str());
            return false;
        }
        
        if (!createRenderObjectBuffers(renderObject)) {
            LOG_ERROR("[RenderResource::loadOBJ] Failed to create buffers for OBJ file: {}", objPath.c_str());
            return false;
        }
        
        m_RenderObjects.push_back(renderObject);
        
        return true;
    }
    
    bool RenderResource::parseOBJFile(const std::string& objPath, RenderObject& renderObject)
    {
        
        
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;
        
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str())) {
            LOG_ERROR("[RenderResource::parseOBJFile] Failed to load OBJ file: {} Error: {}", objPath.c_str(), err.c_str());
            return false;
        }
        
        if (!warn.empty()) {
            LOG_WARN("[RenderResource::parseOBJFile] Warning: {}", warn.c_str());
        }
        
        std::unordered_map<Vertex, uint32_t, VertexHash> uniqueVertices{};
        
        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};
                
                // Position
                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };
                
                // Texture coordinates
                if (index.texcoord_index >= 0) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1] // Flip Y coordinate
                    };
                } else {
                    vertex.texCoord = {0.0f, 0.0f};
                }
                
                // Normal
                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                } else {
                    vertex.normal = {0.0f, 0.0f, 1.0f};
                }
                
                // Default color
                vertex.color = {1.0f, 1.0f, 1.0f};
                
                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(renderObject.vertices.size());
                    renderObject.vertices.push_back(vertex);
                }
                
                renderObject.indices.push_back(uniqueVertices[vertex]);
            }
        }
        
        
        return true;
    }
    
    bool RenderResource::createRenderObjectBuffers(RenderObject& renderObject)
    {
        
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createRenderObjectBuffers] RHI pointer is null!");
            return false;
        }
        
        if (renderObject.vertices.empty()) {
            LOG_ERROR("[RenderResource::createRenderObjectBuffers] No vertices to create buffer for");
            return false;
        }
        
        // Create vertex buffer
        RHIDeviceSize vertexBufferSize = sizeof(renderObject.vertices[0]) * renderObject.vertices.size();
        
        // Create staging buffer for vertex data
        RHIBuffer* stagingBuffer = nullptr;
        RHIDeviceMemory* stagingBufferMemory = nullptr;
        
        m_rhi->createBuffer(vertexBufferSize, 
                           RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           stagingBuffer, 
                           stagingBufferMemory);
        
        // Copy vertex data to staging buffer
        void* data;
        m_rhi->mapMemory(stagingBufferMemory, 0, vertexBufferSize, 0, &data);
        memcpy(data, renderObject.vertices.data(), static_cast<size_t>(vertexBufferSize));
        m_rhi->unmapMemory(stagingBufferMemory);
        
        // Create vertex buffer (convert VkBuffer to RHIBuffer*)
        RHIBuffer* rhiVertexBuffer = nullptr;
        RHIDeviceMemory* rhiVertexBufferMemory = nullptr;
        
        m_rhi->createBuffer(vertexBufferSize, 
                           RHI_BUFFER_USAGE_TRANSFER_DST_BIT | RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
                           rhiVertexBuffer, 
                           rhiVertexBufferMemory);
        
        // Store RHI buffer pointers
        renderObject.vertexBuffer = rhiVertexBuffer;
        renderObject.vertexBufferMemory = rhiVertexBufferMemory;
        
        // Copy from staging buffer to vertex buffer
        m_rhi->copyBuffer(stagingBuffer, rhiVertexBuffer, 0, 0, vertexBufferSize);
        
        // Clean up staging buffer
        m_rhi->destroyBuffer(stagingBuffer);
        m_rhi->freeMemory(stagingBufferMemory);
        
        // Create index buffer
        if (!renderObject.indices.empty()) {
            RHIDeviceSize indexBufferSize = sizeof(renderObject.indices[0]) * renderObject.indices.size();
            
            // Create staging buffer for index data
            RHIBuffer* indexStagingBuffer = nullptr;
            RHIDeviceMemory* indexStagingBufferMemory = nullptr;
            
            m_rhi->createBuffer(indexBufferSize, 
                               RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               indexStagingBuffer, 
                               indexStagingBufferMemory);
            
            // Copy index data to staging buffer
            void* indexData;
            m_rhi->mapMemory(indexStagingBufferMemory, 0, indexBufferSize, 0, &indexData);
            memcpy(indexData, renderObject.indices.data(), static_cast<size_t>(indexBufferSize));
            m_rhi->unmapMemory(indexStagingBufferMemory);
            
            // Create index buffer
            RHIBuffer* rhiIndexBuffer = nullptr;
            RHIDeviceMemory* rhiIndexBufferMemory = nullptr;
            
            m_rhi->createBuffer(indexBufferSize, 
                               RHI_BUFFER_USAGE_TRANSFER_DST_BIT | RHI_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
                               rhiIndexBuffer, 
                               rhiIndexBufferMemory);
            
            // Store RHI buffer pointers
            renderObject.indexBuffer = rhiIndexBuffer;
            renderObject.indexBufferMemory = rhiIndexBufferMemory;
            
            // Copy from staging buffer to index buffer
            m_rhi->copyBuffer(indexStagingBuffer, rhiIndexBuffer, 0, 0, indexBufferSize);
            
            // Clean up staging buffer
            m_rhi->destroyBuffer(indexStagingBuffer);
            m_rhi->freeMemory(indexStagingBufferMemory);
        }
        
        
        return true;
    }
    
    bool RenderResource::createTexturesFromFiles(RenderObject& renderObject, const std::vector<std::string>& textureFiles)
    {
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createTexturesFromFiles] RHI pointer is null!");
            return false;
        }
        
        renderObject.textureImages.resize(textureFiles.size());
        renderObject.textureImageMemorys.resize(textureFiles.size());
        renderObject.textureImageViews.resize(textureFiles.size());
        renderObject.textureSamplers.resize(textureFiles.size());
        
        for (size_t i = 0; i < textureFiles.size(); ++i) {
            const std::string& texturePath = textureFiles[i];
            
            int texWidth, texHeight, texChannels;
            stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            
            if (!pixels) {
                LOG_ERROR("[RenderResource::createTexturesFromFiles] Failed to load texture: {}", texturePath.c_str());
                // Create default texture instead
                if (!createSingleDefaultTexture(renderObject, i)) {
                    return false;
                }
                continue;
            }
            
            RHIDeviceSize imageSize = texWidth * texHeight * 4; // 4 bytes per pixel (RGBA)
            
            // Create staging buffer
            RHIBuffer* stagingBuffer = nullptr;
            RHIDeviceMemory* stagingBufferMemory = nullptr;
            
            m_rhi->createBuffer(imageSize, 
                               RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, 
                               stagingBufferMemory);
            
            // Copy pixel data to staging buffer
            void* data;
            m_rhi->mapMemory(stagingBufferMemory, 0, imageSize, 0, &data);
            memcpy(data, pixels, static_cast<size_t>(imageSize));
            m_rhi->unmapMemory(stagingBufferMemory);
            
            stbi_image_free(pixels);
            
            // Create image
            RHIImage* rhiTextureImage = nullptr;
            RHIDeviceMemory* rhiTextureImageMemory = nullptr;
            
            m_rhi->createImage(texWidth, texHeight, 
                              RHI_FORMAT_R8G8B8A8_SRGB, 
                              RHI_IMAGE_TILING_OPTIMAL,
                              RHI_IMAGE_USAGE_TRANSFER_DST_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
                              RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              rhiTextureImage, 
                              rhiTextureImageMemory, 
                              0, // image_create_flags
                              1, // array_layers
                              1); // miplevels
            
            // Store RHI image pointers
            renderObject.textureImages[i] = rhiTextureImage;
            renderObject.textureImageMemorys[i] = rhiTextureImageMemory;
            
            // Create image view
            RHIImageView* rhiTextureImageView = nullptr;
            m_rhi->createImageView(rhiTextureImage, 
                                  RHI_FORMAT_R8G8B8A8_SRGB, 
                                  RHI_IMAGE_ASPECT_COLOR_BIT,
                                  RHI_IMAGE_VIEW_TYPE_2D, 
                                  1, // layout_count
                                  1, // miplevels
                                  rhiTextureImageView);
            
            renderObject.textureImageViews[i] = rhiTextureImageView;
            
            // Create sampler using default sampler
        RHISampler* rhiTextureSampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);
        renderObject.textureSamplers[i] = rhiTextureSampler;
            
            // Transition image layout and copy buffer to image
            // First transition from undefined to transfer destination
            VulkanUtil::transitionImageLayout(m_rhi.get(),
                                             ((VulkanImage*)rhiTextureImage)->getResource(),
                                             VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             1, // layer_count
                                             1, // miplevels
                                             VK_IMAGE_ASPECT_COLOR_BIT);
            
            // Copy buffer data to image
            VulkanUtil::copyBufferToImage(m_rhi.get(),
                                        ((VulkanBuffer*)stagingBuffer)->getResource(),
                                        ((VulkanImage*)rhiTextureImage)->getResource(),
                                        texWidth,
                                        texHeight,
                                        1); // layer_count
            
            // Transition image layout from transfer destination to shader read only
            VulkanUtil::transitionImageLayout(m_rhi.get(),
                                             ((VulkanImage*)rhiTextureImage)->getResource(),
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             1, // layer_count
                                             1, // miplevels
                                             VK_IMAGE_ASPECT_COLOR_BIT);
            
            // Clean up staging buffer
            m_rhi->destroyBuffer(stagingBuffer);
            m_rhi->freeMemory(stagingBufferMemory);
            
        }
        
        return true;
    }
    
    bool RenderResource::createSingleDefaultTexture(RenderObject& renderObject, size_t index)
    {
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createSingleDefaultTexture] RHI pointer is null!");
            return false;
        }
        
        // Create a simple 1x1 white texture as default
        const int texWidth = 1;
        const int texHeight = 1;
        const int texChannels = 4;
        
        // White pixel data (RGBA)
        unsigned char pixels[4] = {255, 255, 255, 255};
        
        // Create staging buffer for default texture data
        RHIDeviceSize imageSize = texWidth * texHeight * texChannels;
        RHIBuffer* stagingBuffer = nullptr;
        RHIDeviceMemory* stagingBufferMemory = nullptr;
        
        m_rhi->createBuffer(imageSize, 
                           RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           stagingBuffer, 
                           stagingBufferMemory);
        
        // Copy pixel data to staging buffer
        void* data;
        m_rhi->mapMemory(stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        m_rhi->unmapMemory(stagingBufferMemory);
        
        // Create image
        RHIImage* rhiTextureImage = nullptr;
        RHIDeviceMemory* rhiTextureImageMemory = nullptr;
        
        m_rhi->createImage(texWidth, texHeight, 
                          RHI_FORMAT_R8G8B8A8_SRGB, 
                          RHI_IMAGE_TILING_OPTIMAL,
                          RHI_IMAGE_USAGE_TRANSFER_DST_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
                          RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          rhiTextureImage, 
                          rhiTextureImageMemory, 
                          0, // image_create_flags
                          1, // array_layers
                          1); // miplevels
        
        // Store RHI image pointers
        renderObject.textureImages[index] = rhiTextureImage;
        renderObject.textureImageMemorys[index] = rhiTextureImageMemory;
        
        // Create image view
        RHIImageView* rhiTextureImageView = nullptr;
        m_rhi->createImageView(rhiTextureImage, 
                              RHI_FORMAT_R8G8B8A8_SRGB, 
                              RHI_IMAGE_ASPECT_COLOR_BIT,
                              RHI_IMAGE_VIEW_TYPE_2D, 
                              1, // layout_count
                              1, // miplevels
                              rhiTextureImageView);
        
        renderObject.textureImageViews[index] = rhiTextureImageView;
        
        // Create sampler using default sampler
        RHISampler* rhiTextureSampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);
        renderObject.textureSamplers[index] = rhiTextureSampler;
        
        // Transition image layout and copy buffer to image
        // First transition from undefined to transfer destination
        VulkanUtil::transitionImageLayout(m_rhi.get(),
                                         ((VulkanImage*)rhiTextureImage)->getResource(),
                                         VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         1, // layer_count
                                         1, // miplevels
                                         VK_IMAGE_ASPECT_COLOR_BIT);
        
        // Copy buffer data to image
        VulkanUtil::copyBufferToImage(m_rhi.get(),
                                    ((VulkanBuffer*)stagingBuffer)->getResource(),
                                    ((VulkanImage*)rhiTextureImage)->getResource(),
                                    texWidth,
                                    texHeight,
                                    1); // layer_count
        
        // Transition image layout from transfer destination to shader read only
        VulkanUtil::transitionImageLayout(m_rhi.get(),
                                         ((VulkanImage*)rhiTextureImage)->getResource(),
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         1, // layer_count
                                         1, // miplevels
                                         VK_IMAGE_ASPECT_COLOR_BIT);
        
        // Clean up staging buffer
        m_rhi->destroyBuffer(stagingBuffer);
        m_rhi->freeMemory(stagingBufferMemory);
        
        return true;
    }
    
    bool RenderResource::createModelPipelineResource(RHIRenderPass* renderPass)
    {
        
        
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createModelPipelineResource] RHI pointer is null!");
            return false;
        }
        
        if (m_modelPipelineResourceCreated) {
            
            return true;
        }
        
        // PBR shader expects: binding 0 (MVP UBO), binding 1 (View UBO), binding 2 (cubemap), binding 3-7 (textures), binding 8 (shadow map), binding 9 (light space matrix UBO)
        std::vector<RHIDescriptorSetLayoutBinding> model_bindings(10);

        // Binding 0: Fixed UBO binding(MVP) - 修复：片段着色器也使用此UBO，需要包含片段阶段标志
        model_bindings[0].binding = 0;
        model_bindings[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[0].descriptorCount = 1;
        model_bindings[0].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[0].pImmutableSamplers = nullptr;
        // Binding 1: Fixed UBO binding（视图矩阵）
        model_bindings[1].binding = 1;
        model_bindings[1].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[1].descriptorCount = 1;
        model_bindings[1].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[1].pImmutableSamplers = nullptr;
        // Binding 2: 环境反射Cubemap贴图绑定
        model_bindings[2].binding =2;
        model_bindings[2].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        model_bindings[2].descriptorCount = 1;
        model_bindings[2].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[2].pImmutableSamplers = nullptr;

        // Bindings 3 to 7: Texture sampler bindings
        for (uint32_t i = 3; i < 8; ++i) {
            model_bindings[i].binding = i;
            model_bindings[i].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            model_bindings[i].descriptorCount = 1;
            model_bindings[i].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
            model_bindings[i].pImmutableSamplers = nullptr;
        }
        
        // Binding 8: 方向光阴影贴图绑定
        model_bindings[8].binding = 8;
        model_bindings[8].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        model_bindings[8].descriptorCount = 1;
        model_bindings[8].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
        model_bindings[8].pImmutableSamplers = nullptr;
        
        // Binding 9: 光源投影视图矩阵UBO绑定
        model_bindings[9].binding = 9;
        model_bindings[9].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[9].descriptorCount = 1;
        model_bindings[9].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
        model_bindings[9].pImmutableSamplers = nullptr;

        RHIDescriptorSetLayoutCreateInfo model_layoutInfo{};
        model_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        model_layoutInfo.bindingCount = 10;
        model_layoutInfo.pBindings = model_bindings.data();
        
        if (m_rhi->createDescriptorSetLayout(&model_layoutInfo, m_modelPipelineResource.descriptorSetLayout) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createModelPipelineResource] Failed to create descriptor set layout");
            return false;
        }
        
        // Create pipeline layout with Push Constants support
        RHIDescriptorSetLayout* descriptorSetLayouts[] = {m_modelPipelineResource.descriptorSetLayout};
        
        // 配置Push Constants范围 - 用于传递model矩阵
        RHIPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;  // 顶点着色器阶段
        pushConstantRange.offset = 0;                               // 偏移量为0
        pushConstantRange.size = sizeof(glm::mat4);                 // model矩阵大小(64字节)
        
        RHIPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;              // 启用Push Constants
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange; // 设置Push Constants范围
        
        if (m_rhi->createPipelineLayout(&pipelineLayoutInfo, m_modelPipelineResource.pipelineLayout) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createModelPipelineResource] Failed to create pipeline layout");
            // TODO: Add destroyDescriptorSetLayout method to RHI interface
            delete m_modelPipelineResource.descriptorSetLayout;
            return false;
        }
        
        // Create graphics pipeline using MODEL shaders
        RHIShader* vertShaderModule = m_rhi->createShaderModule(PBR_VERT);
        RHIShader* fragShaderModule = m_rhi->createShaderModule(PBR_FRAG);
        
        RHIPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = RHI_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        
        RHIPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = RHI_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
        
        RHIPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        
        // Vertex input configuration
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        
        RHIPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        
        // Input assembly
        RHIPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = RHI_FALSE;
        
        // Viewport state (using dynamic state, so pViewports and pScissors can be null)
        RHIPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr; // Will be set dynamically
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr; // Will be set dynamically
        
        // Rasterizer
        RHIPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = RHI_FALSE;
        rasterizer.rasterizerDiscardEnable = RHI_FALSE;
        rasterizer.polygonMode = RHI_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = RHI_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = RHI_FALSE;
        
        // Multisampling
        RHIPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = RHI_FALSE;
        multisampling.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;
        
        // Color blending
        RHIPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT | RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = RHI_FALSE;
        
        RHIPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = RHI_FALSE;
        colorBlending.logicOp = RHI_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        
        // Depth stencil
        RHIPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = RHI_TRUE;
        depthStencil.depthWriteEnable = RHI_TRUE;
        depthStencil.depthCompareOp = RHI_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = RHI_FALSE;
        depthStencil.stencilTestEnable = RHI_FALSE;
        
        // Dynamic state
        RHIDynamicState dynamicStates[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
        RHIPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
        
        // Create graphics pipeline
        RHIGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_modelPipelineResource.pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = RHI_NULL_HANDLE;
        
        if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE, 1, &pipelineInfo, m_modelPipelineResource.graphicsPipeline) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createModelPipelineResource] Failed to create graphics pipeline");
            // TODO: Add proper cleanup methods to RHI interface
            delete m_modelPipelineResource.pipelineLayout;
            delete m_modelPipelineResource.descriptorSetLayout;
            m_rhi->destroyShaderModule(vertShaderModule);
            m_rhi->destroyShaderModule(fragShaderModule);
            return false;
        }
        
        // Clean up shader modules
        m_rhi->destroyShaderModule(vertShaderModule);
        m_rhi->destroyShaderModule(fragShaderModule);
        
        m_modelPipelineResourceCreated = true;
        
        return true;
    }
    /**
     * @brief Loads a cubemap texture from specified file paths.
     * @param cubemapFiles An array of 6 file paths for the cubemap faces (e.g., +X, -X, +Y, -Y, +Z, -Z).
     * @return True if the cubemap is loaded successfully, false otherwise.
     */
    bool RenderResource::loadCubemapTexture(const std::array<std::string, 6>& cubemapFiles) {
        LOG_INFO("[Skybox] Starting cubemap texture loading");
        std::array<void*, 6> pixels;
        int texWidth, texHeight, texChannels;

        for (int i = 0; i < 6; ++i) {
            LOG_DEBUG("[Skybox] Loading cubemap face {}: {}", i, cubemapFiles[i]);
            pixels[i] = stbi_load(cubemapFiles[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            if (!pixels[i]) {
                LOG_ERROR("[Skybox] Failed to load cubemap image: {}", cubemapFiles[i]);
                for (int j = 0; j < i; ++j) {
                    stbi_image_free(pixels[j]);
                }
                return false;
            }
            LOG_DEBUG("[Skybox] Successfully loaded cubemap face {} ({}x{}, {} channels)", i, texWidth, texHeight, texChannels);
        }
        uint32_t miplevels =
            static_cast<uint32_t>(
                std::floor(log2(std::max(texWidth, texHeight)))) +
            1;
        LOG_DEBUG("[Skybox] Creating cubemap with dimensions {}x{}, {} mip levels", texWidth, texHeight, miplevels);
        
        // Create cubemap using RHI
        m_rhi->createCubeMap(m_cubemapImage, m_cubemapImageView, m_cubemapImageAllocation, texWidth, texHeight, pixels, RHI_FORMAT_R8G8B8A8_UNORM, miplevels);
        LOG_DEBUG("[Skybox] Cubemap image and image view created successfully");

        // Create cubemap sampler
        cubemapSampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);
        if (!cubemapSampler) {
            LOG_ERROR("[Skybox] Failed to create cubemap sampler");
            return false;
        }
        LOG_DEBUG("[Skybox] Cubemap sampler created successfully");

        for (int i = 0; i < 6; ++i) {
            stbi_image_free(pixels[i]);
        }

        LOG_INFO("[Skybox] Cubemap texture loading completed successfully");
        return true;
    }

    // ModelAnimationParams序列化方法实现
    json11::Json ModelAnimationParams::toJson() const {
        return json11::Json::object {
            {"position", json11::Json::array {position.x, position.y, position.z}},
            {"rotation", json11::Json::array {rotation.x, rotation.y, rotation.z}},
            {"scale", json11::Json::array {scale.x, scale.y, scale.z}},
            {"rotationAxis", json11::Json::array {rotationAxis.x, rotationAxis.y, rotationAxis.z}},
            {"rotationSpeed", rotationSpeed},
            {"enableAnimation", enableAnimation},
            {"isPlatform", isPlatform}
        };
    }

    bool ModelAnimationParams::fromJson(const json11::Json& json) {
        if (!json.is_object()) {
            return false;
        }

        // 解析position
        auto pos_json = json["position"];
        if (pos_json.is_array() && pos_json.array_items().size() == 3) {
            position.x = static_cast<float>(pos_json[0].number_value());
            position.y = static_cast<float>(pos_json[1].number_value());
            position.z = static_cast<float>(pos_json[2].number_value());
        }

        // 解析rotation
        auto rot_json = json["rotation"];
        if (rot_json.is_array() && rot_json.array_items().size() == 3) {
            rotation.x = static_cast<float>(rot_json[0].number_value());
            rotation.y = static_cast<float>(rot_json[1].number_value());
            rotation.z = static_cast<float>(rot_json[2].number_value());
        }

        // 解析scale
        auto scale_json = json["scale"];
        if (scale_json.is_array() && scale_json.array_items().size() == 3) {
            scale.x = static_cast<float>(scale_json[0].number_value());
            scale.y = static_cast<float>(scale_json[1].number_value());
            scale.z = static_cast<float>(scale_json[2].number_value());
        }

        // 解析rotationAxis
        auto axis_json = json["rotationAxis"];
        if (axis_json.is_array() && axis_json.array_items().size() == 3) {
            rotationAxis.x = static_cast<float>(axis_json[0].number_value());
            rotationAxis.y = static_cast<float>(axis_json[1].number_value());
            rotationAxis.z = static_cast<float>(axis_json[2].number_value());
        }

        // 解析其他参数
        if (json["rotationSpeed"].is_number()) {
            rotationSpeed = static_cast<float>(json["rotationSpeed"].number_value());
        }
        if (json["enableAnimation"].is_bool()) {
            enableAnimation = json["enableAnimation"].bool_value();
        }
        if (json["isPlatform"].is_bool()) {
            isPlatform = json["isPlatform"].bool_value();
        }

        return true;
    }

    // ========================================================================
    // 方向光源管理接口实现
    // ========================================================================
    
    size_t RenderResource::addDirectionalLight(const DirectionalLightData& lightData)
    {
        m_directionalLights.push_back(lightData);
        size_t index = m_directionalLights.size() - 1;
        LOG_INFO("[RenderResource::addDirectionalLight] Added directional light at index: {}", index);
        return index;
    }
    
    bool RenderResource::updateDirectionalLight(size_t index, const DirectionalLightData& lightData)
    {
        if (index >= m_directionalLights.size()) {
            LOG_ERROR("[RenderResource::updateDirectionalLight] Invalid light index: {}", index);
            return false;
        }
        
        m_directionalLights[index] = lightData;
        LOG_INFO("[RenderResource::updateDirectionalLight] Updated directional light at index: {}", index);
        return true;
    }
    
    bool RenderResource::removeDirectionalLight(size_t index)
    {
        if (index >= m_directionalLights.size()) {
            LOG_ERROR("[RenderResource::removeDirectionalLight] Invalid light index: {}", index);
            return false;
        }
        
        m_directionalLights.erase(m_directionalLights.begin() + index);
        LOG_INFO("[RenderResource::removeDirectionalLight] Removed directional light at index: {}", index);
        return true;
    }
    
    const DirectionalLightData* RenderResource::getDirectionalLight(size_t index) const
    {
        if (index >= m_directionalLights.size()) {
            return nullptr;
        }
        return &m_directionalLights[index];
    }
    
    void RenderResource::clearDirectionalLights()
    {
        m_directionalLights.clear();
        LOG_INFO("[RenderResource::clearDirectionalLights] Cleared all directional lights");
    }
    
    const DirectionalLightData* RenderResource::getPrimaryDirectionalLight() const
    {
        for (const auto& light : m_directionalLights) {
            if (light.enabled) {
                return &light;
            }
        }
        return nullptr;
    }
    
    bool RenderResource::updateDirectionalLightDirection(const glm::vec3& direction)
    {
        // 查找第一个启用的方向光源
        for (size_t i = 0; i < m_directionalLights.size(); ++i) {
            if (m_directionalLights[i].enabled) {
                m_directionalLights[i].direction = glm::normalize(direction);
                LOG_INFO("[RenderResource::updateDirectionalLightDirection] Updated light direction to ({:.3f}, {:.3f}, {:.3f})", 
                         direction.x, direction.y, direction.z);
                return true;
            }
        }
        
        LOG_WARN("[RenderResource::updateDirectionalLightDirection] No enabled directional light found");
        return false;
    }
    
    bool RenderResource::updateDirectionalLightIntensity(float intensity)
    {
        // 查找第一个启用的方向光源
        for (size_t i = 0; i < m_directionalLights.size(); ++i) {
            if (m_directionalLights[i].enabled) {
                m_directionalLights[i].intensity = std::max(0.0f, intensity);
                LOG_INFO("[RenderResource::updateDirectionalLightIntensity] Updated light intensity to {:.3f}", intensity);
                return true;
            }
        }
        
        LOG_WARN("[RenderResource::updateDirectionalLightIntensity] No enabled directional light found");
        return false;
    }
    
    bool RenderResource::updateDirectionalLightColor(const glm::vec3& color)
    {
        // 查找第一个启用的方向光源
        for (size_t i = 0; i < m_directionalLights.size(); ++i) {
            if (m_directionalLights[i].enabled) {
                // 确保颜色值在合理范围内
                m_directionalLights[i].color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
                LOG_INFO("[RenderResource::updateDirectionalLightColor] Updated light color to ({:.3f}, {:.3f}, {:.3f})", 
                         color.r, color.g, color.b);
                return true;
            }
        }
        
        LOG_WARN("[RenderResource::updateDirectionalLightColor] No enabled directional light found");
        return false;
    }
    
    bool RenderResource::updateDirectionalLightDistance(float distance)
    {
        // 查找第一个启用的方向光源
        for (size_t i = 0; i < m_directionalLights.size(); ++i) {
            if (m_directionalLights[i].enabled) {
                // 确保距离值在合理范围内
                m_directionalLights[i].distance = std::max(1.0f, std::min(distance, 100.0f));
                LOG_INFO("[RenderResource::updateDirectionalLightDistance] Updated light distance to {:.3f}", distance);
                return true;
            }
        }
        
        LOG_WARN("[RenderResource::updateDirectionalLightDistance] No enabled directional light found");
        return false;
    }
    
    /**
     * @brief 创建光线追踪管线资源
     * @return 创建是否成功
     */
    bool RenderResource::createRayTracingPipelineResource()
    {
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createRayTracingPipelineResource] RHI pointer is null!");
            return false;
        }
        
        if (m_rayTracingPipelineResourceCreated) {
            LOG_INFO("[RenderResource::createRayTracingPipelineResource] Ray tracing pipeline resource already created");
            return true;
        }
        
        // 创建光线追踪描述符集布局
        std::vector<RHIDescriptorSetLayoutBinding> rt_bindings(6);
        
        // Binding 0: 加速结构（TLAS）
        rt_bindings[0].binding = 0;
        rt_bindings[0].descriptorType = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        rt_bindings[0].descriptorCount = 1;
        rt_bindings[0].stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rt_bindings[0].pImmutableSamplers = nullptr;
        
        // Binding 1: 光线追踪输出图像
        rt_bindings[1].binding = 1;
        rt_bindings[1].descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        rt_bindings[1].descriptorCount = 1;
        rt_bindings[1].stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;
        rt_bindings[1].pImmutableSamplers = nullptr;
        
        // Binding 2: 顶点缓冲区
        rt_bindings[2].binding = 2;
        rt_bindings[2].descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        rt_bindings[2].descriptorCount = 1;
        rt_bindings[2].stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rt_bindings[2].pImmutableSamplers = nullptr;
        
        // Binding 3: 索引缓冲区
        rt_bindings[3].binding = 3;
        rt_bindings[3].descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        rt_bindings[3].descriptorCount = 1;
        rt_bindings[3].stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rt_bindings[3].pImmutableSamplers = nullptr;
        
        // Binding 4: 材质纹理数组
        rt_bindings[4].binding = 4;
        rt_bindings[4].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rt_bindings[4].descriptorCount = 5; // 支持5个纹理（diffuse, normal, metallic, roughness, ao）
        rt_bindings[4].stageFlags = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        rt_bindings[4].pImmutableSamplers = nullptr;
        
        // Binding 5: 相机参数UBO
        rt_bindings[5].binding = 5;
        rt_bindings[5].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        rt_bindings[5].descriptorCount = 1;
        rt_bindings[5].stageFlags = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;
        rt_bindings[5].pImmutableSamplers = nullptr;
        
        RHIDescriptorSetLayoutCreateInfo rt_layoutInfo{};
        rt_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        rt_layoutInfo.bindingCount = static_cast<uint32_t>(rt_bindings.size());
        rt_layoutInfo.pBindings = rt_bindings.data();
        
        if (m_rhi->createDescriptorSetLayout(&rt_layoutInfo, m_rayTracingPipelineResource.descriptorSetLayout) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingPipelineResource] Failed to create descriptor set layout");
            return false;
        }
        
        // 创建管线布局
        RHIDescriptorSetLayout* descriptorSetLayouts[] = {m_rayTracingPipelineResource.descriptorSetLayout};
        
        RHIPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        
        if (m_rhi->createPipelineLayout(&pipelineLayoutInfo, m_rayTracingPipelineResource.pipelineLayout) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingPipelineResource] Failed to create pipeline layout");
            delete m_rayTracingPipelineResource.descriptorSetLayout;
            return false;
        }
        
        // 创建着色器模块
        RHIShader* raygenShader = m_rhi->createShaderModule(RAYTRACING_RGEN);
        RHIShader* missShader = m_rhi->createShaderModule(RAYTRACING_RMISS);
        RHIShader* shadowMissShader = m_rhi->createShaderModule(SHADOW_RMISS);
        RHIShader* closestHitShader = m_rhi->createShaderModule(RAYTRACING_RCHIT);
        
        if (!raygenShader || !missShader || !shadowMissShader || !closestHitShader) {
            LOG_ERROR("[RenderResource::createRayTracingPipelineResource] Failed to create shader modules");
            if (raygenShader) m_rhi->destroyShaderModule(raygenShader);
            if (missShader) m_rhi->destroyShaderModule(missShader);
            if (shadowMissShader) m_rhi->destroyShaderModule(shadowMissShader);
            if (closestHitShader) m_rhi->destroyShaderModule(closestHitShader);
            delete m_rayTracingPipelineResource.pipelineLayout;
            delete m_rayTracingPipelineResource.descriptorSetLayout;
            return false;
        }
        
        // 创建着色器阶段信息
        std::vector<RHIPipelineShaderStageCreateInfo> shaderStages(4);
        
        // Raygen着色器
        shaderStages[0].sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;
        shaderStages[0].module = raygenShader;
        shaderStages[0].pName = "main";
        
        // Miss着色器
        shaderStages[1].sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = RHI_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[1].module = missShader;
        shaderStages[1].pName = "main";
        
        // Shadow Miss着色器
        shaderStages[2].sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[2].stage = RHI_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[2].module = shadowMissShader;
        shaderStages[2].pName = "main";
        
        // Closest Hit着色器
        shaderStages[3].sType = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[3].stage = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        shaderStages[3].module = closestHitShader;
        shaderStages[3].pName = "main";
        
        // 创建着色器组
        std::vector<RHIRayTracingShaderGroupCreateInfo> shaderGroups(4);
        
        // Raygen组
        shaderGroups[0].sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[0].type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[0].generalShader = 0;
        shaderGroups[0].closestHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[0].anyHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[0].intersectionShader = RHI_SHADER_UNUSED_KHR;
        
        // Miss组
        shaderGroups[1].sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[1].type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[1].generalShader = 1;
        shaderGroups[1].closestHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[1].anyHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[1].intersectionShader = RHI_SHADER_UNUSED_KHR;
        
        // Shadow Miss组
        shaderGroups[2].sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[2].type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[2].generalShader = 2;
        shaderGroups[2].closestHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[2].anyHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[2].intersectionShader = RHI_SHADER_UNUSED_KHR;
        
        // Closest Hit组
        shaderGroups[3].sType = RHI_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[3].type = RHI_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroups[3].generalShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[3].closestHitShader = 3;
        shaderGroups[3].anyHitShader = RHI_SHADER_UNUSED_KHR;
        shaderGroups[3].intersectionShader = RHI_SHADER_UNUSED_KHR;
        
        // 创建光线追踪管线
        RHIRayTracingPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = RHI_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
        pipelineInfo.pGroups = shaderGroups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = 2; // 支持2层递归（主光线+阴影光线）
        pipelineInfo.layout = m_rayTracingPipelineResource.pipelineLayout;
        pipelineInfo.basePipelineHandle = nullptr;
        pipelineInfo.basePipelineIndex = -1;
        
        if (m_rhi->createRayTracingPipelines(nullptr, 1, &pipelineInfo, m_rayTracingPipelineResource.rayTracingPipeline) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingPipelineResource] Failed to create ray tracing pipeline");
            m_rhi->destroyShaderModule(raygenShader);
            m_rhi->destroyShaderModule(missShader);
            m_rhi->destroyShaderModule(shadowMissShader);
            m_rhi->destroyShaderModule(closestHitShader);
            delete m_rayTracingPipelineResource.pipelineLayout;
            delete m_rayTracingPipelineResource.descriptorSetLayout;
            return false;
        }
        
        // 创建着色器绑定表（SBT）
        // TODO: 实现SBT创建逻辑
        m_rayTracingPipelineResource.shaderBindingTable = nullptr;
        m_rayTracingPipelineResource.shaderBindingTableAllocation = VK_NULL_HANDLE;
        
        // 清理着色器模块
        m_rhi->destroyShaderModule(raygenShader);
        m_rhi->destroyShaderModule(missShader);
        m_rhi->destroyShaderModule(shadowMissShader);
        m_rhi->destroyShaderModule(closestHitShader);
        
        m_rayTracingPipelineResourceCreated = true;
        LOG_INFO("[RenderResource::createRayTracingPipelineResource] Ray tracing pipeline resource created successfully");
        
        return true;
    }
    
    /**
     * @brief 创建光线追踪资源（加速结构等）
     * @return 创建是否成功
     */
    bool RenderResource::createRayTracingResource()
    {
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::createRayTracingResource] RHI pointer is null!");
            return false;
        }
        
        if (m_rayTracingResourceCreated) {
            LOG_INFO("[RenderResource::createRayTracingResource] Ray tracing resource already created");
            return true;
        }
        
        if (m_RenderObjects.empty()) {
            LOG_WARN("[RenderResource::createRayTracingResource] No render objects available for acceleration structure creation");
            return false;
        }
        
        // 创建底层加速结构（BLAS）
        m_rayTracingResource.bottomLevelAS.resize(m_RenderObjects.size());
        m_rayTracingResource.bottomLevelASBuffers.resize(m_RenderObjects.size());
        m_rayTracingResource.bottomLevelASAllocations.resize(m_RenderObjects.size());
        
        for (size_t i = 0; i < m_RenderObjects.size(); ++i) {
            const auto& renderObject = m_RenderObjects[i];
            
            // 创建BLAS几何信息
            RHIAccelerationStructureGeometry geometry{};
            geometry.sType = RHI_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            geometry.geometryType = RHI_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.triangles.vertexData.deviceAddress = 0; // 需要从缓冲区获取设备地址
            geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.triangles.vertexFormat = RHI_FORMAT_R32G32B32_SFLOAT;
            geometry.triangles.indexData.deviceAddress = 0; // 需要从缓冲区获取设备地址
            geometry.triangles.indexType = RHI_INDEX_TYPE_UINT32;
            geometry.flags = RHI_GEOMETRY_OPAQUE_BIT_KHR;
            
            // 创建BLAS构建信息
            RHIAccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.sType = RHI_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildInfo.type = RHI_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildInfo.flags = RHI_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;
            
            // 创建BLAS
            RHIAccelerationStructureCreateInfo createInfo{};
            createInfo.sType = RHI_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            createInfo.type = RHI_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            createInfo.size = 0; // 将在构建时确定
            
            if (m_rhi->createAccelerationStructure(&createInfo, m_rayTracingResource.bottomLevelAS[i]) != RHI_SUCCESS) {
                LOG_ERROR("[RenderResource::createRayTracingResource] Failed to create BLAS {}", i);
                return false;
            }
        }
        
        // 创建顶层加速结构（TLAS）
        RHIAccelerationStructureCreateInfo tlasCreateInfo{};
        tlasCreateInfo.sType = RHI_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        tlasCreateInfo.type = RHI_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tlasCreateInfo.size = 0; // 将在构建时确定
        
        if (m_rhi->createAccelerationStructure(&tlasCreateInfo, m_rayTracingResource.topLevelAS) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingResource] Failed to create TLAS");
            return false;
        }
        
        // 创建光线追踪输出图像
        // TODO: 从渲染器获取实际的屏幕尺寸
        uint32_t width = 1920;
        uint32_t height = 1080;
        
        RHIImageCreateInfo imageInfo{};
        imageInfo.sType = RHI_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = RHI_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = RHI_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = RHI_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = RHI_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = RHI_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = RHI_SHARING_MODE_EXCLUSIVE;
        
        if (m_rhi->createImage(&imageInfo, m_rayTracingResource.rayTracingOutputImage) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingResource] Failed to create ray tracing output image");
            return false;
        }
        
        // 创建图像视图
        RHIImageViewCreateInfo viewInfo{};
        viewInfo.sType = RHI_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_rayTracingResource.rayTracingOutputImage;
        viewInfo.viewType = RHI_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = RHI_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = RHI_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (m_rhi->createImageView(&viewInfo, m_rayTracingResource.rayTracingOutputImageView) != RHI_SUCCESS) {
            LOG_ERROR("[RenderResource::createRayTracingResource] Failed to create ray tracing output image view");
            return false;
        }
        
        m_rayTracingResourceCreated = true;
        LOG_INFO("[RenderResource::createRayTracingResource] Ray tracing resource created successfully");
        
        return true;
    }
    
    /**
     * @brief 更新光线追踪加速结构
     * @return 更新是否成功
     */
    bool RenderResource::updateRayTracingAccelerationStructures()
    {
        if (!m_rhi) {
            LOG_ERROR("[RenderResource::updateRayTracingAccelerationStructures] RHI pointer is null!");
            return false;
        }
        
        if (!m_rayTracingResourceCreated) {
            LOG_WARN("[RenderResource::updateRayTracingAccelerationStructures] Ray tracing resource not created yet");
            return false;
        }
        
        // TODO: 实现加速结构更新逻辑
        // 这里应该包括：
        // 1. 更新实例缓冲区
        // 2. 重建TLAS
        // 3. 处理动态对象的BLAS更新
        
        LOG_INFO("[RenderResource::updateRayTracingAccelerationStructures] Acceleration structures updated successfully");
        return true;
    }

} // namespace Elish