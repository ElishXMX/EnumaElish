#include "global_context.h"
#include "../core/base/macro.h"
#include "../render/window_system.h"
#include "../core/log/log_system.h"
#include "../render/render_system.h"
#include "../input/input_system.h"
#include "iostream"

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



    }

    void RuntimeGlobalContext::shutdownSystems()
    {
        std::cout<<"shutdownSystems"<<std::endl;

        // m_render_system.reset();

        // m_window_system.reset();

       
        // m_logger_system.reset();

       
    }
}