#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Elish
{
    enum class GameCommand : unsigned int
    {
        forward  = 1 << 0,                 // W
        backward = 1 << 1,                 // S
        left     = 1 << 2,                 // A
        right    = 1 << 3,                 // D
        jump     = 1 << 4,                 // SPACE
        squat    = 1 << 5,                 // not implemented yet
        sprint   = 1 << 6,                 // LEFT SHIFT
        fire     = 1 << 7,                 // not implemented yet
        free_carema = 1 << 8,              // F
        invalid  = (unsigned int)(1 << 31) // lost focus
    };

    extern unsigned int k_complement_control_command;

    class InputSystem
    {

    public:
        void onKey(int key, int scancode, int action, int mods);
        void onCursorPos(double current_cursor_x, double current_cursor_y);

        void initialize();
        void tick();
        void clear();

        int m_cursor_delta_x {0};
        int m_cursor_delta_y {0};

        // 光标移动对应的角度变化（弧度）
        float m_cursor_delta_yaw {0.0f};
        float m_cursor_delta_pitch {0.0f};

        void         resetGameCommand() { m_game_command = 0; }
        unsigned int getGameCommand() const { return m_game_command; }
        
        // 获取光标移动对应的角度变化（弧度）
        float getCursorDeltaYaw() const { return m_cursor_delta_yaw; }
        float getCursorDeltaPitch() const { return m_cursor_delta_pitch; }
        
        // 计算光标增量角度（公有函数，用于调试和手动更新）
        void calculateCursorDeltaAngles();
        
        // 摄像机状态管理
        void updateCameraState(float delta_time);
        glm::vec3 getCameraPosition() const { return m_camera_position; }
        glm::quat getCameraRotation() const { return m_camera_rotation; }
        glm::mat4 getCameraViewMatrix() const;
        
        // 摄像机控制参数
        static constexpr float CAMERA_MOVE_SPEED = 1.0f;
        static constexpr float CAMERA_SPRINT_MULTIPLIER = 3.0f;
        static constexpr float CAMERA_MOUSE_SENSITIVITY = 0.1f;

    private:
        void onKeyInGameMode(int key, int scancode, int action, int mods);
        void processCameraMovement(float delta_time);
        void processCameraRotation();

        unsigned int m_game_command {0};

        int m_last_cursor_x {0};
        int m_last_cursor_y {0};
        
        // 摄像机状态
        glm::vec3 m_camera_position {0.0f, 0.0f, 3.0f};
        glm::quat m_camera_rotation {1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
        
        // 累积的角度变化（不会被清除，用于持续旋转）
        float m_accumulated_yaw {0.0f};
        float m_accumulated_pitch {0.0f};
    };
} // namespace Elish
