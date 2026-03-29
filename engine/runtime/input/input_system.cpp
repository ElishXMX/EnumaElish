#include "input_system.h"

#include "../core/base/macro.h"

#include "../engine.h"
#include "../global/global_context.h"
#include "../render/render_system.h"
#include "../render/window_system.h"
#include "../render/passes/ui_pass.h"
#include "../render/render_pipeline.h"
#include "../picking/picking_system.h"
#include "../physics/physics_scene.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>

namespace Elish
{
    unsigned int k_complement_control_command = 0xFFFFFFFF;

    /**
     * @brief 键盘按键事件的回调函数
     * 当键盘按键被按下、释放或重复时调用此函数
     * @param key 被按下或释放的键盘按键
     * @param scancode 按键的系统特定扫描码
     * @param action 按键的动作（GLFW_PRESS：按下，GLFW_RELEASE：释放，GLFW_REPEAT：重复）
     * @param mods 描述按下了哪些修饰键的位字段
     */
    void InputSystem::onKey(int key, int scancode, int action, int mods)
    {
        onKeyInGameMode(key, scancode, action, mods);
    }

    /**
     * @brief 专门处理游戏模式下的键盘输入
     * 此函数处理按键的按下和释放，以更新游戏命令状态
     * @param key 被按下或释放的键盘按键
     * @param scancode 按键的系统特定扫描码
     * @param action 按键的动作（GLFW_PRESS：按下，GLFW_RELEASE：释放，GLFW_REPEAT：重复）
     * @param mods 描述按下了哪些修饰键的位字段
     */
    void InputSystem::onKeyInGameMode(int key, int scancode, int action, int mods)
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::jump);
        
        if (action == GLFW_PRESS)
        {
            const char* key_name = glfwGetKeyName(key, scancode);
            
            unsigned int old_command = m_game_command;
            
            switch (key)
            {
                case GLFW_KEY_ESCAPE:
                    break;
                case GLFW_KEY_R:
                    break;
                case GLFW_KEY_A:
                    m_game_command |= (unsigned int)GameCommand::left;
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] A key pressed - LEFT movement activated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_S:
                    m_game_command |= (unsigned int)GameCommand::backward;
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] S key pressed - BACKWARD movement activated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_W:
                    m_game_command |= (unsigned int)GameCommand::forward;
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] W key pressed - FORWARD movement activated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_D:
                    m_game_command |= (unsigned int)GameCommand::right;
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] D key pressed - RIGHT movement activated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_SPACE:
                    m_game_command |= (unsigned int)GameCommand::jump;
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    m_game_command |= (unsigned int)GameCommand::squat;
                    break;
                case GLFW_KEY_LEFT_ALT: {
                    auto window_system = g_runtime_global_context.m_window_system;
                    bool old_focus = window_system->getFocusMode();
                    window_system->setFocusMode(!old_focus);
                    break;
                }
                case GLFW_KEY_LEFT_SHIFT:
                    m_game_command |= (unsigned int)GameCommand::sprint;
                    break;
                case GLFW_KEY_F:
                    break;
                default:
                    break;
            }
            
            if (old_command != m_game_command)
            {
                LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Game command updated: 0x{:X} -> 0x{:X}", 
                         timestamp, old_command, m_game_command);
            }
            else
            {
                LOG_DEBUG("[KEYBOARD_INPUT][{}ms] No command change (current: 0x{:X})", 
                         timestamp, m_game_command);
            }
        }
        else if (action == GLFW_RELEASE)
        {
            const char* key_name = glfwGetKeyName(key, scancode);
            LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Key Released: {} (code: {}, scancode: {}, mods: {})", 
                      timestamp, key_name ? key_name : "UNKNOWN", key, scancode, mods);
            
            unsigned int old_command = m_game_command;
            
            switch (key)
            {
                case GLFW_KEY_ESCAPE:
                    break;
                case GLFW_KEY_R:
                    break;
                case GLFW_KEY_W:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::forward);
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] W key released - FORWARD movement deactivated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_S:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::backward);
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] S key released - BACKWARD movement deactivated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_A:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::left);
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] A key released - LEFT movement deactivated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_D:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::right);
                    LOG_DEBUG("[KEYBOARD_INPUT][{}ms] D key released - RIGHT movement deactivated (command: 0x{:X})", timestamp, m_game_command);
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::squat);
                    break;
                case GLFW_KEY_LEFT_SHIFT:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::sprint);
                    break;
                default:
                    break;
            }
            
            if (old_command != m_game_command)
            {
                LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Game command updated: 0x{:X} -> 0x{:X}", 
                         timestamp, old_command, m_game_command);
            }
            else
            {
                LOG_DEBUG("[KEYBOARD_INPUT][{}ms] No command change on release (current: 0x{:X})", 
                         timestamp, m_game_command);
            }
        }
    }

    /**
     * @brief 鼠标按钮事件的回调函数
     * @param button 鼠标按钮（GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT等）
     * @param action 按钮动作（GLFW_PRESS或GLFW_RELEASE）
     * @param mods 修饰键状态
     */
    void InputSystem::onMouseButton(int button, int action, int mods)
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
            {
                m_mouse_left_pressed = true;
                m_is_dragging = false;
                m_drag_start_x = m_last_cursor_x;
                m_drag_start_y = m_last_cursor_y;
            }
            else if (action == GLFW_RELEASE)
            {
                // 如果不是拖动，则触发拾取
                if (!m_is_dragging)
                {
                    triggerPicking();
                }
                
                m_mouse_left_pressed = false;
                m_is_dragging = false;
            }
        }
        else if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS)
            {
                m_mouse_right_pressed = true;
            }
            else if (action == GLFW_RELEASE)
            {
                m_mouse_right_pressed = false;
            }
        }
    }
    
    /**
     * @brief 鼠标光标位置变化的回调函数
     * @param current_cursor_x 光标当前的X坐标
     * @param current_cursor_y 光标当前的Y坐标
     */
    void InputSystem::onCursorPos(double current_cursor_x, double current_cursor_y)
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        bool focus_mode = g_runtime_global_context.m_window_system->getFocusMode();
        
        if (m_mouse_left_pressed && !m_is_dragging)
        {
            double dx = current_cursor_x - m_drag_start_x;
            double dy = current_cursor_y - m_drag_start_y;
            double distance = std::sqrt(dx * dx + dy * dy);
            
            if (distance > DRAG_THRESHOLD)
            {
                m_is_dragging = true;
            }
        }
        
        if (focus_mode && m_is_dragging)
        {
            m_cursor_delta_x = m_last_cursor_x - static_cast<int>(current_cursor_x);
            m_cursor_delta_y = m_last_cursor_y - static_cast<int>(current_cursor_y);
        }
        else
        {
            m_cursor_delta_x = 0;
            m_cursor_delta_y = 0;
        }
        
        m_last_cursor_x = static_cast<int>(current_cursor_x);
        m_last_cursor_y = static_cast<int>(current_cursor_y);
    }

    /**
     * @brief 清除光标增量值
     * 此函数将光标移动的增量和对应的角度变化重置为零
     */
    void InputSystem::clear()
    {
        m_cursor_delta_x = 0;
        m_cursor_delta_y = 0;
        m_cursor_delta_yaw = 0.0f;
        m_cursor_delta_pitch = 0.0f;
    }

    /**
     * @brief 根据窗口大小和FOV计算光标的增量角度
     * 优化鼠标控制响应性，实现更直观的视角旋转
     */
    void InputSystem::calculateCursorDeltaAngles()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        std::array<int, 2> window_size = g_runtime_global_context.m_window_system->getWindowSize();

        if (window_size[0] < 1 || window_size[1] < 1)
        {
            return;
        }

        const float mouse_sensitivity_factor = 0.002f;
        
        m_cursor_delta_yaw = static_cast<float>(m_cursor_delta_x) * mouse_sensitivity_factor;
        m_cursor_delta_pitch = static_cast<float>(m_cursor_delta_y) * mouse_sensitivity_factor;
    }

    /**
     * @brief 初始化输入系统
     * 此函数负责向 WindowSystem 注册键盘、鼠标按钮和鼠标光标位置的回调
     */
    void InputSystem::initialize()
    {
        std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;

        window_system->registerOnKeyFunc(
            std::bind(&InputSystem::onKey, this,
                      std::placeholders::_1,
                      std::placeholders::_2,
                      std::placeholders::_3,
                      std::placeholders::_4));
        
        window_system->registerOnCursorPosFunc(
            std::bind(&InputSystem::onCursorPos, this, std::placeholders::_1, std::placeholders::_2));
        
        window_system->registerOnMouseButtonFunc(
            std::bind(&InputSystem::onMouseButton, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    /**
     * @brief 更新输入系统状态
     * 此函数用于更新输入状态、计算光标增量、清除光标增量以及根据窗口焦点更新游戏命令
     */
    void InputSystem::tick()
    {
        calculateCursorDeltaAngles();
        updateCameraState(0.016f);
        
        m_cursor_delta_x = 0;
        m_cursor_delta_y = 0;
        m_cursor_delta_yaw = 0.0f;
        m_cursor_delta_pitch = 0.0f;

        auto window_system = g_runtime_global_context.m_window_system;
        if (window_system->getFocusMode())
        {
            m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::invalid);
        }
        else
        {
            m_game_command |= (unsigned int)GameCommand::invalid;
        }
    }

    /**
     * @brief 更新摄像机状态
     * @param delta_time 时间增量
     */
    void InputSystem::updateCameraState(float delta_time)
    {
        auto render_system = g_runtime_global_context.m_render_system;
        if (render_system)
        {
            auto render_pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_system->getRenderPipeline());
            if (render_pipeline)
            {
                auto ui_pass = render_pipeline->getUIPass();
                if (ui_pass && ui_pass->isUIFocused())
                {
                    return;
                }
            }
        }
        
        processCameraRotation();
        processCameraMovement(delta_time);
    }
    
    /**
     * @brief 处理摄像机旋转逻辑
     * 实现平滑自由的视角旋转，支持无限制的鼠标旋转
     */
    void InputSystem::processCameraRotation()
    {
        if (m_cursor_delta_yaw != 0.0f || m_cursor_delta_pitch != 0.0f)
        {
            m_accumulated_yaw += m_cursor_delta_yaw * CAMERA_MOUSE_SENSITIVITY;
            m_accumulated_pitch += m_cursor_delta_pitch * CAMERA_MOUSE_SENSITIVITY;
            
            const float max_pitch = glm::radians(89.5f);
            m_accumulated_pitch = glm::clamp(m_accumulated_pitch, -max_pitch, max_pitch);
    
            glm::quat yaw_rotation = glm::angleAxis(m_accumulated_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat pitch_rotation = glm::angleAxis(m_accumulated_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            
            m_camera_rotation = yaw_rotation * pitch_rotation;
            
            auto now = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        }
    }
    
    /**
     * @brief 处理摄像机移动逻辑
     * @param delta_time 时间增量
     */
    void InputSystem::processCameraMovement(float delta_time)
    {
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        glm::vec3 movement(0.0f);
        float move_speed = CAMERA_MOVE_SPEED;
        
        if (m_game_command & static_cast<unsigned int>(GameCommand::sprint))
        {
            move_speed *= CAMERA_SPRINT_MULTIPLIER;
        }
        
        glm::mat3 rotation_matrix = glm::mat3_cast(m_camera_rotation);
        glm::vec3 forward = -rotation_matrix[2];
        glm::vec3 right = rotation_matrix[0];
        glm::vec3 up = rotation_matrix[1];
        
        if (m_game_command & static_cast<unsigned int>(GameCommand::forward))
        {
            movement += forward;
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::backward))
        {
            movement -= forward;
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::right))
        {
            movement += right;
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::left))
        {
            movement -= right;
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::jump))
        {
            movement += up;
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::squat))
        {
            movement -= up;
        }
        
        if (glm::length(movement) > 0.0f)
        {
            glm::vec3 old_position = m_camera_position;
            movement = glm::normalize(movement) * move_speed * delta_time;
            m_camera_position += movement;
        }
    }
    
    /**
     * @brief 获取摄像机的视图矩阵
     * @return 视图矩阵
     */
    glm::mat4 InputSystem::getCameraViewMatrix() const
    {
        glm::mat4 rotation_matrix = glm::mat4_cast(glm::conjugate(m_camera_rotation));
        glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), -m_camera_position);
        return rotation_matrix * translation_matrix;
    }

    /**
     * @brief 触发拾取操作
     * 
     * 执行流程：
     * 1. 获取场景视口的位置和尺寸
     * 2. 将窗口坐标转换为视口相对坐标
     * 3. 检查点击是否在场景视口内
     * 4. 使用视口坐标进行射线检测
     */
    void InputSystem::triggerPicking()
    {
        LOG_INFO("[Picking Trigger] Mouse click event triggers picking operation");

        auto& picking_system = g_runtime_global_context.m_picking_system;
        auto& render_system = g_runtime_global_context.m_render_system;
        auto& window_system = g_runtime_global_context.m_window_system;
        
        if (!picking_system || !render_system || !window_system)
        {
            LOG_ERROR("[Picking Trigger] Error: Systems not initialized (picking={}, render={}, window={})", 
                     (bool)picking_system, (bool)render_system, (bool)window_system);
            return;
        }

        auto pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_system->getRenderPipeline());
        if (!pipeline)
        {
            LOG_ERROR("[Picking Trigger] Error: Cannot get render pipeline");
            return;
        }

        const auto& layout_state = pipeline->getEditorLayoutState();
        float viewport_x = layout_state.sceneViewport.x;
        float viewport_y = layout_state.sceneViewport.y;
        float viewport_width = layout_state.sceneViewport.width;
        float viewport_height = layout_state.sceneViewport.height;

        LOG_INFO("[Picking Trigger] Scene viewport: x={:.1f}, y={:.1f}, w={:.1f}, h={:.1f}", 
                 viewport_x, viewport_y, viewport_width, viewport_height);

        if (viewport_width <= 1.0f || viewport_height <= 1.0f)
        {
            LOG_ERROR("[Picking Trigger] Error: Invalid viewport size");
            return;
        }

        float window_cursor_x = static_cast<float>(m_last_cursor_x);
        float window_cursor_y = static_cast<float>(m_last_cursor_y);

        float viewport_cursor_x = window_cursor_x - viewport_x;
        float viewport_cursor_y = window_cursor_y - viewport_y;

        LOG_INFO("[Picking Trigger] Window coords: ({:.1f}, {:.1f})", window_cursor_x, window_cursor_y);
        LOG_INFO("[Picking Trigger] Viewport coords: ({:.1f}, {:.1f})", viewport_cursor_x, viewport_cursor_y);

        if (viewport_cursor_x < 0.0f || viewport_cursor_x > viewport_width ||
            viewport_cursor_y < 0.0f || viewport_cursor_y > viewport_height)
        {
            LOG_INFO("[Picking Trigger] Click not in scene viewport, skipping picking");
            return;
        }

        auto camera = render_system->getRenderCamera();
        if (!camera)
        {
            LOG_ERROR("[Picking Trigger] Error: Cannot get render camera");
            return;
        }

        picking_system->pickFromScreen(viewport_cursor_x, viewport_cursor_y, 
                                       viewport_width, viewport_height, *camera);
    }

} // namespace Elish