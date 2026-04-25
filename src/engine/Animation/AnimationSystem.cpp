#include "AnimationSystem.h"
#include "Animation.h"
#include "Renderer/ResourceManager.h"
#include "Scene/Components/AnimationComponent.h"
#include "Scene/Components/SpriteComponent.h"
#include "Core/Log/Log.h"
#include "Core/Profiler/Profiler.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

void AnimationSystem::setState(entt::registry& reg, entt::entity e, StateID newState) {
    auto* anim = reg.try_get<AnimationComponent>(e);
    if (!anim) return;
    if (anim->state == newState) return;   // already in that state — keep timing
    anim->state = newState;
    anim->time  = 0.0f;
}

bool AnimationSystem::trigger(entt::registry& reg, entt::entity e,
                              const ResourceManager& resources, TriggerID trig) {
    auto* anim = reg.try_get<AnimationComponent>(e);
    if (!anim) return false;

    const AnimationSet* set = resources.getAnimSet(anim->set);
    if (!set) return false;

    auto stateIt = set->states.find(anim->state);
    if (stateIt == set->states.end()) return false;

    for (const auto& tr : stateIt->second.transitions) {
        if (tr.trigger == trig) {
            // Resolve to setState semantics so 'time' reset and idempotency
            // logic stay in one place.
            setState(reg, e, tr.target);
            return true;
        }
    }
    return false;
}

void AnimationSystem::update(entt::registry& reg, const ResourceManager& resources, float dt) {
    FP_PROFILE_SCOPE("AnimationSystem::update");

    auto view = reg.view<AnimationComponent, SpriteComponent>();
    for (auto e : view) {
        auto& a = view.get<AnimationComponent>(e);
        auto& s = view.get<SpriteComponent>(e);

        const AnimationSet* set = resources.getAnimSet(a.set);
        if (!set) continue;   // entity references an unknown set — skip silently

        // Resolve the current state. Fall back to the set's default if the
        // entity's state is missing (typo, deleted state, etc.) so we still
        // show something instead of crashing.
        auto stateIt = set->states.find(a.state);
        if (stateIt == set->states.end()) {
            stateIt = set->states.find(set->defaultState);
            if (stateIt == set->states.end()) continue;
            a.state = set->defaultState;
            a.time  = 0.0f;
        }
        const AnimationState& st = stateIt->second;

        const Animation* track = resources.getAnimation(st.animation);
        if (!track || track->frames.empty()) continue;

        if (!a.paused) a.time += dt;

        const float frameDuration = (track->fps > 0.0f) ? (1.0f / track->fps) : 0.0f;
        const float totalDuration = frameDuration * static_cast<float>(track->frames.size());

        size_t frameIndex = 0;
        if (frameDuration > 0.0f) {
            if (st.loop) {
                // Wrap: keep within [0, totalDuration). std::fmod handles
                // arbitrarily large times if a paused entity is later
                // unpaused after a long delta.
                const float wrapped = std::fmod(a.time, totalDuration);
                frameIndex = static_cast<size_t>(wrapped / frameDuration);
            } else {
                if (a.time >= totalDuration) {
                    // One-shot completed.
                    if (st.next) {
                        a.state = *st.next;
                        a.time  = 0.0f;
                        // Re-resolve next state's first frame this same tick
                        // so we don't show a stale frame for one update.
                        auto nIt = set->states.find(a.state);
                        if (nIt != set->states.end()) {
                            const Animation* nextTrack =
                                resources.getAnimation(nIt->second.animation);
                            if (nextTrack && !nextTrack->frames.empty()) {
                                s.sprite = nextTrack->frames.front();
                            }
                        }
                        continue;
                    }
                    // No 'next' specified — clamp to last frame and hold.
                    frameIndex = track->frames.size() - 1;
                } else {
                    frameIndex = static_cast<size_t>(a.time / frameDuration);
                }
            }
        }
        frameIndex = std::min(frameIndex, track->frames.size() - 1);

        s.sprite = track->frames[frameIndex];
    }
}

} // namespace engine
