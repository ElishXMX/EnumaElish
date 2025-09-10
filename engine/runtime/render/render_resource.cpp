#include "render_resource.h"
#include "../../3rdparty/tinyobjloader/tiny_obj_loader.h"
#include "../core/base/macro.h"
#include "../../3rdparty/stb/stb_image.h"
#include "../shader/generated/cpp/PBR_vert.h"
#include "../shader/generated/cpp/PBR_frag.h"
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
        
        // PBR shader expects: binding 0 (MVP UBO), binding 1 (View UBO), binding 2 (cubemap), binding 3-7 (textures)
        std::vector<RHIDescriptorSetLayoutBinding> model_bindings(8);

        // Binding 0: Fixed UBO binding(MVP)
        model_bindings[0].binding = 0;
        model_bindings[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        model_bindings[0].descriptorCount = 1;
        model_bindings[0].stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
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

        // Bindings 3 to layout_size-1: Texture sampler bindings
        for (uint32_t i = 3; i < 8; ++i) {
            model_bindings[i].binding = i;
            model_bindings[i].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            model_bindings[i].descriptorCount = 1;
            model_bindings[i].stageFlags = RHI_SHADER_STAGE_FRAGMENT_BIT;
            model_bindings[i].pImmutableSamplers = nullptr;
        }

        RHIDescriptorSetLayoutCreateInfo model_layoutInfo{};
        model_layoutInfo.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        model_layoutInfo.bindingCount = 8;
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
        std::array<void*, 6> pixels;
        int texWidth, texHeight, texChannels;

        for (int i = 0; i < 6; ++i) {
            pixels[i] = stbi_load(cubemapFiles[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            if (!pixels[i]) {
                LOG_ERROR("[RenderResource::loadCubemapTexture] Failed to load cubemap image: {}", cubemapFiles[i]);
                for (int j = 0; j < i; ++j) {
                    stbi_image_free(pixels[j]);
                }
                return false;
            }
        }
        uint32_t miplevels =
            static_cast<uint32_t>(
                std::floor(log2(std::max(texWidth, texHeight)))) +
            1;
        // Create cubemap using RHI
        m_rhi->createCubeMap(m_cubemapImage, m_cubemapImageView, m_cubemapImageAllocation, texWidth, texHeight, pixels, RHI_FORMAT_R8G8B8A8_UNORM, miplevels);

        // Create cubemap sampler
        cubemapSampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);
        if (!cubemapSampler) {
            LOG_ERROR("[RenderResource::loadCubemapTexture] Failed to create cubemap sampler");
            return false;
        }

        for (int i = 0; i < 6; ++i) {
            stbi_image_free(pixels[i]);
        }

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

} // namespace Elish