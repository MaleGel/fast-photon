#pragma once
#include <string>

namespace engine {

// Disk-side defaults for a camera. Loaded once at scene setup and copied
// into CameraComponent2D on the entity that should use them. Kept separate
// from the component so future debug/authoring fields (e.g. clear color,
// gizmo visibility) don't bloat the runtime component.
struct CameraConfig {
    float zoom      = 4.0f;
    float nearPlane = -1.0f;
    float farPlane  =  1.0f;

    float panSpeed  = 6.0f;
    float zoomStep  = 0.9f;
    float zoomMin   = 1.0f;
    float zoomMax   = 20.0f;
};

// Read a CameraConfig from JSON. Missing fields fall back to the defaults
// above, so shortened config files remain valid.
CameraConfig loadCameraConfig(const std::string& path);

} // namespace engine
