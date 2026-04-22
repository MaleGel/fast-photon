#pragma once

namespace engine {

// 2D orthographic camera. Position lives in TransformComponent.
// 'zoom' is half the visible height in world units — smaller = closer.
struct CameraComponent2D {
    float zoom        = 4.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane   = -1.0f;
    float farPlane    =  1.0f;

    // Camera-controller tuning. Kept on the component so each camera can
    // have its own feel (editor vs gameplay camera).
    float panSpeed    = 6.0f;   // world units per second while pan action is held
    float zoomStep    = 0.9f;   // multiplier per zoom tick ( <1 zooms in )
    float zoomMin     = 1.0f;
    float zoomMax     = 20.0f;
};

} // namespace engine
