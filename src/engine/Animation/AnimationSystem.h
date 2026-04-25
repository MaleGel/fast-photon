#pragma once
#include "Renderer/ResourceTypes.h"

#include <entt/fwd.hpp>

namespace engine {

class ResourceManager;

// Per-frame animation update.
//
// For each entity with both AnimationComponent and SpriteComponent:
//   1. advance the component's 'time' by dt (unless paused),
//   2. resolve current frame from the active animation,
//   3. write the corresponding SpriteID into SpriteComponent,
//   4. if the animation finished and is non-looping, jump to the state's
//      'next' (when present) — wrapping back to the start of the new state.
class AnimationSystem {
public:
    static void update(entt::registry& reg, const ResourceManager& resources, float dt);

    // Switch an entity to a new state. Idempotent: repeatedly calling with
    // the same state is a no-op so callers don't have to track the previous
    // state themselves. Resets 'time' on a real transition.
    static void setState(entt::registry& reg, entt::entity e, StateID newState);

    // Fire a named trigger. The system looks up the trigger in the current
    // state's transition table and, if a match is found, transitions to
    // the target state (resetting 'time'). Unknown triggers are silently
    // ignored — game code can fire triggers indiscriminately and rely on
    // the state machine to filter what makes sense in the current state.
    // Returns true if a transition fired.
    static bool trigger(entt::registry& reg, entt::entity e,
                        const ResourceManager& resources, TriggerID trig);
};

} // namespace engine
