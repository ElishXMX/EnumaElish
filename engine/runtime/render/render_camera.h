#pragma once

#include <mutex>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Elish
{
    enum class RenderCameraType : int
    {
        Editor,
        Motor
    };

    class RenderCamera
    {
    public:
        RenderCameraType m_current_camera_type {RenderCameraType::Editor};

        static const glm::vec3 X, Y, Z;
        
        // 相机控制参数
        static constexpr float CAMERA_MOVE_SPEED = 0.1f;      // 基础移动速度
        static constexpr float CAMERA_SPRINT_MULTIPLIER = 3.0f; // 冲刺速度倍数
        static constexpr float CAMERA_MOUSE_SENSITIVITY = 3.0f; // 鼠标灵敏度，值越大旋转越快 // 鼠标灵敏度

        glm::vec3 m_position {0.0f, 0.0f, 0.0f};
        glm::quat m_rotation {0.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
        glm::quat m_invRotation {0.0f, 0.0f, 0.0f, 0.0f};
        float     m_znear {0.1f};
        float     m_zfar {1000.0f};
        glm::vec3 m_up_axis {Y};

        static constexpr float MIN_FOV {10.0f};
        static constexpr float MAX_FOV {89.0f};
        static constexpr int   MAIN_VIEW_MATRIX_INDEX {0};

        std::vector<glm::mat4> m_view_matrices {glm::mat4(1.0f)};

        void setCurrentCameraType(RenderCameraType type);
        void setMainViewMatrix(const glm::mat4& view_matrix, RenderCameraType type = RenderCameraType::Editor);

        void move(const glm::vec3& delta);
        void rotate(const glm::vec2& delta);
        void zoom(float offset);
        void lookAt(const glm::vec3& position, const glm::vec3& target, const glm::vec3& up);
        
        // 相机控制函数
        void processInput(unsigned int game_command, float cursor_delta_yaw, float cursor_delta_pitch, float delta_time);
        void moveForward(float distance);
        void moveBackward(float distance);
        void moveLeft(float distance);
        void moveRight(float distance);
        void moveUp(float distance);
        void moveDown(float distance);

        void setAspect(float aspect);
        void setFOVx(float fovx) { m_fovx = fovx; }

        glm::vec3 position() const { return m_position; }
        glm::quat rotation() const { return m_rotation; }

        glm::vec3 forward() const { return m_invRotation * (-Z); }  // -Z轴向前
        glm::vec3 up() const { return m_invRotation * Y; }          // Y轴向上  
        glm::vec3 right() const { return m_invRotation * X; }       // X轴向右
        glm::vec2 getFOV() const { return {m_fovx, m_fovy}; }
        glm::mat4 getViewMatrix();
        glm::mat4 getPersProjMatrix() const;
        glm::mat4 getLookAtMatrix() const { return glm::lookAt(position(), position() + forward(), up()); }
        float     getFovYDeprecated() const { return m_fovy; }

    protected:
        float m_aspect {0.f};
        float m_fovx {89.0f};
        float m_fovy {0.f};

        std::mutex m_view_matrix_mutex;
    };

    inline const glm::vec3 RenderCamera::X = {1.0f, 0.0f, 0.0f};
    inline const glm::vec3 RenderCamera::Y = {0.0f, 1.0f, 0.0f};
    inline const glm::vec3 RenderCamera::Z = {0.0f, 0.0f, 1.0f};

} // namespace Elish
