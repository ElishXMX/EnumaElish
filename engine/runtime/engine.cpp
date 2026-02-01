#include "engine.h"
#include "render/render_system.h"
#include "render/window_system.h"
#include "input/input_system.h"

#include "global/global_context.h"
#include <Windows.h>
#include <synchapi.h>


namespace Elish
{
    void Engine::initialize()
    {
        g_runtime_global_context.startSystems();//开启各个系统

    }

    void Engine::run()
    {
        std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;
        while (!window_system->shouldClose())
        {
            const float delta_time = calculateDeltaTime();//计算下一帧
            // Sleep(1000 / 60); //使用Windows API的Sleep函数来控制帧率 - 修复：应该是1000/60而不是10000/60
            tickOneFrame(delta_time);//窗口启动后继续
     }
    }
    float Engine::calculateDeltaTime()
    {
        float delta_time;
        {
            using namespace std::chrono;

            steady_clock::time_point tick_time_point = steady_clock::now();
            duration<float> time_span = duration_cast<duration<float>>(tick_time_point - m_last_tick_time_point);
            delta_time                = time_span.count();

            m_last_tick_time_point = tick_time_point;
        }
        return delta_time;
    }

    bool Engine::tickOneFrame(float delta_time)
    {
        // LOG_DEBUG("[Engine] Starting tickOneFrame");
        logicalTick(delta_time);//逻辑更新
        // LOG_DEBUG("[Engine] logicalTick completed");
        calculateFPS(delta_time);//计算fps
        // LOG_DEBUG("[Engine] calculateFPS completed");

        // single thread
        // exchange data between logic and render contexts
        // g_runtime_global_context.m_render_system->swapLogicRenderData();//交换数据

        // 检查窗口系统是否有效
        if (!g_runtime_global_context.m_window_system) {
            LOG_FATAL("[Engine] Window system is null in tickOneFrame!");
            return false;
        }
        
        g_runtime_global_context.m_window_system->pollEvents();
        // LOG_DEBUG("[Engine] pollEvents completed");
        
        // 检查渲染系统是否有效
        if (!g_runtime_global_context.m_render_system) {
            LOG_FATAL("[Engine] Render system is null in tickOneFrame!");
            return false;
        }
        
        rendererTick(delta_time);//渲染更新
        // LOG_DEBUG("[Engine] rendererTick completed");


        g_runtime_global_context.m_window_system->setTitle(
            std::string("EummaElish Engine - " + std::to_string(getFPS()) + " FPS").c_str());
        // LOG_DEBUG("[Engine] setTitle completed");

        const bool should_window_close = g_runtime_global_context.m_window_system->shouldClose();
        // LOG_DEBUG("[Engine] tickOneFrame completed");
        return !should_window_close;
    }

    
    void Engine::logicalTick(float delta_time)
    {
        // LOG_DEBUG("[Engine] Starting logicalTick");
        
        // 检查输入系统是否有效
        if (!g_runtime_global_context.m_input_system) {
            LOG_FATAL("[Engine] Input system is null in logicalTick!");
            return;
        }
        
        g_runtime_global_context.m_input_system->tick();
        // LOG_DEBUG("[Engine] logicalTick input system tick completed");
     }

    bool Engine::rendererTick(float delta_time)
    {
        // LOG_DEBUG("[Engine] Starting rendererTick");
        g_runtime_global_context.m_render_system->tick(delta_time);
        // LOG_DEBUG("[Engine] rendererTick completed");
        return true;
    }

    const float Engine::s_fps_alpha = 1.f / 100;
    void Engine::calculateFPS(float delta_time)
    {
        m_frame_count++;

        if (m_frame_count == 1)
        {
            m_average_duration = delta_time;
        }
        else
        {
            m_average_duration = m_average_duration * (1 - s_fps_alpha) + delta_time * s_fps_alpha;
        }

        m_fps = static_cast<int>(1.f / m_average_duration);
    }//计算FPS

}