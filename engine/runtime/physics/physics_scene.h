/**
 * @file physics_scene.h
 * @brief 物理场景管理器 - 基于 Jolt Physics 实现
 * 
 * 提供物理模拟、射线检测和碰撞检测功能
 */
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRenderer.h>
#include "jolt_debug_renderer.h"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Elish
{
    constexpr uint32_t INVALID_BODY_ID = 0xFFFFFFFF;

    /**
     * @brief 射线检测结果
     */
    struct RayCastResult
    {
        bool            m_hit = false;
        JPH::BodyID     m_body_id;
        glm::vec3       m_hit_position = glm::vec3(0.0f);
        glm::vec3       m_hit_normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float           m_hit_distance = 0.0f;
        std::string     m_body_name;
        void*           m_user_data = nullptr;
    };

    /**
     * @brief 刚体创建信息
     */
    struct RigidBodyCreateInfo
    {
        std::string             m_name;
        glm::vec3               m_position = glm::vec3(0.0f);
        glm::quat               m_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3               m_half_extent = glm::vec3(1.0f);
        glm::vec3               m_scale = glm::vec3(1.0f);
        bool                    m_is_dynamic = false;
        void*                   m_user_data = nullptr;
        std::vector<glm::vec3>  m_vertices;
    };

    /**
     * @brief 物理场景类 - 封装 Jolt Physics 系统
     */
    class PhysicsScene
    {
    public:
        PhysicsScene();
        ~PhysicsScene();

        /**
         * @brief 初始化物理系统
         */
        bool initialize();

        /**
         * @brief 清理物理系统
         */
        void clear();

        /**
         * @brief 更新物理模拟
         */
        void update(float delta_time);

        /**
         * @brief 创建盒形刚体
         */
        JPH::BodyID createBoxBody(const RigidBodyCreateInfo& create_info);

        /**
         * @brief 移除刚体
         */
        void removeBody(JPH::BodyID body_id);

        /**
         * @brief 设置刚体位置
         */
        void setBodyPosition(JPH::BodyID body_id, const glm::vec3& position);

        /**
         * @brief 设置刚体旋转
         */
        void setBodyRotation(JPH::BodyID body_id, const glm::quat& rotation);

        /**
         * @brief 设置刚体缩放（需要重建形状）
         */
        void setBodyScale(JPH::BodyID body_id, const glm::vec3& scale);

        /**
         * @brief 设置刚体变换（位置、旋转、缩放）
         */
        void setBodyTransform(JPH::BodyID body_id, const glm::vec3& position, 
                              const glm::quat& rotation, const glm::vec3& scale);

        /**
         * @brief 更新刚体的顶点（重建凸包形状）
         */
        void updateBodyVertices(JPH::BodyID body_id, const std::vector<glm::vec3>& vertices);

        /**
         * @brief 获取刚体位置
         */
        glm::vec3 getBodyPosition(JPH::BodyID body_id) const;

        /**
         * @brief 获取刚体旋转
         */
        glm::quat getBodyRotation(JPH::BodyID body_id) const;

        /**
         * @brief 射线检测
         */
        bool raycast(const glm::vec3& ray_origin,
                     const glm::vec3& ray_direction,
                     float ray_length,
                     RayCastResult& out_result);

        /**
         * @brief 射线检测（返回所有碰撞）
         */
        bool raycastAll(const glm::vec3& ray_origin,
                        const glm::vec3& ray_direction,
                        float ray_length,
                        std::vector<RayCastResult>& out_results);

        /**
         * @brief 获取 Jolt 物理系统
         */
        JPH::PhysicsSystem* getPhysicsSystem() { return m_physics_system; }

        /**
         * @brief 获取刚体数量
         */
        size_t getBodyCount() const { return m_body_names.size(); }

        /**
         * @brief 设置刚体名称
         */
        void setBodyName(JPH::BodyID body_id, const std::string& name);

        /**
         * @brief 获取刚体名称
         */
        std::string getBodyName(JPH::BodyID body_id) const;

        /**
         * @brief 根据名称获取刚体ID
         * @param name 刚体名称
         * @return 刚体ID，如果未找到返回无效ID
         */
        JPH::BodyID getBodyIDByName(const std::string& name) const;

        /**
         * @brief 设置调试绘制回调
         */
        using DebugDrawCallback = std::function<void(const glm::vec3&, const glm::vec3&, const glm::vec3&)>;
        void setDebugDrawCallback(DebugDrawCallback callback) { m_debug_draw_callback = callback; }

        /**
         * @brief 绘制调试信息
         */
        void drawDebug();

#ifdef JPH_DEBUG_RENDERER
        /**
         * @brief 获取 Jolt 调试渲染器
         */
        JoltDebugRenderer* getDebugRenderer() { return m_debug_renderer.get(); }

        /**
         * @brief 绘制物理体线框（使用 Jolt 内置功能）
         * @param view_matrix 视图矩阵
         * @param proj_matrix 投影矩阵
         * @param viewport_width 视口宽度
         * @param viewport_height 视口高度
         */
        void drawPhysicsBodies(const glm::mat4& view_matrix, const glm::mat4& proj_matrix,
                               float viewport_width, float viewport_height);

        /**
         * @brief 获取调试线段数量
         */
        size_t getDebugLineCount() const;
#endif

    private:
        /**
         * @brief 初始化 Jolt Physics
         */
        bool initializeJolt();

        /**
         * @brief GLM 转 Jolt 向量
         */
        static JPH::Vec3 toJolt(const glm::vec3& v);

        /**
         * @brief Jolt 转 GLM 向量
         */
        static glm::vec3 toGLM(const JPH::Vec3& v);

        /**
         * @brief GLM 转 Jolt 四元数
         */
        static JPH::Quat toJolt(const glm::quat& q);

        /**
         * @brief Jolt 转 GLM 四元数
         */
        static glm::quat toGLM(const JPH::Quat& q);

    private:
        bool                                m_initialized = false;
        
        JPH::TempAllocatorImpl*             m_temp_allocator = nullptr;
        JPH::JobSystemThreadPool*           m_job_system = nullptr;
        JPH::PhysicsSystem*                 m_physics_system = nullptr;
        JPH::BroadPhaseLayerInterface*      m_broad_phase_layer_interface = nullptr;
        JPH::ObjectLayerPairFilter*         m_object_layer_pair_filter = nullptr;
        JPH::ObjectVsBroadPhaseLayerFilter* m_object_vs_broad_phase_layer_filter = nullptr;

        std::unordered_map<uint32_t, std::string> m_body_names;
        std::unordered_map<uint32_t, void*>       m_body_user_data;

        DebugDrawCallback                   m_debug_draw_callback;

#ifdef JPH_DEBUG_RENDERER
        std::unique_ptr<JoltDebugRenderer>  m_debug_renderer;
#endif
    };
}
