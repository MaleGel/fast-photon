#pragma once
#include "Renderer/ResourceTypes.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace engine {

// One animation track — an ordered list of sprite frames played at a fixed
// rate. The data is purely descriptive; runtime cursor lives on the entity
// (AnimationComponent), so the same Animation can drive any number of
// entities concurrently.
struct Animation {
    std::vector<SpriteID> frames;
    float                 fps = 8.0f;
};

// A named transition out of a state. Fired explicitly by gameplay via
// AnimationSystem::trigger(); the state machine looks up the trigger
// against the current state's table and switches if a match exists.
struct AnimationTransition {
    TriggerID trigger;
    StateID   target;
};

// One state inside a state machine: which animation to play, whether it
// loops, and (for one-shots) which state the system should auto-transition
// into when the animation ends. 'transitions' is the trigger-driven
// alternative — works for both looping and one-shot states, and is
// independent of 'next'.
struct AnimationState {
    AnimationID                      animation;
    bool                             loop = true;
    std::optional<StateID>           next;          // only meaningful when loop = false
    std::vector<AnimationTransition> transitions;   // may be empty
};

// A bundle of states keyed by name, plus a default state to start in.
// Typically one set per character archetype (warrior, archer, ...).
struct AnimationSet {
    std::unordered_map<StateID, AnimationState> states;
    StateID                                     defaultState;
};

} // namespace engine
