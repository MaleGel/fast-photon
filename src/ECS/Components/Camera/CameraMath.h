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

} // namespace engine
