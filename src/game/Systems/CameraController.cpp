#include "CameraController.h"
#include "Platform/Input/InputMap.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"

#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"

#include <algorithm>

namespace engine {

// Action IDs this controller reacts to. Matches assets/data/input_bindings.json.
static const ActionID kPanLeft  ("camera.pan_left");
static const ActionID kPanRight ("camera.pan_right");
static const ActionID kPanUp    ("camera.pan_up");
static const ActionID kPanDown  ("camera.pan_down");
static const ActionID kZoomIn   ("camera.zoom_in");
static const ActionID kZoomOut  ("camera.zoom_out");

void CameraController::init(entt::registry& registry, InputMap& input, EventBus& bus) {
    m_input = &input;

    // Zoom is discrete (one tick per mouse-wheel unit) — handle as events.
    // Pan is continuous — handled in update() via isActionDown().
    m_actionSub = bus.subscribe<ActionTriggeredEvent>(
        [&registry](const ActionTriggeredEvent& e) {
            if (e.action != kZoomIn && e.action != kZoomOut) return;

            auto view = registry.view<ActiveCameraTag, CameraComponent2D>();
            for (auto entity : view) {
                auto& cam = view.get<CameraComponent2D>(entity);
                // zoomStep < 1.0 zooms in, > 1.0 zooms out. Invert for ZoomOut.
                const float factor = (e.action == kZoomIn)
                    ? cam.zoomStep
                    : 1.0f / cam.zoomStep;
                cam.zoom = std::clamp(cam.zoom * factor, cam.zoomMin, cam.zoomMax);
                break;
            }
        });

    FP_CORE_INFO("CameraController initialized");
}

void CameraController::shutdown(entt::registry& /*registry*/) {
    m_actionSub.release();
    m_input = nullptr;
}

void CameraController::update(entt::registry& registry, float realDt) {
    if (!m_input) return;

    auto view = registry.view<ActiveCameraTag, TransformComponent, CameraComponent2D>();
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& cam       = view.get<CameraComponent2D>(entity);

        float dx = 0.0f;
        float dy = 0.0f;
        if (m_input->isActionDown(kPanLeft))  dx -= 1.0f;
        if (m_input->isActionDown(kPanRight)) dx += 1.0f;
        // Vulkan clip space: +Y goes down on screen. "Pan up" (camera looks
        // further up on the map) moves the camera's world Y downward.
        if (m_input->isActionDown(kPanUp))    dy -= 1.0f;
        if (m_input->isActionDown(kPanDown))  dy += 1.0f;

        if (dx != 0.0f || dy != 0.0f) {
            // Scale with zoom so the feel stays the same when zoomed out.
            const float speed = cam.panSpeed * cam.zoom;
            transform.position.x += dx * speed * realDt;
            transform.position.y += dy * speed * realDt;
        }

        break;  // only the first ActiveCameraTag entity
    }
}

} // namespace engine
