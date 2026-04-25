#include "AnimationSystem.h"
#include "Animation.h"
#include "Renderer/ResourceManager.h"
#include "Scene/Components/AnimationComponent.h"
#include "Scene/Components/SpriteComponent.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Job/JobSystem.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

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

// Per-entity update body. Pulled out as a free function so JobSystem's
// parallel_for can call it on any worker thread. Each invocation only
// reads `resources` (immutable for the frame) and writes the entity's
// own AnimationComponent + SpriteComponent — no shared mutable state.
static void tickAnimationFor(entt::registry& reg, entt::entity e,
                             const ResourceManager& resources, float dt) {
    auto& a = reg.get<AnimationComponent>(e);
    auto& s = reg.get<SpriteComponent>(e);

    const AnimationSet* set = resources.getAnimSet(a.set);
    if (!set) return;

    auto stateIt = set->states.find(a.state);
    if (stateIt == set->states.end()) {
        stateIt = set->states.find(set->defaultState);
        if (stateIt == set->states.end()) return;
        a.state = set->defaultState;
        a.time  = 0.0f;
    }
    const AnimationState& st = stateIt->second;

    const Animation* track = resources.getAnimation(st.animation);
    if (!track || track->frames.empty()) return;

    if (!a.paused) a.time += dt;

    const float frameDuration = (track->fps > 0.0f) ? (1.0f / track->fps) : 0.0f;
    const float totalDuration = frameDuration * static_cast<float>(track->frames.size());

    size_t frameIndex = 0;
    if (frameDuration > 0.0f) {
        if (st.loop) {
            const float wrapped = std::fmod(a.time, totalDuration);
            frameIndex = static_cast<size_t>(wrapped / frameDuration);
        } else {
            if (a.time >= totalDuration) {
                if (st.next) {
                    a.state = *st.next;
                    a.time  = 0.0f;
                    auto nIt = set->states.find(a.state);
                    if (nIt != set->states.end()) {
                        const Animation* nextTrack =
                            resources.getAnimation(nIt->second.animation);
                        if (nextTrack && !nextTrack->frames.empty()) {
                            s.sprite = nextTrack->frames.front();
                        }
                    }
                    return;
                }
                frameIndex = track->frames.size() - 1;
            } else {
                frameIndex = static_cast<size_t>(a.time / frameDuration);
            }
        }
    }
    frameIndex = std::min(frameIndex, track->frames.size() - 1);
    s.sprite = track->frames[frameIndex];
}

void AnimationSystem::update(entt::registry& reg, const ResourceManager& resources, float dt) {
    FP_PROFILE_SCOPE("AnimationSystem::update");

    // Snapshot entity ids so parallel_for can index them. entt views are
    // iterator-based and can't be sliced. The snapshot cost is one
    // pointer-and-int per entity — negligible compared to per-tick work
    // even for thousands of entities.
    auto view = reg.view<AnimationComponent, SpriteComponent>();
    std::vector<entt::entity> entities;
    entities.reserve(view.size_hint());
    for (auto e : view) entities.push_back(e);

    if (entities.empty()) return;

    // Threshold: parallel dispatch carries fixed overhead (mutex,
    // condvar wakeup, cache-line bouncing on the atomic counter). For
    // tiny entity counts that's measurably more expensive than the
    // work itself. 64 is a conservative line in the sand — well below
    // it sequential always wins.
    constexpr size_t kParallelThreshold = 64;
    if (entities.size() < kParallelThreshold) {
        for (auto e : entities) tickAnimationFor(reg, e, resources, dt);
        return;
    }

    // Each iteration only writes its own entity's components, so parallel
    // execution is safe. parallel_for picks a chunk size that keeps
    // workers busy without over-fragmenting.
    auto handle = JobSystem::parallel_for(0, entities.size(),
        [&entities, &reg, &resources, dt](size_t i) {
            tickAnimationFor(reg, entities[i], resources, dt);
        });
    handle.wait();
}

} // namespace engine
