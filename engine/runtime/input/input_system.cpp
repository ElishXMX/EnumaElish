#include "input_system.h"

#include "../core/base/macro.h"

#include "../engine.h"
#include "../global/global_context.h"
// #include "../render/render_camera.h"
#include "../render/render_system.h"
#include "../render/window_system.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>

namespace Elish
{
    unsigned int k_complement_control_command = 0xFFFFFFFF;

    /**
     * @class InputSystem
     * @brief 处理用户输入，包括键盘、鼠标等事件。
     *
     * 该系统负责捕获原始输入事件，并将其转换为游戏逻辑可以理解的命令。
     * 它还管理输入状态，例如按键是否按下、鼠标位置变化等。
     */

    /**
     * @brief 键盘按键事件的回调函数。
     * 当键盘按键被按下、释放或重复时调用此函数。
     * 目前包含编辑器模式下被注释掉的逻辑。
     *
     * @param key 被按下或释放的键盘按键。
     * @param scancode 按键的系统特定扫描码。
     * @param action 按键的动作（GLFW_PRESS：按下，GLFW_RELEASE：释放，GLFW_REPEAT：重复）。
     * @param mods 描述按下了哪些修饰键的位字段。
     */
    void InputSystem::onKey(int key, int scancode, int action, int mods)
    {
        // if (!g_is_editor_mode)
        // {
            onKeyInGameMode(key, scancode, action, mods);
        // }
    }

    /**
     * @brief 专门处理游戏模式下的键盘输入。
     * 此函数处理按键的按下和释放，以更新游戏命令状态。
     *
     * @param key 被按下或释放的键盘按键。
     * @param scancode 按键的系统特定扫描码。
     * @param action 按键的动作（GLFW_PRESS：按下，GLFW_RELEASE：释放，GLFW_REPEAT：重复）。
     * @param mods 描述按下了哪些修饰键的位字段。
     */
    void InputSystem::onKeyInGameMode(int key, int scancode, int action, int mods)
    {
        // 获取当前时间戳用于调试
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        // 清除跳跃命令的持续状态（跳跃应该是瞬时的）
        m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::jump);
        
        if (action == GLFW_PRESS)
        {
            const char* key_name = glfwGetKeyName(key, scancode);
            // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Key Pressed: {} (code: {}, scancode: {}, mods: {})", 
                      timestamp, key_name ? key_name : "UNKNOWN", key, scancode, mods);
            
            unsigned int old_command = m_game_command;
            
            switch (key)
            {
                case GLFW_KEY_ESCAPE:
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] ESC key pressed - No action defined", timestamp);
                    break;
                case GLFW_KEY_R:
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] R key pressed - No action defined", timestamp);
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
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] SPACE key pressed - JUMP activated", timestamp);
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    m_game_command |= (unsigned int)GameCommand::squat;
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] LEFT_CTRL key pressed - SQUAT activated", timestamp);
                    break;
                case GLFW_KEY_LEFT_ALT: {
                    std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;
                    bool old_focus = window_system->getFocusMode();
                    window_system->setFocusMode(!old_focus);
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] LEFT_ALT key pressed - Focus mode toggled: {} -> {}", 
                              // timestamp, old_focus, !old_focus);
                }
                break;
                case GLFW_KEY_LEFT_SHIFT:
                    m_game_command |= (unsigned int)GameCommand::sprint;
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] LEFT_SHIFT key pressed - SPRINT activated", timestamp);
                    break;
                case GLFW_KEY_F:
                    m_game_command ^= (unsigned int)GameCommand::free_carema;
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] F key pressed - FREE_CAMERA toggled", timestamp);
                    break;
                default:
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Unhandled key pressed: {} (code: {})", 
                    //           timestamp, key_name ? key_name : "UNKNOWN", key);
                    break;
            }
            
            // 输出命令状态变化
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
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] ESC key released - No action defined", timestamp);
                    break;
                case GLFW_KEY_R:
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] R key released - No action defined", timestamp);
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
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] LEFT_CTRL key released - SQUAT deactivated", timestamp);
                    break;
                case GLFW_KEY_LEFT_SHIFT:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::sprint);
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] LEFT_SHIFT key released - SPRINT deactivated", timestamp);
                    break;
                default:
                    // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Unhandled key released: {} (code: {})", 
                    //           timestamp, key_name ? key_name : "UNKNOWN", key);
                    break;
            }
            
            // 输出命令状态变化
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
        else if (action == GLFW_REPEAT)
        {
            // 处理按键重复事件（可选）
            const char* key_name = glfwGetKeyName(key, scancode);
            // LOG_DEBUG("[KEYBOARD_INPUT][{}ms] Key Repeat: {} (code: {})", 
            //           timestamp, key_name ? key_name : "UNKNOWN", key);
        }
    }

    /**
     * @brief 鼠标按钮事件的回调函数。
     * 处理鼠标按钮的按下和释放事件，用于拖动检测。
     *
     * @param button 鼠标按钮（GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT等）。
     * @param action 按钮动作（GLFW_PRESS或GLFW_RELEASE）。
     * @param mods 修饰键状态。
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
                // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Left mouse button pressed - Start position: ({:.2f}, {:.2f})", 
                //           timestamp, m_drag_start_x, m_drag_start_y);
            }
            else if (action == GLFW_RELEASE)
            {
                m_mouse_left_pressed = false;
                m_is_dragging = false;
                // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Left mouse button released - Dragging stopped", timestamp);
            }
        }
        else if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS)
            {
                m_mouse_right_pressed = true;
                // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Right mouse button pressed", timestamp);
            }
            else if (action == GLFW_RELEASE)
            {
                m_mouse_right_pressed = false;
                // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Right mouse button released", timestamp);
            }
        }
    }
    
    /**
     * @brief 鼠标光标位置事件的回调函数。
     * 当鼠标光标移动时调用此函数，并更新光标的增量。
     * 现在包含拖动检测逻辑，仅在按下并拖动时计算增量。
     *
     * @param current_cursor_x 光标当前的X坐标。
     * @param current_cursor_y 光标当前的Y坐标。
     */
    void InputSystem::onCursorPos(double current_cursor_x, double current_cursor_y)
    {
        // 获取当前时间戳
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        // 记录鼠标事件触发状态
        bool focus_mode = g_runtime_global_context.m_window_system->getFocusMode();
        
        // 检测拖动状态
        if (m_mouse_left_pressed && !m_is_dragging)
        {
            double dx = current_cursor_x - m_drag_start_x;
            double dy = current_cursor_y - m_drag_start_y;
            double distance = std::sqrt(dx * dx + dy * dy);
            
            if (distance > DRAG_THRESHOLD)
            {
                m_is_dragging = true;
                // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Drag detected - Distance: {:.2f}, Threshold: {:.2f}", 
                //          timestamp, distance, DRAG_THRESHOLD);
            }
        }
        
        // 仅在焦点模式且正在拖动时计算增量
        if (focus_mode && m_is_dragging)
        {
            m_cursor_delta_x = m_last_cursor_x - current_cursor_x;
            m_cursor_delta_y = m_last_cursor_y - current_cursor_y;
            
            // LOG_DEBUG("[MOUSE_CONTROL][{}ms] Drag movement - Delta: ({:.4f}, {:.4f})", 
            //           timestamp, m_cursor_delta_x, m_cursor_delta_y);
        }
        else
        {
            // 不在拖动状态时，清零增量
            m_cursor_delta_x = 0;
            m_cursor_delta_y = 0;
        }
        
        m_last_cursor_x = current_cursor_x;
        m_last_cursor_y = current_cursor_y;
    }

    /**
     * @brief 清除光标增量值。
     * 此函数将光标移动的增量和对应的角度变化重置为零。
     */
    void InputSystem::clear()
    {
        m_cursor_delta_x = 0;
        m_cursor_delta_y = 0;
        m_cursor_delta_yaw = 0.0f;
        m_cursor_delta_pitch = 0.0f;
    }

    /**
     * @brief 根据窗口大小和FOV计算光标的增量角度。
     * 优化鼠标控制响应性，实现更直观的视角旋转。
     * 使用简化的计算方法提高性能和响应速度。
     */
    void InputSystem::calculateCursorDeltaAngles()
    {
        // 获取当前时间戳
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        std::array<int, 2> window_size = g_runtime_global_context.m_window_system->getWindowSize();
        
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Starting angle calculation - Window size: {}x{}, Delta: ({:.4f}, {:.4f})", 
        //           timestamp, window_size[0], window_size[1], m_cursor_delta_x, m_cursor_delta_y);

        if (window_size[0] < 1 || window_size[1] < 1)
        {
            // LOG_DEBUG("[ANGLE_CALC][{}ms] Invalid window size, skipping calculation", timestamp);
            return;
        }

        // 使用简化的鼠标增量计算，提高响应性
        // 直接将像素增量转换为角度增量，避免复杂的FOV计算
        const float mouse_sensitivity_factor = 0.002f; // 基础灵敏度因子
        
        // 计算角度增量（弧度）
        m_cursor_delta_yaw = static_cast<float>(m_cursor_delta_x) * mouse_sensitivity_factor;
        m_cursor_delta_pitch = static_cast<float>(m_cursor_delta_y) * mouse_sensitivity_factor;
        
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Simplified calculation - Yaw: {:.6f}, Pitch: {:.6f}", timestamp, m_cursor_delta_yaw, m_cursor_delta_pitch);
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Final angles (degrees) - Yaw: {:.4f}°, Pitch: {:.4f}°", 
        //           timestamp, glm::degrees(m_cursor_delta_yaw), glm::degrees(m_cursor_delta_pitch));
    }

    /**
     * @brief 初始化输入系统。
     * 此函数负责向 WindowSystem 注册键盘、鼠标按钮和鼠标光标位置的回调，以确保在输入事件发生时能及时更新输入状态。
     */
    void InputSystem::initialize()
    {
        std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;

        window_system->registerOnKeyFunc(std::bind(&InputSystem::onKey,
                                                   this,
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
     * @brief 更新输入系统状态。
     * 此函数用于更新输入状态、计算光标增量、清除光标增量以及根据窗口焦点更新游戏命令。
     */
    void InputSystem::tick()
    {
        calculateCursorDeltaAngles();
        
        // 更新摄像机状态（使用固定的16ms时间步长）
        updateCameraState(0.016f);
       
        // 只清除鼠标增量，不清除累积的角度
        m_cursor_delta_x = 0;
        m_cursor_delta_y = 0;
        m_cursor_delta_yaw = 0.0f;
        m_cursor_delta_pitch = 0.0f;

        std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;
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
     * @brief 更新摄像机状态，处理移动和旋转
     * @param delta_time 时间增量
     */
    void InputSystem::updateCameraState(float delta_time)
    {
        // 处理摄像机旋转
        processCameraRotation();
        
        // 处理摄像机移动
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
            // 累积角度变化，应用鼠标灵敏度
            m_accumulated_yaw += m_cursor_delta_yaw * CAMERA_MOUSE_SENSITIVITY;
            m_accumulated_pitch += m_cursor_delta_pitch * CAMERA_MOUSE_SENSITIVITY;
            
            // 限制俯仰角范围，防止翻转但允许更大的旋转范围
            const float max_pitch = glm::radians(89.5f); // 稍微放宽限制
            m_accumulated_pitch = glm::clamp(m_accumulated_pitch, -max_pitch, max_pitch);
            
            // 创建旋转四元数，使用更平滑的旋转组合
            glm::quat yaw_rotation = glm::angleAxis(m_accumulated_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat pitch_rotation = glm::angleAxis(m_accumulated_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            
            // 组合旋转（先偏航后俯仰），确保旋转顺序正确
            m_camera_rotation = yaw_rotation * pitch_rotation;
            
            // 调试信息
            auto now = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            
            // LOG_DEBUG("[processCameraRotation] [CAMERA_ROTATION][{}ms] Accumulated angles - Yaw: {:.6f} rad ({:.4f}°), Pitch: {:.6f} rad ({:.4f}°)",
            //     timestamp, m_accumulated_yaw, glm::degrees(m_accumulated_yaw), m_accumulated_pitch, glm::degrees(m_accumulated_pitch));
        }
    }
    
    /**
     * @brief 处理摄像机移动逻辑
     * @param delta_time 时间增量
     */
    void InputSystem::processCameraMovement(float delta_time)
    {
        // 获取时间戳用于调试
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        // 记录当前游戏命令状态
        LOG_DEBUG("[processCameraMovement] Current game command: 0x{:X}, delta_time: {:.6f}", m_game_command, delta_time);
        
        glm::vec3 movement(0.0f);
        float move_speed = CAMERA_MOVE_SPEED;
        
        // 检查冲刺
        if (m_game_command & static_cast<unsigned int>(GameCommand::sprint))
        {
            move_speed *= CAMERA_SPRINT_MULTIPLIER;
            LOG_DEBUG("[processCameraMovement] Sprint mode activated, speed: {:.3f}", move_speed);
        }
        
        // 计算移动向量（基于摄像机的局部坐标系）
        glm::mat3 rotation_matrix = glm::mat3_cast(m_camera_rotation);
        glm::vec3 forward = -rotation_matrix[2]; // 摄像机前方向（负Z）
        glm::vec3 right = rotation_matrix[0];    // 摄像机右方向（正X）
        glm::vec3 up = rotation_matrix[1];       // 摄像机上方向（正Y）
        
        // 修复WASD控制逻辑，确保符合标准操作习惯
        if (m_game_command & static_cast<unsigned int>(GameCommand::forward)) // W键前进
        {
            movement += forward;
            LOG_DEBUG("[processCameraMovement] FORWARD command detected, adding forward vector: ({:.3f}, {:.3f}, {:.3f})", forward.x, forward.y, forward.z);
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::backward)) // S键后退
        {
            movement -= forward;
            LOG_DEBUG("[processCameraMovement] BACKWARD command detected, subtracting forward vector: ({:.3f}, {:.3f}, {:.3f})", forward.x, forward.y, forward.z);
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::right)) // D键右移
        {
            movement += right;
            LOG_DEBUG("[processCameraMovement] RIGHT command detected, adding right vector: ({:.3f}, {:.3f}, {:.3f})", right.x, right.y, right.z);
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::left)) // A键左移
        {
            movement -= right;
            LOG_DEBUG("[processCameraMovement] LEFT command detected, subtracting right vector: ({:.3f}, {:.3f}, {:.3f})", right.x, right.y, right.z);
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::jump))
        {
            movement += up;
            LOG_DEBUG("[processCameraMovement] JUMP command detected, adding up vector: ({:.3f}, {:.3f}, {:.3f})", up.x, up.y, up.z);
        }
        if (m_game_command & static_cast<unsigned int>(GameCommand::squat))
        {
            movement -= up;
            LOG_DEBUG("[processCameraMovement] SQUAT command detected, subtracting up vector: ({:.3f}, {:.3f}, {:.3f})", up.x, up.y, up.z);
        }
        
        // 记录原始移动向量
        LOG_DEBUG("[processCameraMovement] Raw movement vector: ({:.6f}, {:.6f}, {:.6f}), length: {:.6f}", 
                  movement.x, movement.y, movement.z, glm::length(movement));
        
        // 归一化移动向量并应用速度和时间
        if (glm::length(movement) > 0.0f)
        {
            glm::vec3 old_position = m_camera_position;
            movement = glm::normalize(movement) * move_speed * delta_time;
            m_camera_position += movement;
            
            LOG_DEBUG("[processCameraMovement] [CAMERA_MOVEMENT][{}ms] Position: ({:.3f}, {:.3f}, {:.3f}) -> ({:.3f}, {:.3f}, {:.3f}), Movement: ({:.3f}, {:.3f}, {:.3f})",
                timestamp, old_position.x, old_position.y, old_position.z,
                m_camera_position.x, m_camera_position.y, m_camera_position.z,
                movement.x, movement.y, movement.z);
        }
        else
        {
            LOG_DEBUG("[processCameraMovement] No movement detected - game_command: 0x{:X}", m_game_command);
            
            // 检查是否有按键但没有移动的异常情况
            if (m_game_command != 0)
            {
                LOG_DEBUG("[processCameraMovement] WARNING: Game command is non-zero (0x{:X}) but no movement calculated!", m_game_command);
                
                // 详细检查每个命令位
                if (m_game_command & static_cast<unsigned int>(GameCommand::forward))
                    LOG_DEBUG("[processCameraMovement] - FORWARD bit is set");
                if (m_game_command & static_cast<unsigned int>(GameCommand::backward))
                    LOG_DEBUG("[processCameraMovement] - BACKWARD bit is set");
                if (m_game_command & static_cast<unsigned int>(GameCommand::left))
                    LOG_DEBUG("[processCameraMovement] - LEFT bit is set");
                if (m_game_command & static_cast<unsigned int>(GameCommand::right))
                    LOG_DEBUG("[processCameraMovement] - RIGHT bit is set");
            }
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
} // namespace Elish
