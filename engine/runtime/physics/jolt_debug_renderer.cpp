/**
 * @file jolt_debug_renderer.cpp
 * @brief Jolt Physics 调试渲染器实现
 */
#include "jolt_debug_renderer.h"
#include "../core/base/macro.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef JPH_DEBUG_RENDERER

namespace Elish
{
    JoltDebugRenderer::JoltDebugRenderer()
        : DebugRendererSimple()
    {
        LOG_INFO("[JoltDebugRenderer] Initialized");
    }

    JoltDebugRenderer::~JoltDebugRenderer()
    {
        clearDebugData();
    }

    void JoltDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
    {
        DebugLine line;
        line.start = glm::vec3(inFrom.GetX(), inFrom.GetY(), inFrom.GetZ());
        line.end = glm::vec3(inTo.GetX(), inTo.GetY(), inTo.GetZ());
        line.color = colorToABGR(inColor);
        m_debug_lines.push_back(line);
    }

    void JoltDebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                                          JPH::ColorArg inColor, ECastShadow inCastShadow)
    {
        DebugTriangle tri;
        tri.v0 = glm::vec3(inV1.GetX(), inV1.GetY(), inV1.GetZ());
        tri.v1 = glm::vec3(inV2.GetX(), inV2.GetY(), inV2.GetZ());
        tri.v2 = glm::vec3(inV3.GetX(), inV3.GetY(), inV3.GetZ());
        tri.color = colorToABGR(inColor);
        m_debug_triangles.push_back(tri);

        DebugRendererSimple::DrawTriangle(inV1, inV2, inV3, inColor, inCastShadow);
    }

    void JoltDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view &inString,
                                        JPH::ColorArg inColor, float inHeight)
    {
    }

    void JoltDebugRenderer::setViewProjection(const glm::mat4& view_matrix, const glm::mat4& proj_matrix,
                                               float viewport_width, float viewport_height)
    {
        m_view_matrix = view_matrix;
        m_proj_matrix = proj_matrix;
        m_viewport_width = viewport_width;
        m_viewport_height = viewport_height;
    }

    void JoltDebugRenderer::clearDebugData()
    {
        m_debug_lines.clear();
        m_debug_triangles.clear();
    }

    bool JoltDebugRenderer::worldToScreen(const glm::vec3& world_pos, float& out_screen_x, float& out_screen_y)
    {
        glm::vec4 clip_pos = m_proj_matrix * m_view_matrix * glm::vec4(world_pos, 1.0f);
        
        if (clip_pos.w <= 0.0001f)
        {
            return false;
        }
        
        glm::vec3 ndc_pos = glm::vec3(clip_pos) / clip_pos.w;
        
        out_screen_x = (ndc_pos.x + 1.0f) * 0.5f * m_viewport_width;
        out_screen_y = (1.0f - ndc_pos.y) * 0.5f * m_viewport_height;
        
        return true;
    }

    uint32_t JoltDebugRenderer::colorToABGR(JPH::ColorArg inColor)
    {
        uint32_t r = static_cast<uint32_t>(inColor.r * 255.0f);
        uint32_t g = static_cast<uint32_t>(inColor.g * 255.0f);
        uint32_t b = static_cast<uint32_t>(inColor.b * 255.0f);
        uint32_t a = static_cast<uint32_t>(inColor.a * 255.0f);
        
        return (a << 24) | (b << 16) | (g << 8) | r;
    }
}

#endif // JPH_DEBUG_RENDERER
