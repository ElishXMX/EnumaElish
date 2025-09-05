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
#include <iomanip>
#include <sstream>

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
        m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::jump);
        
        if (action == GLFW_PRESS)
        {
            LOG_DEBUG("Key Pressed: {} (scancode: {}, action: {}, mods: {})", glfwGetKeyName(key, scancode), scancode, action, mods);
            switch (key)
            {
                case GLFW_KEY_ESCAPE:
                    // close();
                    break;
                case GLFW_KEY_R:
                    break;
                case GLFW_KEY_A:
                    m_game_command |= (unsigned int)GameCommand::left;
                    break;
                case GLFW_KEY_S:
                    m_game_command |= (unsigned int)GameCommand::backward;
                    break;
                case GLFW_KEY_W:
                    m_game_command |= (unsigned int)GameCommand::forward;
                    break;
                case GLFW_KEY_D:
                    m_game_command |= (unsigned int)GameCommand::right;
                    break;
                case GLFW_KEY_SPACE:
                    m_game_command |= (unsigned int)GameCommand::jump;
                    break;
                case GLFW_KEY_LEFT_CONTROL:
                    m_game_command |= (unsigned int)GameCommand::squat;
                    break;
                case GLFW_KEY_LEFT_ALT: {
                    std::shared_ptr<WindowSystem> window_system = g_runtime_global_context.m_window_system;
                    window_system->setFocusMode(!window_system->getFocusMode());
                }
                break;
                case GLFW_KEY_LEFT_SHIFT:
                    m_game_command |= (unsigned int)GameCommand::sprint;
                    break;
                case GLFW_KEY_F:
                    m_game_command ^= (unsigned int)GameCommand::free_carema;
                    break;
                default:
                    break;
            }
        }
        else if (action == GLFW_RELEASE)
        {
            LOG_DEBUG("Key Released: {} (scancode: {}, action: {}, mods: {})", glfwGetKeyName(key, scancode), scancode, action, mods);
            switch (key)
            {
                case GLFW_KEY_ESCAPE:
                    // close();
                    break;
                case GLFW_KEY_R:
                    break;
                case GLFW_KEY_W:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::forward);
                    break;
                case GLFW_KEY_S:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::backward);
                    break;
                case GLFW_KEY_A:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::left);
                    break;
                case GLFW_KEY_D:
                    m_game_command &= (k_complement_control_command ^ (unsigned int)GameCommand::right);
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
        }
    }

    /**
     * @brief 鼠标光标位置事件的回调函数。
     * 当鼠标光标移动时调用此函数，并更新光标的增量。
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
        // LOG_DEBUG("[MOUSE_EVENT][{}ms] onCursorPos triggered - Focus: {}, Current: ({:.2f}, {:.2f}), Last: ({:.2f}, {:.2f})", 
        //           timestamp, focus_mode, current_cursor_x, current_cursor_y, m_last_cursor_x, m_last_cursor_y);
        
        if (focus_mode)
        {
            m_cursor_delta_x = m_last_cursor_x - current_cursor_x;
            m_cursor_delta_y = m_last_cursor_y - current_cursor_y;
            
            // 输出详细的鼠标增量信息
            // LOG_DEBUG("[MOUSE_DELTA][{}ms] Delta calculated - X: {:.4f}, Y: {:.4f}", 
            //           timestamp, m_cursor_delta_x, m_cursor_delta_y);
        }
        else
        {
            // LOG_DEBUG("[MOUSE_EVENT][{}ms] Focus mode disabled, delta not calculated", timestamp);
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
     * 使用glm库直接计算光标移动对应的角度变化，用于相机控制。
     * 基于固定的45度FOV值和当前窗口尺寸进行计算。
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
            LOG_DEBUG("[ANGLE_CALC][{}ms] Invalid window size, skipping calculation", timestamp);
            return;
        }

        // 使用固定的45度FOV值（与main_camera_pass.cpp中的设置保持一致）
        const float fov_y_degrees = 45.0f;
        const float fov_y_radians = glm::radians(fov_y_degrees);
        
        // 计算宽高比
        const float aspect_ratio = static_cast<float>(window_size[0]) / static_cast<float>(window_size[1]);
        
        // 根据Y方向FOV和宽高比计算X方向FOV
        const float fov_x_radians = 2.0f * glm::atan(glm::tan(fov_y_radians * 0.5f) * aspect_ratio);
        
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Intermediate values - FOV_Y: {:.4f}°, FOV_Y_rad: {:.6f}, Aspect: {:.4f}, FOV_X_rad: {:.6f}", 
        //           timestamp, fov_y_degrees, fov_y_radians, aspect_ratio, fov_x_radians);
        
        // 将光标像素移动转换为弧度
        // 光标移动的像素值除以窗口尺寸，再乘以对应方向的FOV
        float normalized_x = static_cast<float>(m_cursor_delta_x) / static_cast<float>(window_size[0]);
        float normalized_y = static_cast<float>(m_cursor_delta_y) / static_cast<float>(window_size[1]);
        
        m_cursor_delta_yaw = normalized_x * fov_x_radians;
        m_cursor_delta_pitch = -normalized_y * fov_y_radians;
        
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Normalized deltas - X: {:.6f}, Y: {:.6f}", timestamp, normalized_x, normalized_y);
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Final angles (radians) - Yaw: {:.6f}, Pitch: {:.6f}", timestamp, m_cursor_delta_yaw, m_cursor_delta_pitch);
        // LOG_DEBUG("[ANGLE_CALC][{}ms] Final angles (degrees) - Yaw: {:.4f}°, Pitch: {:.4f}°", 
        //           timestamp, glm::degrees(m_cursor_delta_yaw), glm::degrees(m_cursor_delta_pitch));
    }

    /**
     * @brief 初始化输入系统。
     * 此函数负责向 WindowSystem 注册键盘和鼠标光标位置的回调，以确保在输入事件发生时能及时更新输入状态。
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
     */
    void InputSystem::processCameraRotation()
    {
        if (m_cursor_delta_yaw != 0.0f || m_cursor_delta_pitch != 0.0f)
        {
            // 累积角度变化
            m_accumulated_yaw += m_cursor_delta_yaw * CAMERA_MOUSE_SENSITIVITY;
            m_accumulated_pitch += m_cursor_delta_pitch * CAMERA_MOUSE_SENSITIVITY;
            
            // 限制俯仰角范围
            const float max_pitch = glm::radians(89.0f);
            m_accumulated_pitch = glm::clamp(m_accumulated_pitch, -max_pitch, max_pitch);
            
            // 创建旋转四元数
            glm::quat yaw_rotation = glm::angleAxis(m_accumulated_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat pitch_rotation = glm::angleAxis(m_accumulated_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            
            // 组合旋转（先偏航后俯仰）
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
        glm::vec3 movement(0.0f);
        float move_speed = CAMERA_MOVE_SPEED;
        
        // 检查冲刺
        if (m_game_command & (1 << static_cast<int>(GameCommand::sprint)))
        {
            move_speed *= CAMERA_SPRINT_MULTIPLIER;
        }
        
        // 计算移动向量（基于摄像机的局部坐标系）
        glm::mat3 rotation_matrix = glm::mat3_cast(m_camera_rotation);
        glm::vec3 forward = -rotation_matrix[2]; // 摄像机前方向（负Z）
        glm::vec3 right = rotation_matrix[0];    // 摄像机右方向（正X）
        glm::vec3 up = rotation_matrix[1];       // 摄像机上方向（正Y）
        
        if (m_game_command & (1 << static_cast<int>(GameCommand::forward)))
        {
            movement += forward;
        }
        if (m_game_command & (1 << static_cast<int>(GameCommand::backward)))
        {
            movement -= forward;
        }
        if (m_game_command & (1 << static_cast<int>(GameCommand::right)))
        {
            movement += right;
        }
        if (m_game_command & (1 << static_cast<int>(GameCommand::left)))
        {
            movement -= right;
        }
        if (m_game_command & (1 << static_cast<int>(GameCommand::jump)))
        {
            movement += up;
        }
        if (m_game_command & (1 << static_cast<int>(GameCommand::squat)))
        {
            movement -= up;
        }
        
        // 归一化移动向量并应用速度和时间
        if (glm::length(movement) > 0.0f)
        {
            movement = glm::normalize(movement) * move_speed * delta_time;
            m_camera_position += movement;
            
            // 调试信息
            auto now = std::chrono::steady_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            
            // LOG_DEBUG("[processCameraMovement] [CAMERA_MOVEMENT][{}ms] Position: ({:.3f}, {:.3f}, {:.3f}), Movement: ({:.3f}, {:.3f}, {:.3f})",
            //     timestamp, m_camera_position.x, m_camera_position.y, m_camera_position.z,
            //     movement.x, movement.y, movement.z);
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
