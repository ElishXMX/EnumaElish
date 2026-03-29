/**
 * @file picking_system.h
 * @brief 拾取系统头文件 - 基于 Jolt Physics 实现
 * 
 * 实现从屏幕坐标拾取3D物体的功能
 */
#pragma once

#include "../physics/physics_scene.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>

namespace Elish
{
    class RenderCamera;

    /**
     * @brief 拾取系统类
     * 
     * 负责将屏幕坐标转换为世界空间射线并执行射线检测
     */
    class PickingSystem
    {
    public:
        PickingSystem();
        ~PickingSystem();

        /**
         * @brief 初始化拾取系统
         */
        bool initialize(PhysicsScene* physics_scene);

        /**
         * @brief 清理资源
         */
        void clear();

        /**
         * @brief 从屏幕坐标执行拾取
         */
        bool pickFromScreen(float screen_x, float screen_y,
                           float viewport_width, float viewport_height,
                           const RenderCamera& camera);

        /**
         * @brief 设置拾取回调函数
         */
        void setPickCallback(std::function<void(const RayCastResult&)> callback)
        {
            m_pick_callback = callback;
        }

        /**
         * @brief 获取最后一次拾取结果
         */
        const RayCastResult& getLastHit() const { return m_last_hit; }

        /**
         * @brief 检查是否有有效的拾取
         */
        bool hasValidPick() const { return m_has_valid_pick; }

        /**
         * @brief 设置最大射线长度
         */
        void setMaxRayLength(float length) { m_max_ray_length = length; }

    private:
        /**
         * @brief 将屏幕坐标转换为世界空间射线
         */
        void calculateWorldRay(float screen_x, float screen_y,
                              float viewport_width, float viewport_height,
                              const RenderCamera& camera,
                              glm::vec3& out_ray_origin,
                              glm::vec3& out_ray_direction);

    private:
        PhysicsScene*                           m_physics_scene = nullptr;
        bool                                    m_has_valid_pick = false;
        RayCastResult                           m_last_hit;
        float                                   m_max_ray_length = 1000.0f;
        glm::vec3                               m_last_ray_origin;
        glm::vec3                               m_last_ray_direction;
        std::function<void(const RayCastResult&)> m_pick_callback;
    };
}
