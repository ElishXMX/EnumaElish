#pragma once
#include "window_system.h"
#include "interface/rhi.h"
#include "../../3rdparty/json11/json11.hpp"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Elish
{
    class WindowSystem;
    class RHI;
     class RenderCamera;
    class RenderPipelineBase;
    class RenderResource;

    
    struct RenderSystemInitInfo
    {
        std::shared_ptr<WindowSystem> window_system;
    };

    class RenderSystem
    {
    
    public:
         virtual ~RenderSystem() = default;
         void tick(float delta_time) ;
         void initialize(RenderSystemInitInfo init_info)  ;

         std::shared_ptr<RenderCamera> getRenderCamera() const;
         std::shared_ptr<RHI>          getRHI() const;
         std::shared_ptr<RenderPipelineBase> getRenderPipeline() const;
         
    private:
         /**
          * @brief Load content resources from the content directory
          * This function scans the content/models and content/textures directories
          * and loads OBJ models with their corresponding texture files
          */
         /**
          * @brief Load content resources from the content directory based on provided paths.
          * @param model_paths A map where keys are model names and values are their OBJ file paths.
          * @param model_texture_map A map where keys are model names and values are lists of their texture file paths.
          */
         void loadContentResources(const std::unordered_map<std::string, std::string>& model_paths,
                                   const std::unordered_map<std::string, std::vector<std::string>>& model_texture_map);

         /**
          * @brief 从JSON文件加载资源配置
          * @param json_file_path JSON配置文件的路径
          * @param model_paths 输出参数：模型名称到文件路径的映射
          * @param model_texture_map 输出参数：模型名称到纹理文件路径列表的映射
          * @return 成功返回true，失败返回false
          */
         bool loadResourcesFromJson(const std::string& json_file_path,
                                   std::unordered_map<std::string, std::string>& model_paths,
                                   std::unordered_map<std::string, std::vector<std::string>>& model_texture_map);

         /**
          * @brief 读取文件内容到字符串
          * @param file_path 文件路径
          * @param content 输出参数：文件内容
          * @return 成功返回true，失败返回false
          */
         bool readFileToString(const std::string& file_path, std::string& content);
        
       
    private:
        std::shared_ptr<RenderResource> m_render_resource;
        std::shared_ptr<RHI>                m_rhi;
        std::shared_ptr<RenderPipelineBase> m_render_pipeline;
        std::shared_ptr<RenderCamera>       m_render_camera;
        
        
    };
}