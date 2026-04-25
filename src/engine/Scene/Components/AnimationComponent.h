#pragma once
#include "Renderer/ResourceTypes.h"

namespace engine {

// Drives a SpriteComponent through frames of a state machine.
//
// AnimationSystem reads (set, state) and the elapsed 'time' to pick the
// current frame, then writes that sprite id into the entity's
// SpriteComponent each tick. State changes happen externally via
// AnimationSystem::setState — gameplay code calls it; this component
// stores no transition logic of its own.
struct AnimationComponent {
    AnimationSetID set;       // which state machine (warriors, archers, ...)
    StateID        state;     // current state name (set by setState / auto 'next')
    float          time = 0;  // seconds spent in this state, capped per state
    bool           paused = false;
};

} // namespace engine
