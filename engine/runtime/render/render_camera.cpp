#include "render_camera.h"
#include "../input/input_system.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include "../core/base/macro.h"

namespace Elish
{
    void RenderCamera::setCurrentCameraType(RenderCameraType type)
    {
        std::lock_guard<std::mutex> lock_guard(m_view_matrix_mutex);
        m_current_camera_type = type;
    }

    void RenderCamera::setMainViewMatrix(const glm::mat4& view_matrix, RenderCameraType type)
    {
        std::lock_guard<std::mutex> lock_guard(m_view_matrix_mutex);
        m_current_camera_type                   = type;
        m_view_matrices[MAIN_VIEW_MATRIX_INDEX] = view_matrix;

        // 从视图矩阵中提取相机位置
        glm::vec3 s = glm::vec3(view_matrix[0][0], view_matrix[0][1], view_matrix[0][2]);
        glm::vec3 u = glm::vec3(view_matrix[1][0], view_matrix[1][1], view_matrix[1][2]);
        glm::vec3 f = glm::vec3(-view_matrix[2][0], -view_matrix[2][1], -view_matrix[2][2]);
        m_position = s * (-view_matrix[0][3]) + u * (-view_matrix[1][3]) + f * view_matrix[2][3];
    }

    void RenderCamera::move(const glm::vec3& delta) { m_position += delta; }

    void RenderCamera::rotate(const glm::vec2& delta)
    {
        // 将角度从度转换为弧度
        glm::vec2 radians_delta = glm::radians(delta);

        // 限制俯仰角，防止翻转
        float dot = glm::dot(m_up_axis, forward());
        if ((dot < -0.99f && radians_delta.x > 0.0f) || // 角度接近180度
            (dot > 0.99f && radians_delta.x < 0.0f))    // 角度接近0度
            radians_delta.x = 0.0f;

        // 俯仰角相对于当前的侧向旋转
        // 偏航角独立发生
        // 这防止了滚转
        glm::quat pitch = glm::angleAxis(radians_delta.x, X);
        glm::quat yaw = glm::angleAxis(radians_delta.y, Z);

        m_rotation = pitch * m_rotation * yaw;
        m_invRotation = glm::conjugate(m_rotation);
    }

    void RenderCamera::zoom(float offset)
    {
        // > 0 = 放大 (减少FOV角度)
        m_fovx = std::clamp(m_fovx - offset, MIN_FOV, MAX_FOV);
    }

    void RenderCamera::lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up)
    {
        m_position = position;

        // 计算前向向量
        glm::vec3 forward = glm::normalize(target - position);
        
        // 计算右向量
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::normalize(up)));
        
        // 重新计算上向量以确保正交
        glm::vec3 orthUp = glm::cross(right, forward);
        
        // 构建旋转矩阵
        glm::mat3 rotMatrix;
        rotMatrix[0] = right;
        rotMatrix[1] = orthUp;
        rotMatrix[2] = -forward; // 注意：相机朝向-Z方向
        
        // 从旋转矩阵创建四元数
        m_rotation = glm::quat_cast(rotMatrix);
        m_invRotation = glm::conjugate(m_rotation);
    }

    glm::mat4 RenderCamera::getViewMatrix()
    {
        std::lock_guard<std::mutex> lock_guard(m_view_matrix_mutex);
        if (m_current_camera_type == RenderCameraType::Motor)
        {
            return m_view_matrices[MAIN_VIEW_MATRIX_INDEX];
        }
        else
        {
            // 使用glm创建视图矩阵
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), -m_position);
            glm::mat4 rotation = glm::mat4_cast(m_invRotation);
            return rotation * translation;
        }
    }

    glm::mat4 RenderCamera::getPersProjMatrix() const
    {
        // Vulkan的坐标系修正矩阵 (Y轴翻转)
        glm::mat4 fix_mat = glm::mat4(
            1.0f,  0.0f, 0.0f, 0.0f,
            0.0f, -1.0f, 0.0f, 0.0f,
            0.0f,  0.0f, 1.0f, 0.0f,
            0.0f,  0.0f, 0.0f, 1.0f
        );
        
        // 使用glm创建透视投影矩阵
         glm::mat4 proj_mat = glm::perspective(glm::radians(m_fovx), m_aspect, m_znear, m_zfar);
         return fix_mat * proj_mat;
    }

    void RenderCamera::setAspect(float aspect)
    {
        m_aspect = aspect;

        // 1 / tan(fovy * 0.5) / aspect = 1 / tan(fovx * 0.5)
        // 1 / tan(fovy * 0.5) = aspect / tan(fovx * 0.5)
        // tan(fovy * 0.5) = tan(fovx * 0.5) / aspect

        m_fovy = glm::degrees(std::atan(std::tan(glm::radians(m_fovx) * 0.5f) / m_aspect) * 2.0f);
    }


     // 相机控制函数实现
     void RenderCamera::moveForward(float distance)
     {
         m_position += forward() * distance;
     }

     void RenderCamera::moveBackward(float distance)
     {
         m_position -= forward() * distance;
     }

     void RenderCamera::moveLeft(float distance)
     {
         m_position -= right() * distance;
     }

     void RenderCamera::moveRight(float distance)
     {
         m_position += right() * distance;
     }

     void RenderCamera::moveUp(float distance)
     {
         m_position += up() * distance;
     }

     void RenderCamera::moveDown(float distance)
     {
         m_position -= up() * distance;
     }

     void RenderCamera::processInput(unsigned int game_command, float cursor_delta_yaw, float cursor_delta_pitch, float delta_time)
     {
         // 获取当前时间戳
         auto now = std::chrono::high_resolution_clock::now();
         auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
         
        //  LOG_DEBUG("[CAMERA_INPUT][{}ms] processInput called - Command: 0x{:X}, Yaw: {:.6f}, Pitch: {:.6f}, DeltaTime: {:.6f}", 
        //            timestamp, game_command, cursor_delta_yaw, cursor_delta_pitch, delta_time);
         
         // 计算移动距离（基于时间的平滑移动）
         float move_distance = CAMERA_MOVE_SPEED * delta_time;
         
         // 检查是否按下冲刺键
         if (game_command & (1 << static_cast<int>(GameCommand::sprint)))
         {
             move_distance *= CAMERA_SPRINT_MULTIPLIER;
         }
         
         // 处理键盘移动命令
         std::string movement_info = "";
         if (game_command & (1 << static_cast<int>(GameCommand::forward)))
         {
             moveForward(move_distance);
             movement_info += "FORWARD ";
         }
         if (game_command & (1 << static_cast<int>(GameCommand::backward)))
         {
             moveBackward(move_distance);
             movement_info += "BACKWARD ";
         }
         if (game_command & (1 << static_cast<int>(GameCommand::left)))
         {
             moveLeft(move_distance);
             movement_info += "LEFT ";
         }
         if (game_command & (1 << static_cast<int>(GameCommand::right)))
         {
             moveRight(move_distance);
             movement_info += "RIGHT ";
         }
         if (game_command & (1 << static_cast<int>(GameCommand::jump)))
         {
             moveUp(move_distance);
             movement_info += "UP ";
         }
         if (game_command & (1 << static_cast<int>(GameCommand::squat)))
         {
             moveDown(move_distance);
             movement_info += "DOWN ";
         }
         
         if (!movement_info.empty())
         {
             LOG_DEBUG("[CAMERA_MOVEMENT][{}ms] Movement detected - Commands: [{}], Distance: {:.4f}", 
                       timestamp, movement_info, move_distance);
         }
         
         // 处理鼠标旋转（应用灵敏度）
         if (std::abs(cursor_delta_yaw) > 0.001f || std::abs(cursor_delta_pitch) > 0.001f)
         {
            //  LOG_DEBUG("[CAMERA_ROTATION][{}ms] Mouse rotation input - Raw delta: ({:.6f}, {:.6f}), Sensitivity: {:.4f}", 
            //            timestamp, cursor_delta_yaw, cursor_delta_pitch, CAMERA_MOUSE_SENSITIVITY);
             
             glm::vec2 mouse_delta(cursor_delta_yaw * CAMERA_MOUSE_SENSITIVITY, 
                                   cursor_delta_pitch * CAMERA_MOUSE_SENSITIVITY);
             
            //  LOG_DEBUG("[CAMERA_ROTATION][{}ms] Applying rotation - Final delta: ({:.6f}, {:.6f})", 
            //            timestamp, mouse_delta.x, mouse_delta.y);
             
             rotate(mouse_delta);
         }
         else
         {
             LOG_DEBUG("[CAMERA_ROTATION][{}ms] No mouse rotation input", timestamp);
         }
     }
 } // namespace Elish
