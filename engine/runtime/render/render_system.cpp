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
#include <iomanip>
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
        LOG_INFO("[RENDER_SYSTEM] ===== RenderSystem initialization started =====");
        
        //vulkan初始化
         // render context initialize
        LOG_INFO("[RENDER_SYSTEM] Step 1: Initializing Vulkan RHI");
        RHIInitInfo rhi_init_info;
        rhi_init_info.window_system = init_info.window_system;

        m_rhi = std::make_shared<VulkanRHI>();
        m_rhi->initialize(rhi_init_info);
        LOG_INFO("[RENDER_SYSTEM] Step 1 completed: Vulkan RHI initialized");
        //vulkan实例创建完成
        
        // initialize render resource
        LOG_INFO("[RENDER_SYSTEM] Step 2: Initializing render resource");
        m_render_resource = std::make_shared<RenderResource>();
        m_render_resource->initialize(m_rhi);
        LOG_INFO("[RENDER_SYSTEM] Step 2 completed: Render resource initialized");

        // load content resources from JSON configuration
        std::unordered_map<std::string, std::string> model_paths;
        std::unordered_map<std::string, std::vector<std::string>> model_texture_map;
        std::unordered_map<std::string, ModelAnimationParams> model_animation_params;
        
        // 尝试从JSON文件加载资源配置
        const std::string json_config_path = "../engine/runtime/content/levels/levels1.json";
        LOG_INFO("[JSON_LOADER] ===== Starting JSON configuration loading =====");
        LOG_INFO("[JSON_LOADER] Attempting to load JSON configuration from: {}", json_config_path);
        LOG_INFO("[JSON_LOADER] Current working directory check...");
        if (loadResourcesFromJson(json_config_path, model_paths, model_texture_map, model_animation_params)) {
            LOG_INFO("[JSON_LOADER] Successfully loaded resources from JSON configuration: {}", json_config_path);
            LOG_INFO("[JSON_LOADER] Loaded {} models from JSON", model_paths.size());
            for (const auto& [name, path] : model_paths) {
                LOG_INFO("[JSON_LOADER] Model: {} -> {}", name, path);
            }
        } else {
            // 如果JSON加载失败，使用硬编码的备用配置
            LOG_ERROR("[JSON_LOADER] Failed to load JSON configuration, using fallback hardcoded resources.");
            LOG_ERROR("[JSON_LOADER] This will result in no models being loaded!");
        }

        loadContentResources(model_paths, model_texture_map, model_animation_params);
        

        // load cubemap
        LOG_INFO("[Skybox] Initializing cubemap resources for skybox rendering");
        std::array<std::string, 6> cubemapFiles = {
            "engine/content/textures/cubemap_X0.png",
            "engine/content/textures/cubemap_X1.png",
            "engine/content/textures/cubemap_Y2.png",
            "engine/content/textures/cubemap_Y3.png",
            "engine/content/textures/cubemap_Z4.png",
            "engine/content/textures/cubemap_Z5.png",
        };
        bool cubemapLoadSuccess = m_render_resource->loadCubemapTexture(cubemapFiles);
        if (cubemapLoadSuccess) {
            LOG_INFO("[Skybox] Cubemap resources initialized successfully");
        } else {
            LOG_ERROR("[Skybox] Failed to initialize cubemap resources - skybox rendering may not work properly");
        }
        
        // initialize render pipeline
        LOG_DEBUG("[RenderSystem] Creating render pipeline");
        m_render_pipeline        = std::make_shared<RenderPipeline>();
        LOG_DEBUG("[RenderSystem] Setting RHI for render pipeline");
        m_render_pipeline->m_rhi = m_rhi;
        LOG_DEBUG("[RenderSystem] Initializing render pipeline");
        m_render_pipeline->initialize();
        LOG_DEBUG("[RenderSystem] Render pipeline initialization completed");

        // 注意：光线追踪资源将在模型加载完成后创建
        if (!m_rhi->isRayTracingSupported()) {
            LOG_WARN("[RenderSystem] Ray tracing is not supported on this device");
        }
        
    }

    void RenderSystem::tick(float delta_time)
    {
        m_rhi->prepareContext();

        m_render_pipeline->preparePassData(m_render_resource);

        m_render_pipeline->forwardRender(m_rhi, m_render_resource);

        // 简易性能基准记录（启用方式：设置环境变量 ELISH_BENCH=1）
        static bool bench_enabled = [](){ const char* v = std::getenv("ELISH_BENCH"); return v && std::string(v) == "1"; }();
        static uint64_t frame_count = 0;
        static double acc_ms = 0.0;
        static auto last_ts = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        double dt_ms = std::chrono::duration<double, std::milli>(now - last_ts).count();
        last_ts = now;
        acc_ms += dt_ms;
        frame_count++;
        if (bench_enabled && frame_count % 120 == 0)
        {
            double avg_ms = acc_ms / 120.0;
            acc_ms = 0.0;
            double scale = 0.0;
            uint32_t spp = 0;
            uint32_t depth = 0;
            std::ofstream ofs("../bin/rt_benchmark.csv", std::ios::app);
            if (ofs)
            {
                ofs << std::fixed << std::setprecision(3)
                    << avg_ms << "," << (avg_ms > 0.0 ? (1000.0/avg_ms) : 0.0) << "," << scale << "," << spp << "," << depth << "\n";
            }
            LOG_INFO("[Benchmark] avg_ms={:.3f}, fps={:.1f}", avg_ms, (avg_ms>0.0? (1000.0/avg_ms) : 0.0));
        }
    }
    void RenderSystem::loadContentResources(const std::unordered_map<std::string, std::string>& model_paths,
                                            const std::unordered_map<std::string, std::vector<std::string>>& model_texture_map,
                                            const std::unordered_map<std::string, ModelAnimationParams>& model_animation_params)
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
                // LOG_DEBUG("✓ Model file exists: {}", adjustedObjPath);
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
                        // LOG_DEBUG("  ✓ Texture {}: {}", i + 1, texFile);
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
                renderObject.name = modelName;      // 设置模型名称（原有字段）
                renderObject.modelName = modelName; // 设置模型标识名称（新字段）
                
                // 应用动画参数（从JSON加载或使用默认值）
                if (model_animation_params.count(modelName)) {
                    renderObject.animationParams = model_animation_params.at(modelName);
                    // LOG_DEBUG("Applied loaded animation params for model '{}'", modelName);
                } else {
                    // 使用默认动画参数（初始状态保持静止）
                    renderObject.animationParams.position = glm::vec3(0.0f);
                    renderObject.animationParams.rotation = glm::vec3(0.0f);
                    renderObject.animationParams.scale = glm::vec3(1.0f);
                    renderObject.animationParams.rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                    renderObject.animationParams.rotationSpeed = 1.0f;
                    renderObject.animationParams.enableAnimation = false; // 默认禁用动画，保持静止
                    
                    // 检查是否为平台模型（保持静止）
                    renderObject.animationParams.isPlatform = (modelName.find("platform") != std::string::npos);
                    if (renderObject.animationParams.isPlatform) {
                        renderObject.animationParams.enableAnimation = false; // 平台不启用动画
                    }
                    // LOG_DEBUG("Applied default animation params for model '{}'", modelName);
                }
                
                bool success = m_render_resource->createRenderObjectResource(renderObject, adjustedObjPath, textureFiles);
                if (success) {
                    m_render_resource->addRenderObject(renderObject);
                    loadedModels++;
                    LOG_INFO("✓ Successfully loaded model '{}' with {} vertices{}", 
                            modelName, renderObject.vertices.size(),
                            renderObject.animationParams.isPlatform ? " (Platform - Static)" : "");
                } else {
                    LOG_ERROR("✗ Failed to create render object for model '{}'", modelName);
                    failedModels++;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("✗ Exception while loading model '{}': {}", modelName, e.what());
                failedModels++;
            }
        }
        
        LOG_INFO("Model loading summary: {} loaded, {} failed", loadedModels, failedModels);
        LOG_INFO("Total render objects in resource manager: {}", m_render_resource->getRenderObjectCount());
        
        if (loadedModels > 0) {
            LOG_INFO("✓ Model loading completed successfully!");
            
            // 在模型加载完成后创建光线追踪资源
            if (m_rhi->isRayTracingSupported()) {
                LOG_INFO("[RenderSystem] Creating ray tracing pipeline resources after model loading");
                if (!m_render_resource->createRayTracingPipelineResource()) {
                    LOG_ERROR("[RenderSystem] Failed to create ray tracing pipeline resources");
                } else {
                    LOG_INFO("[RenderSystem] Ray tracing pipeline resources created successfully");
                }
                
                LOG_INFO("[RenderSystem] Creating ray tracing acceleration structures");
                if (!m_render_resource->createRayTracingResource()) {
                    LOG_ERROR("[RenderSystem] Failed to create ray tracing resources");
                } else {
                    LOG_INFO("[RenderSystem] Ray tracing resources created successfully! 🚀");
                }
            }
        } else {
            LOG_ERROR("✗ No models were loaded successfully!");
        }
        
    }
    bool RenderSystem::readFileToString(const std::string& file_path, std::string& content) {
        LOG_INFO("[JSON_LOADER] Attempting to read file: {}", file_path);
        try {
            std::ifstream file(file_path, std::ios::in | std::ios::binary);
            if (!file.is_open()) {
                LOG_ERROR("[JSON_LOADER] Failed to open file: {}", file_path);
                return false;
            }
            LOG_INFO("[JSON_LOADER] File opened successfully: {}", file_path);

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
                                           std::unordered_map<std::string, std::vector<std::string>>& model_texture_map,
                                           std::unordered_map<std::string, ModelAnimationParams>& model_animation_params) {
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
            // LOG_DEBUG("Loaded model path: {} -> {}", entity_name, model_path);

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
                // LOG_DEBUG("Loaded texture map for '{}': {} textures", entity_name, textures.size());
            } else {
                LOG_WARN("Missing or invalid texture map for entity '{}'", entity_name);
            }

            // 解析动画参数（可选）
            auto animation_params_json = entity["animation_params"];
            ModelAnimationParams animParams;
            if (animation_params_json.is_object()) {
                if (animParams.fromJson(animation_params_json)) {
                    model_animation_params[entity_name] = animParams;
                    // LOG_DEBUG("Loaded animation params for '{}'", entity_name);
                } else {
                    LOG_WARN("Failed to parse animation params for entity '{}'", entity_name);
                }
            } else {
                // 使用默认动画参数
                model_animation_params[entity_name] = animParams;
            }
        }

        // 验证加载的数据
        if (model_paths.empty()) {
            LOG_ERROR("No valid model paths found in JSON file: {}", json_file_path);
            return false;
        }

        // LOG_DEBUG("Successfully loaded {} model paths, {} texture maps, and {} animation params from JSON", 
        //         model_paths.size(), model_texture_map.size(), model_animation_params.size());
        return true;
    }

    std::shared_ptr<RenderCamera> RenderSystem::getRenderCamera() const { return m_render_camera; }

    std::shared_ptr<RHI>          RenderSystem::getRHI() const { return m_rhi; }

    std::shared_ptr<RenderPipelineBase> RenderSystem::getRenderPipeline() const { return m_render_pipeline; }

}