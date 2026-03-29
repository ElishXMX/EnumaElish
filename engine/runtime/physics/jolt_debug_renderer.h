/**
 * @file jolt_debug_renderer.h
 * @brief Jolt Physics 调试渲染器 - 使用 ImGui 绘制碰撞体
 * 
 * 继承 Jolt Physics 的 DebugRendererSimple 类，
 * 使用 ImGui 的后台绘制功能来渲染碰撞体的线框
 */
#pragma once

#include <Jolt/Jolt.h>

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Renderer/DebugRendererSimple.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

namespace Elish
{
    class RenderCamera;

    /**
     * @brief Jolt Physics 调试渲染器
     * 
     * 实现 Jolt Physics 的 DebugRendererSimple 接口，
     * 使用 ImGui 的 GetBackgroundDrawList() 绘制碰撞体线框
     */
    class JoltDebugRenderer : public JPH::DebugRendererSimple
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        /**
         * @brief 线段数据结构
         */
        struct DebugLine
        {
            glm::vec3 start;
            glm::vec3 end;
            uint32_t  color;
        };

        /**
         * @brief 三角形数据结构
         */
        struct DebugTriangle
        {
            glm::vec3 v0;
            glm::vec3 v1;
            glm::vec3 v2;
            uint32_t  color;
        };

        JoltDebugRenderer();
        virtual ~JoltDebugRenderer();

        /**
         * @brief 绘制线段
         * @param inFrom 起点
         * @param inTo 终点
         * @param inColor 颜色
         */
        virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

        /**
         * @brief 绘制三角形（线框模式）
         * @param inV1 顶点1
         * @param inV2 顶点2
         * @param inV3 顶点3
         * @param inColor 颜色
         * @param inCastShadow 是否投射阴影（忽略）
         */
        virtual void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, 
                                  JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) override;

        /**
         * @brief 绘制3D文本（暂不实现）
         * @param inPosition 位置
         * @param inString 文本内容
         * @param inColor 颜色
         * @param inHeight 高度
         */
        virtual void DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view &inString, 
                                JPH::ColorArg inColor = JPH::Color::sWhite, float inHeight = 0.5f) override;

        /**
         * @brief 设置视图投影矩阵
         * @param view_matrix 视图矩阵
         * @param proj_matrix 投影矩阵
         * @param viewport_width 视口宽度
         * @param viewport_height 视口高度
         */
        void setViewProjection(const glm::mat4& view_matrix, const glm::mat4& proj_matrix,
                               float viewport_width, float viewport_height);

        /**
         * @brief 清空调试绘制数据
         */
        void clearDebugData();

        /**
         * @brief 获取调试线段数据
         */
        const std::vector<DebugLine>& getDebugLines() const { return m_debug_lines; }

        /**
         * @brief 获取调试三角形数据
         */
        const std::vector<DebugTriangle>& getDebugTriangles() const { return m_debug_triangles; }

        /**
         * @brief 获取绘制的线段数量
         */
        size_t getLineCount() const { return m_debug_lines.size(); }

        /**
         * @brief 获取绘制的三角形数量
         */
        size_t getTriangleCount() const { return m_debug_triangles.size(); }

    private:
        /**
         * @brief 世界坐标转屏幕坐标
         * @param world_pos 世界坐标
         * @param out_screen_x 输出屏幕X坐标
         * @param out_screen_y 输出屏幕Y坐标
         * @return 是否在屏幕内
         */
        bool worldToScreen(const glm::vec3& world_pos, float& out_screen_x, float& out_screen_y);

        /**
         * @brief Jolt颜色转换为ABGR uint32
         */
        static uint32_t colorToABGR(JPH::ColorArg inColor);

    private:
        std::vector<DebugLine>      m_debug_lines;
        std::vector<DebugTriangle>  m_debug_triangles;

        glm::mat4                   m_view_matrix = glm::mat4(1.0f);
        glm::mat4                   m_proj_matrix = glm::mat4(1.0f);
        float                       m_viewport_width = 1920.0f;
        float                       m_viewport_height = 1080.0f;
    };
}

#endif // JPH_DEBUG_RENDERER
