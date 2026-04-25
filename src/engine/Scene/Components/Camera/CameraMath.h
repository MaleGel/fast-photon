#pragma once
#include "CameraComponent2D.h"
#include "../TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

// Build the combined view*projection matrix for a 2D orthographic camera.
// The camera is placed in world-space by 'transform.position' (xy only),
// and the orthographic frustum size is derived from 'cam.zoom' and aspect.
inline glm::mat4 buildViewProjection(const TransformComponent& transform,
                                     const CameraComponent2D&  cam) {
    const float halfH = cam.zoom;
    const float halfW = cam.zoom * cam.aspectRatio;

    // Orthographic projection — linear mapping of a world-space rectangle
    // into clip space [-1..1]. GLM writes into a right-handed, z [-1..1] mat4;
    // we set near/far to our 2D defaults.
    glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH,
                                cam.nearPlane, cam.farPlane);

    // View = inverse of camera world transform. For a 2D translation-only
    // camera this is just negate-translate.
    glm::mat4 view = glm::translate(glm::mat4(1.0f),
                                    glm::vec3(-transform.position.x,
                                              -transform.position.y,
                                              0.0f));

    return proj * view;
}

// Convert a screen-pixel position (origin = top-left of the window) into
// world-space coordinates using the given 2D camera.
//
// Math:
//   1. Screen pixels → NDC [-1..1].
//      NDC.y grows downward to match Vulkan's clip space (our ortho() sets
//      top=-halfH, bottom=+halfH, so +Y points the same way on screen and
//      in world), so no manual flip is needed.
//   2. NDC → world by applying the inverse of the view-projection matrix.
//      For a purely orthographic camera the inverse is well-defined and
//      cheap to compute — GLM caches nothing, but this is one matrix
//      invert per query, not per-frame-per-pixel.
inline glm::vec2 screenToWorld(const glm::vec2&          screenPos,
                               const TransformComponent& transform,
                               const CameraComponent2D&  cam,
                               uint32_t                  screenWidth,
                               uint32_t                  screenHeight) {
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    if (w <= 0.0f || h <= 0.0f) return { 0.0f, 0.0f };

    // Pixel → NDC. Both axes are mapped the same way: 0 → -1, size → +1.
    const float ndcX = (2.0f * screenPos.x / w) - 1.0f;
    const float ndcY = (2.0f * screenPos.y / h) - 1.0f;

    // Undo the camera's view-projection to land back in world space.
    const glm::mat4 vp    = buildViewProjection(transform, cam);
    const glm::mat4 invVp = glm::inverse(vp);
    const glm::vec4 world = invVp * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    return { world.x, world.y };
}

} // namespace engine
