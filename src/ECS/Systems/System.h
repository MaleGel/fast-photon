#pragma once
#include <entt/entt.hpp>

namespace engine {

// Base class for ECS systems.
//
// Two update hooks serve different time domains:
//   * fixedUpdate(registry, fixedDt) — runs 0..N times per frame with a
//     constant dt. Use for deterministic simulation: movement, AI, physics.
//   * update(registry, realDt)      — runs exactly once per frame with the
//     real elapsed time. Use for presentation: animation blending, UI,
//     debug overlays.
//
// Default impls are empty, so systems only override what they actually need.
class System {
public:
    virtual ~System() = default;

    virtual void init(entt::registry& registry)                         {}
    virtual void fixedUpdate(entt::registry& registry, float fixedDt)   {}
    virtual void update     (entt::registry& registry, float realDt)    {}
    virtual void shutdown(entt::registry& registry)                     {}
};

} // namespace engine
