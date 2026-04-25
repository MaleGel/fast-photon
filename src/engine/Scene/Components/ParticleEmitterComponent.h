#pragma once
#include "Renderer/ResourceTypes.h"

namespace engine {

// Persistent emitter attached to an entity. VfxSystem ticks every entity
// with this component and spawns particles into the named system's pool
// based on its emitRate.
//
// One-shot effects (explosions, hit sparks) don't go through this
// component — they're fired ad-hoc via VfxSystem::spawnOnce or the
// vfx.spawn Lua binding (Step 2).
struct ParticleEmitterComponent {
    ParticleSystemID system;
    bool             active        = true;
    float            timeAccum     = 0.0f;   // seconds accumulated toward the next spawn
    bool             firedBurst    = false;  // flips true after the optional initial burst runs
};

} // namespace engine
