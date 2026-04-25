#pragma once
#include "ParticleSystem.h"
#include "Renderer/ResourceTypes.h"
#include "Renderer/Pipeline.h"
#include "Renderer/ComputePipeline.h"
#include "Renderer/VertexBuffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <entt/fwd.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <unordered_map>
#include <vector>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class ResourceManager;
class DescriptorAllocator;
class RenderQueue;
class EventBus;

// Per-frame VFX driver. Owns one GPU pool (SSBO) per ParticleSystem
// resource, plus the compute pipelines that spawn into and simulate
// over those pools. Renders particles in the scene render pass via
// instanced billboard draws.
//
// Frame flow:
//   1. tickEmitters(dt)      — CPU walks every entity with a
//                              ParticleEmitterComponent and pushes
//                              spawn requests into per-system queues.
//   2. recordCompute(cmd)    — uploads per-system spawn buffers, runs
//                              the spawn compute pass (1 thread per
//                              spawn), runs simulate (1 thread per
//                              particle in the pool). Run between
//                              scene and swap render passes from
//                              FrameRenderer.
//   3. submit(queue, ...)    — enqueues one RenderCommand per system
//                              with at least one alive particle. The
//                              backend dispatch (executeBatch) does
//                              the instanced draws.
class VfxSystem final : public IRenderBackend {
public:
    void init(VulkanContext& ctx, RenderPass& sceneRenderPass,
              ResourceManager& resources, DescriptorAllocator& descriptors,
              EventBus& bus);
    void shutdown(VulkanContext& ctx);

    // CPU-side fire-and-forget API for one-shot effects. Pushes a
    // burst of particles into the matching pool right now. Safe to
    // call from any place that has access to the VfxSystem (game
    // code, Lua bindings, event handlers).
    //
    // 'count' caps to avoid blowing the spawn queue in one frame.
    void spawnOnce(ParticleSystemID systemId, glm::vec2 position,
                   uint32_t count);

    // Tick CPU-side emitters: accumulate emit_rate, push spawn
    // requests for every entity with ParticleEmitterComponent.
    void tickEmitters(entt::registry& reg, float dt);

    // Record GPU compute: upload spawn buffers, dispatch spawn +
    // simulate. Called between scene and swap render passes (where
    // it sits outside any render pass, like BrightExtractPass).
    void recordCompute(VkCommandBuffer cmd, float dt);

    // Render: submit one RenderCommand per system that has spawns
    // queued or live particles. Layer placement is fixed; tweak via
    // ParticleSystem fields later if needed.
    void submit(RenderQueue& queue, const Swapchain& swapchain,
                entt::registry& registry);

    // IRenderBackend — drains all queued VFX commands; executes one
    // instanced draw per system.
    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    // Per-system GPU resources. Created on demand the first time we
    // see an emitter (or spawnOnce) reference its system; reuses the
    // ParticleSystem definition's maxParticles for sizing.
    struct Pool {
        VkBuffer       particleBuf      = VK_NULL_HANDLE;
        VmaAllocation  particleAlloc    = VK_NULL_HANDLE;

        // Spawn ring + counter. spawnBuf is host-visible so the CPU
        // can fill it each frame without staging; counter is GPU-only,
        // a uint head index plus padding for std430.
        VkBuffer       spawnBuf         = VK_NULL_HANDLE;
        VmaAllocation  spawnAlloc       = VK_NULL_HANDLE;
        void*          spawnMapped      = nullptr;
        uint32_t       spawnCapacity    = 0;     // matches max_particles for now

        VkBuffer       counterBuf       = VK_NULL_HANDLE;
        VmaAllocation  counterAlloc     = VK_NULL_HANDLE;

        VkDescriptorSet computeSet      = VK_NULL_HANDLE;  // pool + spawns + counter
        VkDescriptorSet renderPoolSet   = VK_NULL_HANDLE;  // pool only (set=0 of render)
        VkDescriptorSet renderTexSet    = VK_NULL_HANDLE;  // sprite (set=2 of render)

        // CPU staging — collected during tickEmitters / spawnOnce,
        // copied into spawnMapped at the start of recordCompute.
        std::vector<GpuSpawnRequest> pendingSpawns;

        const ParticleSystem* def = nullptr;
        ParticleSystemID      id;

        // Cached UV rect of the bound sprite (texture-space, [0..1]).
        glm::vec4 uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
    };

    Pool& acquirePool(ParticleSystemID id);
    void  destroyPool(VulkanContext& ctx, Pool& pool);

    void rebuildPipelines();
    void writeFrameUbo(entt::registry& reg);

    // Cached engine pointers. Set in init(), nulled in shutdown.
    VulkanContext*       m_ctx        = nullptr;
    RenderPass*          m_renderPass = nullptr;
    ResourceManager*     m_resources  = nullptr;
    DescriptorAllocator* m_descriptors = nullptr;

    // Pipelines + descriptor layouts.
    ComputePipeline       m_spawnPipeline;
    ComputePipeline       m_simulatePipeline;
    Pipeline              m_renderPipeline;
    PipelineConfig        m_renderConfig;
    ComputePipelineConfig m_spawnConfig;
    ComputePipelineConfig m_simulateConfig;
    VkDescriptorSetLayout m_computeLayout    = VK_NULL_HANDLE;   // pool + spawns + counter
    VkDescriptorSetLayout m_renderPoolLayout = VK_NULL_HANDLE;   // pool SSBO (set=0)
    VkDescriptorSetLayout m_renderFrameLayout= VK_NULL_HANDLE;   // VP UBO (set=1)
    VkDescriptorSetLayout m_renderTexLayout  = VK_NULL_HANDLE;   // texture (set=2)
    VkDescriptorSet       m_renderFrameSet   = VK_NULL_HANDLE;
    UniformBuffer         m_frameUbo;

    // Unit quad shared with SpriteRenderer; we just create our own
    // copy rather than couple the two systems.
    VertexBuffer          m_quadBuffer;

    VkExtent2D            m_currentExtent = { 0, 0 };

    // System-keyed map: stable lookup (no rehash invalidation of
    // descriptor sets) — emplace + return by reference.
    std::unordered_map<ParticleSystemID, Pool> m_pools;

    // Hot-reload subscription for our compute + render shaders.
    Subscription          m_shaderReloadSub;
};

} // namespace engine
