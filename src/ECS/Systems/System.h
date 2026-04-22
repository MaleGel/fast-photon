#pragma once
#include <entt/entt.hpp>

namespace engine {

class System {
public:
    virtual ~System() = default;

    virtual void init(entt::registry& registry)     {}
    virtual void update(entt::registry& registry, float dt) = 0;
    virtual void shutdown(entt::registry& registry) {}
};

} // namespace engine
