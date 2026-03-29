/**
 * @file global_context.cpp
 * @brief 全局上下文实现
 */
#include "global_context.h"
#include "../core/base/macro.h"
#include "../render/window_system.h"
#include "../core/log/log_system.h"
#include "../render/render_system.h"
#include "../render/render_resource.h"
#include "../render/render_pipeline.h"
#include "../input/input_system.h"
#include "../physics/physics_scene.h"
#include "../picking/picking_system.h"
#include <iostream>
#include <cfloat>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>

namespace Elish
{
    RuntimeGlobalContext g_runtime_global_context;

    void RuntimeGlobalContext::startSystems()
    {
        std::cout << "[GLOBAL_CONTEXT] Starting systems..." << std::endl;
        
        m_logger_system = std::make_shared<LogSystem>();
        std::cout << "[GLOBAL_CONTEXT] LogSystem created" << std::endl;

        m_window_system = std::make_shared<WindowSystem>();
        std::cout << "[GLOBAL_CONTEXT] WindowSystem created" << std::endl;
        WindowCreateInfo window_create_info;
        m_window_system->initialize(window_create_info);
        std::cout << "[GLOBAL_CONTEXT] WindowSystem initialized" << std::endl;
        
        m_input_system = std::make_shared<InputSystem>();
        std::cout << "[GLOBAL_CONTEXT] InputSystem created" << std::endl;
        m_input_system->initialize();
        std::cout << "[GLOBAL_CONTEXT] InputSystem initialized" << std::endl;

        m_render_system = std::make_shared<RenderSystem>();
        std::cout << "[GLOBAL_CONTEXT] RenderSystem created" << std::endl;
        RenderSystemInitInfo render_init_info;
        render_init_info.window_system = m_window_system;
        m_render_system->initialize(render_init_info);
        std::cout << "[GLOBAL_CONTEXT] RenderSystem initialized" << std::endl;

        m_physics_scene = std::make_shared<PhysicsScene>();
        std::cout << "[GLOBAL_CONTEXT] PhysicsScene created" << std::endl;
        m_physics_scene->initialize();
        std::cout << "[GLOBAL_CONTEXT] PhysicsScene initialized" << std::endl;

        m_picking_system = std::make_shared<PickingSystem>();
        std::cout << "[GLOBAL_CONTEXT] PickingSystem created" << std::endl;
        m_picking_system->initialize(m_physics_scene.get());
        std::cout << "[GLOBAL_CONTEXT] PickingSystem initialized" << std::endl;

        setupPickingCallback();

        createCollidersForRenderObjects();
    }

    void RuntimeGlobalContext::setupPickingCallback()
    {
        if (!m_picking_system)
        {
            LOG_ERROR("[Picking] Cannot setup callback: picking_system is null");
            return;
        }

        auto callback = [this](const RayCastResult& result) {
            this->onPickingResult(result);
        };
        m_picking_system->setPickCallback(callback);
        LOG_INFO("[Picking] Pick callback setup successfully");
    }

    void RuntimeGlobalContext::onPickingResult(const RayCastResult& result)
    {
        if (!result.m_hit)
        {
            return;
        }

        LOG_INFO("[Picking] Object picked: {} (ID: {})", 
                 result.m_body_name, result.m_body_id.GetIndexAndSequenceNumber());

        if (!m_render_system)
        {
            LOG_ERROR("[Picking] Cannot update selection: render_system is null");
            return;
        }

        auto pipeline = std::dynamic_pointer_cast<RenderPipeline>(m_render_system->getRenderPipeline());
        if (!pipeline)
        {
            LOG_ERROR("[Picking] Cannot update selection: pipeline is null or wrong type");
            return;
        }

        auto render_resource = m_render_system->getRenderResource();
        if (!render_resource)
        {
            LOG_ERROR("[Picking] Cannot update selection: render_resource is null");
            return;
        }

        const auto& render_objects = render_resource->getLoadedRenderObjects();
        int found_index = -1;

        for (size_t i = 0; i < render_objects.size(); ++i)
        {
            if (render_objects[i].name == result.m_body_name)
            {
                found_index = static_cast<int>(i);
                break;
            }
        }

        auto& layout_state = pipeline->getEditorLayoutState();
        layout_state.selectedObjectIndex = found_index;

        if (found_index >= 0)
        {
            LOG_INFO("[Picking] UI selection updated: index = {}", found_index);
        }
        else
        {
            LOG_WARN("[Picking] Object '{}' not found in render objects list", result.m_body_name);
        }
    }

    void RuntimeGlobalContext::createCollidersForRenderObjects()
    {
        if (!m_render_system || !m_physics_scene)
        {
            LOG_ERROR("[Picking] Cannot create colliders: render_system or physics_scene is null");
            return;
        }

        auto render_resource = m_render_system->getRenderResource();
        if (!render_resource)
        {
            LOG_ERROR("[Picking] Cannot create colliders: render_resource is null");
            return;
        }

        const auto& render_objects = render_resource->getLoadedRenderObjects();
        LOG_INFO("[Picking] Creating colliders for {} render objects", render_objects.size());

        size_t collider_count = 0;
        
        for (size_t i = 0; i < render_objects.size(); ++i)
        {
            const auto& render_obj = render_objects[i];
            const auto& anim_params = render_obj.animationParams;
            
            glm::vec3 min_bounds(FLT_MAX);
            glm::vec3 max_bounds(-FLT_MAX);
            glm::vec3 center(0.0f);
            
            if (!render_obj.vertices.empty())
            {
                for (const auto& vertex : render_obj.vertices)
                {
                    min_bounds.x = std::min(min_bounds.x, vertex.pos.x);
                    min_bounds.y = std::min(min_bounds.y, vertex.pos.y);
                    min_bounds.z = std::min(min_bounds.z, vertex.pos.z);
                    max_bounds.x = std::max(max_bounds.x, vertex.pos.x);
                    max_bounds.y = std::max(max_bounds.y, vertex.pos.y);
                    max_bounds.z = std::max(max_bounds.z, vertex.pos.z);
                }
                center = (min_bounds + max_bounds) * 0.5f;
            }
            
            glm::quat rotation_quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            rotation_quat = glm::rotate(rotation_quat, anim_params.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
            rotation_quat = glm::rotate(rotation_quat, anim_params.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            rotation_quat = glm::rotate(rotation_quat, anim_params.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

            glm::vec3 actual_position = anim_params.position + center;
            
            RigidBodyCreateInfo create_info;
            create_info.m_name = render_obj.name.empty() 
                ? "RenderObject_" + std::to_string(i) 
                : render_obj.name;
            create_info.m_position = actual_position;
            create_info.m_rotation = rotation_quat;
            create_info.m_scale = anim_params.scale;
            create_info.m_is_dynamic = false;
            
            if (!render_obj.vertices.empty())
            {
                for (const auto& vertex : render_obj.vertices)
                {
                    glm::vec3 centered_pos = vertex.pos - center;
                    create_info.m_vertices.push_back(centered_pos);
                }
            }
            
            glm::vec3 half_extent = (max_bounds - min_bounds) * 0.5f;
            create_info.m_half_extent = glm::max(half_extent, glm::vec3(0.1f));

            JPH::BodyID body_id = m_physics_scene->createBoxBody(create_info);

            if (!body_id.IsInvalid())
            {
                collider_count++;
                LOG_INFO("[Picking] Created collider for '{}' (ID: {})", 
                         create_info.m_name, body_id.GetIndexAndSequenceNumber());
                LOG_INFO("  anim_params.position: ({:.3f}, {:.3f}, {:.3f})", 
                         anim_params.position.x, anim_params.position.y, anim_params.position.z);
                LOG_INFO("  vertex center: ({:.3f}, {:.3f}, {:.3f})", 
                         center.x, center.y, center.z);
                LOG_INFO("  actual_position: ({:.3f}, {:.3f}, {:.3f})", 
                         actual_position.x, actual_position.y, actual_position.z);
                LOG_INFO("  Scale: ({:.3f}, {:.3f}, {:.3f})", 
                         anim_params.scale.x, anim_params.scale.y, anim_params.scale.z);
                LOG_INFO("  Vertices: {}", create_info.m_vertices.size());
            }
            else
            {
                LOG_ERROR("[Picking] Failed to create collider for '{}'", create_info.m_name);
            }
        }

        LOG_INFO("[Picking] Collider creation complete: {} colliders created", collider_count);
    }

    void RuntimeGlobalContext::syncCollidersWithRenderObjects()
    {
        if (!m_physics_scene || !m_render_system)
        {
            return;
        }

        auto render_resource = m_render_system->getRenderResource();
        if (!render_resource)
        {
            return;
        }

        const auto& render_objects = render_resource->getLoadedRenderObjects();

        for (const auto& render_obj : render_objects)
        {
            if (render_obj.name.empty())
            {
                continue;
            }

            JPH::BodyID body_id = m_physics_scene->getBodyIDByName(render_obj.name);
            if (body_id.IsInvalid())
            {
                continue;
            }

            const auto& anim_params = render_obj.animationParams;

            glm::mat4 model_matrix = glm::mat4(1.0f);
            model_matrix = glm::translate(model_matrix, anim_params.position);
            
            const float currentTime = static_cast<float>(glfwGetTime());
            if (anim_params.enableAnimation && !anim_params.isPlatform)
            {
                float rotationAngle = currentTime * anim_params.rotationSpeed;
                model_matrix = glm::rotate(model_matrix, rotationAngle, anim_params.rotationAxis);
            }
            
            model_matrix = glm::rotate(model_matrix, anim_params.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
            model_matrix = glm::rotate(model_matrix, anim_params.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
            model_matrix = glm::rotate(model_matrix, anim_params.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
            
            model_matrix = glm::scale(model_matrix, anim_params.scale);

            glm::quat rotation_quat = glm::quat_cast(model_matrix);
            rotation_quat = glm::normalize(rotation_quat);
            glm::vec3 position = glm::vec3(model_matrix[3]);

            glm::vec3 body_pos = m_physics_scene->getBodyPosition(body_id);
            glm::quat body_rot = m_physics_scene->getBodyRotation(body_id);

            // static int log_counter = 0;
            // if (log_counter++ % 60 == 0)
            // {
            //     LOG_INFO("[Sync] Object '{}':", render_obj.name);
            //     LOG_INFO("  anim_params.position: ({:.3f},{:.3f},{:.3f})", 
            //              anim_params.position.x, anim_params.position.y, anim_params.position.z);
            //     LOG_INFO("  anim_params.scale: ({:.3f},{:.3f},{:.3f})", 
            //              anim_params.scale.x, anim_params.scale.y, anim_params.scale.z);
            //     LOG_INFO("  anim_params.rotation: ({:.3f},{:.3f},{:.3f})", 
            //              anim_params.rotation.x, anim_params.rotation.y, anim_params.rotation.z);
            //     
            //     if (!render_obj.vertices.empty())
            //     {
            //         glm::vec3 min_v(FLT_MAX), max_v(-FLT_MAX);
            //         for (const auto& v : render_obj.vertices)
            //         {
            //             min_v = glm::min(min_v, v.pos);
            //             max_v = glm::max(max_v, v.pos);
            //         }
            //         LOG_INFO("  vertex bounds: min=({:.3f},{:.3f},{:.3f}) max=({:.3f},{:.3f},{:.3f})", 
            //                  min_v.x, min_v.y, min_v.z, max_v.x, max_v.y, max_v.z);
            //     }
            //     
            //     LOG_INFO("  model_matrix[3]: ({:.3f},{:.3f},{:.3f})", 
            //              model_matrix[3][0], model_matrix[3][1], model_matrix[3][2]);
            //     LOG_INFO("  extracted position: ({:.3f},{:.3f},{:.3f})", 
            //              position.x, position.y, position.z);
            //     LOG_INFO("  body_pos: ({:.3f},{:.3f},{:.3f})", 
            //              body_pos.x, body_pos.y, body_pos.z);
            // }

            m_physics_scene->setBodyPosition(body_id, position);
            m_physics_scene->setBodyRotation(body_id, rotation_quat);
        }
    }

    void RuntimeGlobalContext::shutdownSystems()
    {
        std::cout << "[GLOBAL_CONTEXT] shutdownSystems" << std::endl;

        m_picking_system.reset();
        m_physics_scene.reset();
        m_render_system.reset();
        m_input_system.reset();
        m_window_system.reset();
        m_logger_system.reset();
    }
}
