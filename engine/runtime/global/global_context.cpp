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
        m_logger_system = std::make_shared<LogSystem>();

        
        m_window_system = std::make_shared<WindowSystem>();
        WindowCreateInfo window_create_info;
        m_window_system->initialize(window_create_info);
        
        m_input_system = std::make_shared<InputSystem>();
        m_input_system->initialize();


        m_render_system = std::make_shared<RenderSystem>();
        RenderSystemInitInfo render_init_info;
        render_init_info.window_system = m_window_system;
        m_render_system->initialize(render_init_info);



    }

    void RuntimeGlobalContext::shutdownSystems()
    {
        std::cout<<"shutdownSystems"<<std::endl;

        // m_render_system.reset();

        // m_window_system.reset();

       
        // m_logger_system.reset();

       
    }
}