#pragma once
#include "Renderer/ResourceTypes.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

// Immutable description of one particle effect kind. Shared by every
// emitter referencing this system (each emitter still has its own GPU
// pool — see VfxSystem). Authored in the manifest's "particle_systems"
// section, resolved by name to a ParticleSystemID.
struct ParticleSystem {
    // Pool size for this system. Each system gets its own GPU SSBO sized
    // to maxParticles, so small effects (hit sparks) don't pay for big
    // ones (smoke). Capped on the engine side at a sane upper bound.
    uint32_t   maxParticles = 256;

    // Spawn rate for *persistent* emitters (entities with
    // ParticleEmitterComponent). One-shot effects ignore this and emit
    // a fixed burst at spawn — see VfxSystem::spawnOnce.
    float      emitRate = 16.0f;            // particles per second
    uint32_t   burstCount = 0;               // optional one-shot burst on emitter start

    // Per-particle randomised lifetime, world-space initial velocity,
    // gravity. Sampled uniformly at spawn time.
    float      lifetimeMin = 1.0f;
    float      lifetimeMax = 1.0f;
    glm::vec2  velocityMin{ 0.0f, 0.0f };
    glm::vec2  velocityMax{ 0.0f, 0.0f };
    glm::vec2  gravity{ 0.0f, 0.0f };

    // Visual envelope, lerped by (time_alive / lifetime).
    float      sizeStart  = 0.5f;
    float      sizeEnd    = 0.0f;
    glm::vec4  colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4  colorEnd  { 1.0f, 1.0f, 1.0f, 0.0f };

    // Sprite sampled per particle. Resolved by VfxSystem at render time
    // to a (Texture, UV rect) pair.
    SpriteID   sprite;
};

// GPU-side particle layout. Exact match to the SSBO struct in
// vfx_simulate.comp / vfx_render.vert — keep them in sync. Aligned to
// 16 bytes for std430 sanity.
//
// 'alive' is 0/1 packed as uint32 (vec4 alignment is friendlier than
// bool for std430). The compute pass writes 0 when lifetime is reached;
// the render pass collapses dead particles to a degenerate quad.
struct alignas(16) GpuParticle {
    glm::vec2 position;
    glm::vec2 velocity;

    glm::vec4 colorStart;
    glm::vec4 colorEnd;

    float     lifetime;
    float     timeAlive;
    float     sizeStart;
    float     sizeEnd;

    glm::vec2 gravity;
    uint32_t  alive;
    uint32_t  _pad0;
};
static_assert(sizeof(GpuParticle) == 80, "GpuParticle must match shader std430 layout");

// CPU-side spawn request the simulate pass injects into the pool.
// Mirrored as 'SpawnRequest' in vfx_spawn.comp.
struct alignas(16) GpuSpawnRequest {
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 colorStart;
    glm::vec4 colorEnd;
    float     lifetime;
    float     sizeStart;
    float     sizeEnd;
    uint32_t  _pad0;
    glm::vec2 gravity;
    glm::vec2 _pad1;
};
static_assert(sizeof(GpuSpawnRequest) == 80, "GpuSpawnRequest layout mismatch");

} // namespace engine
