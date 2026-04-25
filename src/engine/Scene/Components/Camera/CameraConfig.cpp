#include "CameraConfig.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace engine {

CameraConfig loadCameraConfig(const std::string& path) {
    CameraConfig cfg;  // start from built-in defaults

    std::ifstream file(path);
    FP_CORE_ASSERT(file.is_open(), "Cannot open camera config: {}", path);

    nlohmann::json doc = nlohmann::json::parse(file);

    // Override only the fields present in the file.
    cfg.zoom      = doc.value("zoom",      cfg.zoom);
    cfg.nearPlane = doc.value("nearPlane", cfg.nearPlane);
    cfg.farPlane  = doc.value("farPlane",  cfg.farPlane);
    cfg.panSpeed  = doc.value("panSpeed",  cfg.panSpeed);
    cfg.zoomStep  = doc.value("zoomStep",  cfg.zoomStep);
    cfg.zoomMin   = doc.value("zoomMin",   cfg.zoomMin);
    cfg.zoomMax   = doc.value("zoomMax",   cfg.zoomMax);

    FP_CORE_INFO("CameraConfig loaded from {} (zoom={:.2f}, pan={:.2f}, step={:.2f})",
                 path, cfg.zoom, cfg.panSpeed, cfg.zoomStep);
    return cfg;
}

} // namespace engine
