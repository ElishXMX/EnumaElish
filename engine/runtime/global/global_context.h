#pragma once

#include <memory>
#include <string>

namespace Elish
{
    class LogSystem;
    class InputSystem;
    class RenderSystem;
    class WindowSystem;
    class PhysicsScene;
    class PickingSystem;
    struct RayCastResult;

    /// Manage the lifetime and creation/destruction order of all global system
    class RuntimeGlobalContext
    {
    public:
        // create all global systems and initialize these systems
        void startSystems();
        // destroy all global systems
        void shutdownSystems();
        
        /**
         * @brief 每帧同步碰撞体位置到渲染对象
         * @details 确保碰撞体与渲染对象的位置、旋转、缩放完全同步
         */
        void syncCollidersWithRenderObjects();

    private:
        // 为渲染对象创建碰撞体
        void createCollidersForRenderObjects();
        
        // 设置拾取回调
        void setupPickingCallback();
        
        // 拾取结果回调处理
        void onPickingResult(const RayCastResult& result);

    public:
        std::shared_ptr<LogSystem>         m_logger_system;
        std::shared_ptr<InputSystem>       m_input_system;
        std::shared_ptr<WindowSystem>      m_window_system;
        std::shared_ptr<RenderSystem>      m_render_system;
        std::shared_ptr<PhysicsScene>      m_physics_scene;
        std::shared_ptr<PickingSystem>     m_picking_system;
    };
    extern RuntimeGlobalContext g_runtime_global_context;//全局变量
} // namespace Elish