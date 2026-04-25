#pragma once
#include "Scene/Systems/System.h"

namespace engine {

class RenderSystem final : public System {
public:
    void update(entt::registry& registry, float dt) override;
};

} // namespace engine
