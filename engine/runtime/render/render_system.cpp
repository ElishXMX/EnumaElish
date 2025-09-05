#include "render_system.h"
#include "window_system.h"
#include "../core/base/macro.h"

#include "interface/vulkan/vulkan_rhi.h"
#include "render_pipeline.h"
#include "render_pipeline_base.h"
#include "render_resource.h"
#include "passes/main_camera_pass.h"
#include "../../3rdparty/tinyobjloader/tiny_obj_loader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <stdexcept>
#include <array>
#include <chrono>
#include <unordered_set>
#include "render_camera.h"

// Forward declaration
// using namespace Elish;

namespace Elish
{
    // RenderSystem::~RenderSystem() = default;
    //创建Vulkan实例/加载全局资源/初始化相机和管线
    void RenderSystem::initialize(RenderSystemInitInfo init_info )
    {
        //vulkan初始化
         // render context initialize
        RHIInitInfo rhi_init_info;
        rhi_init_info.window_system = init_info.window_system;

        m_rhi = std::make_shared<VulkanRHI>();
        m_rhi->initialize(rhi_init_info);
        //vulkan实例创建完成
        
        // initialize render resource
        m_render_resource = std::make_shared<RenderResource>();
        m_render_resource->initialize(m_rhi);

        // load content resources from JSON configuration
        std::unordered_map<std::string, std::string> model_paths;
        std::unordered_map<std::string, std::vector<std::string>> model_texture_map;
        
        // 尝试从JSON文件加载资源配置
        const std::string json_config_path = "../engine/runtime/content/levels/levels1.json";
        LOG_INFO("[JSON_LOADER] Attempting to load resources from: {}", json_config_path);
        if (loadResourcesFromJson(json_config_path, model_paths, model_texture_map)) {
            LOG_INFO("[JSON_LOADER] Successfully loaded resources from JSON configuration: {}", json_config_path);
            std::cout << "[JSON_LOADER] Successfully loaded resources from JSON configuration: " << json_config_path << std::endl;
        } else {
            // 如果JSON加载失败，使用硬编码的备用配置
            LOG_WARN("[JSON_LOADER] Failed to load JSON configuration, using fallback hardcoded resources.");
            
           
        }

        loadContentResources(model_paths, model_texture_map);
        

        // load cubemap
        std::array<std::string, 6> cubemapFiles = {
            "engine/content/textures/cubemap_X0.png",
            "engine/content/textures/cubemap_X1.png",
            "engine/content/textures/cubemap_Y2.png",
            "engine/content/textures/cubemap_Y3.png",
            "engine/content/textures/cubemap_Z4.png",
            "engine/content/textures/cubemap_Z5.png",
        };
        m_render_resource->loadCubemapTexture(cubemapFiles);
        
        // initialize render pipeline
        m_render_pipeline        = std::make_shared<RenderPipeline>();
        m_render_pipeline->m_rhi = m_rhi;
        m_render_pipeline->initialize();

       
        
    }

    void RenderSystem::tick(float delta_time)
    {
        m_rhi->prepareContext();

        m_render_pipeline->preparePassData(m_render_resource);

        m_render_pipeline->forwardRender(m_rhi, m_render_resource);
    }
    void RenderSystem::loadContentResources(const std::unordered_map<std::string, std::string>& model_paths,
                                            const std::unordered_map<std::string, std::vector<std::string>>& model_texture_map)
    {
        int loadedModels = 0;
        int failedModels = 0;
        
        for (const auto& pair : model_paths) {
            const std::string& modelName = pair.first;
            const std::string& objPath = pair.second;
            
            // 调整模型文件路径，相对于build-ninja目录
            std::string adjustedObjPath = "../" + objPath;
            
            // Check if model file exists
            std::filesystem::path modelFilePath(adjustedObjPath);
            if (!std::filesystem::exists(modelFilePath)) {
                LOG_ERROR("Model file does not exist: {}", adjustedObjPath);
                LOG_ERROR("Absolute path: {}", std::filesystem::absolute(modelFilePath).string());
                failedModels++;
                continue;
            } else {
                
                
            }
            
            std::vector<std::string> textureFiles;
            if (model_texture_map.count(modelName)) {
                // 调整纹理文件路径
                std::vector<std::string> originalTextures = model_texture_map.at(modelName);
                textureFiles.clear();
                for (const auto& texPath : originalTextures) {
                    textureFiles.push_back("../" + texPath);
                }
                
                
                // Validate each texture file
                int validTextures = 0;
                for (size_t i = 0; i < textureFiles.size(); ++i) {
                    const auto& texFile = textureFiles[i];
                    std::filesystem::path texturePath(texFile);
                    
                    if (std::filesystem::exists(texturePath)) {
                        
                        
                        validTextures++;
                    } else {
                        LOG_ERROR("  ✗ Texture {}: {} (FILE NOT FOUND)", i + 1, texFile);
                        LOG_ERROR("    Absolute path: {}", std::filesystem::absolute(texturePath).string());
                    }
                }
                
            } else {
                LOG_WARN("No textures found for model '{}', using default texture", modelName);
            }
            
            
            try {
                RenderObject renderObject;
                bool success = m_render_resource->createRenderObjectResource(renderObject, adjustedObjPath, textureFiles);
                if (success) {
                    m_render_resource->addRenderObject(renderObject);
                    loadedModels++;
                    
                    
                } else {
                    LOG_ERROR("✗ Failed to create render object for model '{}'", modelName);
                    failedModels++;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("✗ Exception while loading model '{}': {}", modelName, e.what());
                failedModels++;
            }
        }
        
        
        
        
        
        
        if (loadedModels > 0) {
            
        } else {
            LOG_ERROR("✗ No models were loaded successfully!");
        }
        
    }
    bool RenderSystem::readFileToString(const std::string& file_path, std::string& content) {
        try {
            std::ifstream file(file_path, std::ios::in | std::ios::binary);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open file: {}", file_path);
                return false;
            }

            // 获取文件大小
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);

            // 读取文件内容
            content.resize(file_size);
            file.read(&content[0], file_size);
            
            if (file.fail() && !file.eof()) {
                LOG_ERROR("Failed to read file content: {}", file_path);
                return false;
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception while reading file '{}': {}", file_path, e.what());
            return false;
        }
    }

    bool RenderSystem::loadResourcesFromJson(const std::string& json_file_path,
                                           std::unordered_map<std::string, std::string>& model_paths,
                                           std::unordered_map<std::string, std::vector<std::string>>& model_texture_map) {
        // 读取JSON文件内容
        std::string json_content;
        if (!readFileToString(json_file_path, json_content)) {
            LOG_ERROR("Failed to read JSON file: {}", json_file_path);
            return false;
        }

        // 解析JSON内容
        std::string parse_error;
        json11::Json json_data = json11::Json::parse(json_content, parse_error);
        
        if (!parse_error.empty()) {
            LOG_ERROR("JSON parse error in file '{}': {}", json_file_path, parse_error);
            return false;
        }

        // 检查JSON结构是否有效
        if (!json_data.is_object()) {
            LOG_ERROR("Invalid JSON structure: root must be an object in file '{}'", json_file_path);
            return false;
        }

        // 解析嵌套的levels结构
        auto levels_json = json_data["levels"];
        if (!levels_json.is_array() || levels_json.array_items().empty()) {
            LOG_ERROR("Missing or invalid 'levels' array in JSON file: {}", json_file_path);
            return false;
        }

        // 获取第一个level的entities
        auto first_level = levels_json.array_items()[0];
        if (!first_level.is_object()) {
            LOG_ERROR("Invalid level structure in JSON file: {}", json_file_path);
            return false;
        }

        auto entities_json = first_level["entities"];
        if (!entities_json.is_array()) {
            LOG_ERROR("Missing or invalid 'entities' array in level: {}", json_file_path);
            return false;
        }

        // 遍历entities并提取模型和纹理信息
        for (const auto& entity : entities_json.array_items()) {
            if (!entity.is_object()) {
                LOG_WARN("Invalid entity structure, skipping");
                continue;
            }

            auto name_json = entity["name"];
            auto model_path_json = entity["model_paths"];
            auto texture_map_json = entity["model_texture_map"];

            if (!name_json.is_string() || !model_path_json.is_string()) {
                LOG_WARN("Invalid entity: missing name or model_paths");
                continue;
            }

            std::string entity_name = name_json.string_value();
            std::string model_path = model_path_json.string_value();
            
            // 添加模型路径
            model_paths[entity_name] = model_path;
            LOG_INFO("Loaded model path: {} -> {}", entity_name, model_path);

            // 添加纹理映射
            if (texture_map_json.is_array()) {
                std::vector<std::string> textures;
                for (const auto& texture_json : texture_map_json.array_items()) {
                    if (texture_json.is_string()) {
                        textures.push_back(texture_json.string_value());
                    } else {
                        LOG_WARN("Invalid texture path in array for entity '{}': expected string", entity_name);
                    }
                }
                model_texture_map[entity_name] = textures;
                LOG_INFO("Loaded texture map for '{}': {} textures", entity_name, textures.size());
            } else {
                LOG_WARN("Missing or invalid texture map for entity '{}'", entity_name);
            }
        }

        // 验证加载的数据
        if (model_paths.empty()) {
            LOG_ERROR("No valid model paths found in JSON file: {}", json_file_path);
            return false;
        }

        LOG_INFO("Successfully loaded {} model paths and {} texture maps from JSON", 
                model_paths.size(), model_texture_map.size());
        return true;
    }

    std::shared_ptr<RenderCamera> RenderSystem::getRenderCamera() const { return m_render_camera; }

    std::shared_ptr<RHI>          RenderSystem::getRHI() const { return m_rhi; }

}