#pragma once
#include "Scene/Systems/System.h"
#include "Core/Events/Subscription.h"

namespace engine {

class EventBus;
class InputMap;

// Translates InputMap actions into transform/zoom updates for the entity
// carrying ActiveCameraTag + CameraComponent2D + TransformComponent.
//
// Pan uses polling (continuous while keys are held) in update(realDt).
// Zoom uses events (discrete ticks from the mouse wheel).
class CameraController final : public System {
public:
    void init(entt::registry& registry, InputMap& input, EventBus& bus);
    void update(entt::registry& registry, float realDt) override;
    void shutdown(entt::registry& registry) override;

private:
    InputMap*    m_input = nullptr;
    Subscription m_actionSub;
};

} // namespace engine
