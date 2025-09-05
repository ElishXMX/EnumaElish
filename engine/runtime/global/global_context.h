#pragma once

#include <memory>
#include <string>

namespace Elish
{
    class LogSystem;
    class InputSystem;
    class RenderSystem;
    class WindowSystem;
   
    /// Manage the lifetime and creation/destruction order of all global system
    class RuntimeGlobalContext
    {
    public:
        // create all global systems and initialize these systems
        void startSystems();
        // destroy all global systems
        void shutdownSystems();

    public:
        std::shared_ptr<LogSystem>         m_logger_system;
        std::shared_ptr<InputSystem>       m_input_system;
        std::shared_ptr<WindowSystem>      m_window_system;
        std::shared_ptr<RenderSystem>      m_render_system;
    };

    extern RuntimeGlobalContext g_runtime_global_context;//全局变量
} // namespace Elish