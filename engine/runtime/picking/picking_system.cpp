/**
 * @file picking_system.cpp
 * @brief 拾取系统实现 - 基于 Jolt Physics
 * 
 * 核心流程：
 * 1. 将屏幕坐标转换为标准化设备坐标(NDC)
 * 2. 通过逆投影矩阵将NDC转换到观察空间
 * 3. 通过逆视图矩阵将观察空间转换到世界空间
 * 4. 从相机位置发射射线进行碰撞检测
 */
#include "picking_system.h"
#include "../physics/physics_scene.h"
#include "../render/render_camera.h"
#include "../core/base/macro.h"
#include <glm/gtc/matrix_inverse.hpp>

namespace Elish
{
    PickingSystem::PickingSystem()
    {
    }

    PickingSystem::~PickingSystem()
    {
        clear();
    }

    bool PickingSystem::initialize(PhysicsScene* physics_scene)
    {
        if (!physics_scene)
        {
            LOG_ERROR("[PickingSystem] physics_scene is null");
            return false;
        }

        m_physics_scene = physics_scene;
        m_has_valid_pick = false;

        LOG_INFO("[PickingSystem] Initialized successfully");
        return true;
    }

    void PickingSystem::clear()
    {
        m_physics_scene = nullptr;
        m_has_valid_pick = false;
        m_pick_callback = nullptr;
    }

    /**
     * @brief 将屏幕坐标转换为世界空间射线
     * 
     * 坐标转换流程：
     * 屏幕坐标 -> NDC坐标 -> 裁剪空间 -> 观察空间 -> 世界空间
     */
    void PickingSystem::calculateWorldRay(float screen_x, float screen_y,
                                          float viewport_width, float viewport_height,
                                          const RenderCamera& camera,
                                          glm::vec3& out_ray_origin,
                                          glm::vec3& out_ray_direction)
    {
        float ndc_x = (2.0f * screen_x) / viewport_width - 1.0f;
        float ndc_y = 1.0f - (2.0f * screen_y) / viewport_height;

        glm::vec4 clip_coords(ndc_x, ndc_y, -1.0f, 1.0f);

        glm::mat4 inv_projection = glm::inverse(camera.getPersProjMatrix());
        glm::vec4 eye_coords = inv_projection * clip_coords;
        
        eye_coords = glm::vec4(eye_coords.x, eye_coords.y, -1.0f, 0.0f);

        glm::mat4 inv_view = glm::inverse(camera.getViewMatrix());
        glm::vec4 world_direction = inv_view * eye_coords;

        out_ray_origin = camera.position();
        out_ray_direction = glm::normalize(glm::vec3(world_direction));
    }

    /**
     * @brief 从屏幕坐标执行拾取操作
     */
    bool PickingSystem::pickFromScreen(float screen_x, float screen_y,
                                        float viewport_width, float viewport_height,
                                        const RenderCamera& camera)
    {
        LOG_INFO("========================================");
        LOG_INFO("[Picking] Starting ray cast");
        LOG_INFO("  Viewport coords: ({:.1f}, {:.1f})", screen_x, screen_y);
        LOG_INFO("  Viewport size: {:.1f} x {:.1f}", viewport_width, viewport_height);

        if (!m_physics_scene)
        {
            LOG_ERROR("[Picking] Error: physics_scene is null");
            LOG_INFO("========================================");
            return false;
        }

        glm::vec3 ray_origin;
        glm::vec3 ray_direction;
        calculateWorldRay(screen_x, screen_y, viewport_width, viewport_height,
                         camera, ray_origin, ray_direction);

        m_last_ray_origin = ray_origin;
        m_last_ray_direction = ray_direction;

        LOG_INFO("  Ray origin: ({:.3f}, {:.3f}, {:.3f})", 
                 ray_origin.x, ray_origin.y, ray_origin.z);
        LOG_INFO("  Ray direction: ({:.3f}, {:.3f}, {:.3f})", 
                 ray_direction.x, ray_direction.y, ray_direction.z);
        LOG_INFO("  Max distance: {:.1f}", m_max_ray_length);

        if (m_physics_scene->raycast(ray_origin, ray_direction, m_max_ray_length, m_last_hit))
        {
            m_has_valid_pick = true;

            LOG_INFO("----------------------------------------");
            LOG_INFO("[Picking] SUCCESS! Hit detected");
            LOG_INFO("  Body name: {}", m_last_hit.m_body_name);
            LOG_INFO("  Body ID: {}", m_last_hit.m_body_id.GetIndexAndSequenceNumber());
            LOG_INFO("  Hit position: ({:.3f}, {:.3f}, {:.3f})", 
                     m_last_hit.m_hit_position.x,
                     m_last_hit.m_hit_position.y,
                     m_last_hit.m_hit_position.z);
            LOG_INFO("  Hit distance: {:.3f}", m_last_hit.m_hit_distance);
            LOG_INFO("========================================");

            if (m_pick_callback)
            {
                m_pick_callback(m_last_hit);
            }

            return true;
        }
        else
        {
            LOG_INFO("[Picking] No hit detected");
            LOG_INFO("========================================");
            m_has_valid_pick = false;
            return false;
        }
    }

} // namespace Elish
