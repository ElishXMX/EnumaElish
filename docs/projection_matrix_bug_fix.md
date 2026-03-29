# Projection Matrix Aspect Ratio Bug Fix

## Problem Description

When the camera rotates left/right (yaw rotation), the rendered model position does not stay in the correct location, while the physics collider debug visualization remains correct. The pitch rotation (up/down) works correctly after previous fix.

## Root Cause

The UBO (Uniform Buffer Object) update was using the **entire window's aspect ratio** (`swapchainInfo.extent`), while the collider debug drawing was using the **scene viewport's aspect ratio** (`layout_state.sceneViewport`). This caused the projection matrices to be different, resulting in horizontal position offset during camera yaw rotation.

### Technical Details

**UBO Projection Matrix**: `proj[0]: (1.358, ...)` - using entire window aspect ratio
**Collider Projection Matrix**: `proj[0]: (1.980, ...)` - using scene viewport aspect ratio

The difference in aspect ratio caused the rendered model to appear at a different horizontal position than the physics collider.

## Reproduction Steps

1. Enable "Show Colliders" checkbox in the UI
2. Rotate camera left/right (yaw rotation) using mouse drag
3. Observe that the rendered model shifts horizontally relative to the collider debug box
4. The pitch rotation (up/down) works correctly

## Solution

Modified `MainCameraPass::updateUniformBuffer()` to use the scene viewport's aspect ratio instead of the entire window's aspect ratio:

```cpp
// Get scene viewport dimensions (consistent with collider drawing)
auto render_pipeline_base = g_runtime_global_context.m_render_system ? 
    g_runtime_global_context.m_render_system->getRenderPipeline() : nullptr;

float aspectRatio = 16.0f / 9.0f;  // Default aspect ratio

if (render_pipeline_base) {
    auto render_pipeline = std::dynamic_pointer_cast<RenderPipeline>(render_pipeline_base);
    if (render_pipeline) {
        auto layout_state = render_pipeline->getEditorLayoutState();
        float viewport_width = layout_state.sceneViewport.width;
        float viewport_height = layout_state.sceneViewport.height;
        if (viewport_width > 1.0f && viewport_height > 1.0f) {
            aspectRatio = viewport_width / viewport_height;
        }
    }
}

// Set camera aspect ratio (using scene viewport aspect ratio)
m_camera->setAspect(aspectRatio);
```

## Files Modified

- `engine/runtime/render/passes/main_camera_pass.cpp` - Updated `updateUniformBuffer()` function

## Verification

After the fix:
- **UBO Projection Matrix**: `proj[0]: (1.980, ...)` 
- **Collider Projection Matrix**: `proj[0]: (1.980, ...)`

Both matrices are now identical, ensuring the rendered model and physics collider are perfectly synchronized during camera rotation.

## Related Issues

- Camera pitch rotation fix (Y-axis flip correction)
- Camera yaw rotation axis correction (Z-axis to Y-axis)

## Date

2026-03-29
