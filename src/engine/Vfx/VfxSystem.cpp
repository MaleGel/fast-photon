#include "VfxSystem.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderLayer.h"
#include "Renderer/RendererEvents.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RenderPass.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/DescriptorAllocator.h"
#include "Renderer/Texture.h"
#include "Renderer/Sprite.h"
#include "Scene/Components/ParticleEmitterComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <cstring>
#include <random>
#include <stdexcept>

namespace engine {

namespace {

// Shared with SpriteRenderer / GridRenderer.
constexpr std::array<float, 6 * 4> k_unitQuad = {
    -0.5f, -0.5f,   0.f, 0.f,
     0.5f, -0.5f,   1.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,

    -0.5f, -0.5f,   0.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,
    -0.5f,  0.5f,   0.f, 1.f,
};

constexpr uint32_t kComputeWorkgroupSize = 64;

struct alignas(16) FrameUniforms {
    glm::mat4 viewProjection;
};

// Push constants — must match the shaders byte-for-byte.
struct alignas(16) SpawnPushConstants {
    uint32_t capacity;
    uint32_t spawnCount;
};
struct alignas(16) SimulatePushConstants {
    float    dt;
    uint32_t capacity;
};
struct alignas(16) RenderPushConstants {
    glm::vec4 uvRect;
};

// RenderQueue payload. We carry a pointer to the per-system Pool so the
// backend can pull descriptor sets and instance count without scanning
// m_pools each command.
struct VfxBatchCmd {
    void* poolPtr;   // VfxSystem::Pool* — type-erased for the trivially-copyable rule
};
static_assert(sizeof(VfxBatchCmd) <= RenderCommand::kPayloadSize, "");
static_assert(std::is_trivially_copyable_v<VfxBatchCmd>, "");

// Cheap RNG for spawn jitter. Not cryptographic, doesn't need to be —
// just consistent within a process. mt19937 is overkill but it's already
// in <random> so we don't pull a new dep.
std::mt19937& rng() {
    static std::mt19937 g{ 0xfa57c0deu };
    return g;
}

float frand(float lo, float hi) {
    if (lo == hi) return lo;
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng());
}

} // namespace

// ── Init / shutdown ─────────────────────────────────────────────────────────

void VfxSystem::init(VulkanContext& ctx, RenderPass& sceneRenderPass,
                     ResourceManager& resources, DescriptorAllocator& descriptors,
                     EventBus& bus) {
    m_ctx         = &ctx;
    m_renderPass  = &sceneRenderPass;
    m_resources   = &resources;
    m_descriptors = &descriptors;

    // ── Compute descriptor layout: pool + spawns + counter ────────
    VkDescriptorSetLayoutBinding cb[3]{};
    for (int i = 0; i < 3; ++i) {
        cb[i].binding         = static_cast<uint32_t>(i);
        cb[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cb[i].descriptorCount = 1;
        cb[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    m_computeLayout = descriptors.createLayout(ctx, { cb[0], cb[1], cb[2] });

    // ── Render: 3 sets (pool, frame UBO, sprite tex) ──────────────
    VkDescriptorSetLayoutBinding poolBind{};
    poolBind.binding         = 0;
    poolBind.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolBind.descriptorCount = 1;
    poolBind.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    m_renderPoolLayout = descriptors.createLayout(ctx, { poolBind });

    VkDescriptorSetLayoutBinding frameBind{};
    frameBind.binding         = 0;
    frameBind.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBind.descriptorCount = 1;
    frameBind.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    m_renderFrameLayout = descriptors.createLayout(ctx, { frameBind });

    VkDescriptorSetLayoutBinding texBind{};
    texBind.binding         = 0;
    texBind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBind.descriptorCount = 1;
    texBind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_renderTexLayout = descriptors.createLayout(ctx, { texBind });

    // Frame UBO (one for the whole VfxSystem — every system shares the
    // active camera's VP matrix).
    m_renderFrameSet = descriptors.allocate(ctx, m_renderFrameLayout);
    m_frameUbo.init(ctx, sizeof(FrameUniforms));
    {
        VkDescriptorBufferInfo bi{};
        bi.buffer = m_frameUbo.handle();
        bi.range  = sizeof(FrameUniforms);
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = m_renderFrameSet;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo     = &bi;
        vkUpdateDescriptorSets(ctx.device(), 1, &w, 0, nullptr);
    }

    // ── Pipeline configs (cached for hot-reload rebuilds) ─────────
    m_spawnConfig = ComputePipelineConfig{};
    m_spawnConfig.computeShader     = ShaderID("vfx_spawn_comp");
    m_spawnConfig.pushConstantSize  = sizeof(SpawnPushConstants);
    m_spawnConfig.descriptorLayouts = { m_computeLayout };
    m_spawnPipeline.init(ctx, resources, m_spawnConfig);

    m_simulateConfig = ComputePipelineConfig{};
    m_simulateConfig.computeShader     = ShaderID("vfx_simulate_comp");
    m_simulateConfig.pushConstantSize  = sizeof(SimulatePushConstants);
    m_simulateConfig.descriptorLayouts = { m_computeLayout };
    m_simulatePipeline.init(ctx, resources, m_simulateConfig);

    // Render pipeline: instanced quad. Custom vertex layout (vec2 pos +
    // vec2 uv, stride 16) — same as default sprite layout but we declare
    // it explicitly so this code is robust to Pipeline's default changing.
    m_renderConfig = PipelineConfig{};
    m_renderConfig.vertShader        = ShaderID("vfx_vert");
    m_renderConfig.fragShader        = ShaderID("vfx_frag");
    m_renderConfig.pushConstantSize  = sizeof(RenderPushConstants);
    m_renderConfig.descriptorLayouts = {
        m_renderPoolLayout, m_renderFrameLayout, m_renderTexLayout
    };
    m_renderConfig.vertexStride      = sizeof(float) * 4;
    m_renderConfig.vertexAttributes  = {
        { 0, VK_FORMAT_R32G32_SFLOAT, 0 },
        { 1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 },
    };
    // Particles look right with depth read but no write — so a hundred
    // overlapping particles all blend instead of mutually clipping.
    m_renderConfig.depthTest  = true;
    m_renderConfig.depthWrite = false;
    m_renderPipeline.init(ctx, sceneRenderPass.handle(), resources, m_renderConfig);

    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_spawnConfig.computeShader
                || e.id == m_simulateConfig.computeShader
                || e.id == m_renderConfig.vertShader
                || e.id == m_renderConfig.fragShader) {
                rebuildPipelines();
            }
        });

    FP_CORE_INFO("VfxSystem initialized");
}

void VfxSystem::shutdown(VulkanContext& ctx) {
    m_shaderReloadSub.release();
    for (auto& [id, pool] : m_pools) destroyPool(ctx, pool);
    m_pools.clear();
    m_quadBuffer.destroy(ctx);
    m_frameUbo.destroy(ctx);
    m_renderPipeline.shutdown(ctx);
    m_simulatePipeline.shutdown(ctx);
    m_spawnPipeline.shutdown(ctx);
    m_ctx = nullptr;
    m_renderPass = nullptr;
    m_resources = nullptr;
    m_descriptors = nullptr;
    FP_CORE_TRACE("VfxSystem destroyed");
}

void VfxSystem::rebuildPipelines() {
    m_spawnPipeline.shutdown(*m_ctx);
    m_simulatePipeline.shutdown(*m_ctx);
    m_renderPipeline.shutdown(*m_ctx);
    m_spawnPipeline.init(*m_ctx, *m_resources, m_spawnConfig);
    m_simulatePipeline.init(*m_ctx, *m_resources, m_simulateConfig);
    m_renderPipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_renderConfig);
    FP_CORE_INFO("VfxSystem: pipelines rebuilt after shader reload");
}

// ── Pool acquisition ────────────────────────────────────────────────────────

VfxSystem::Pool& VfxSystem::acquirePool(ParticleSystemID id) {
    auto it = m_pools.find(id);
    if (it != m_pools.end()) return it->second;

    const ParticleSystem* def = m_resources->getParticleSystem(id);
    FP_CORE_ASSERT(def != nullptr,
                   "VfxSystem: unknown particle system '{}'", id.c_str());

    Pool pool;
    pool.def           = def;
    pool.id            = id;
    pool.spawnCapacity = def->maxParticles;

    // Particle SSBO — GPU-only. Big enough for maxParticles slots, zeroed.
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = sizeof(GpuParticle) * def->maxParticles;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                  | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateBuffer(m_ctx->allocator(), &bci, &aci,
                            &pool.particleBuf, &pool.particleAlloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("VfxSystem: particle SSBO alloc failed");
    }

    // Spawn buffer — CPU writes per frame, GPU reads. Sized to maxParticles
    // so a frame can refill the entire pool if pushed hard.
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = sizeof(GpuSpawnRequest) * def->maxParticles;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_ctx->allocator(), &bci, &aci,
                            &pool.spawnBuf, &pool.spawnAlloc, &info) != VK_SUCCESS)
            throw std::runtime_error("VfxSystem: spawn buffer alloc failed");
        pool.spawnMapped = info.pMappedData;
    }

    // Counter — single uint head + 3 pad uints (std430). GPU-only, init
    // to zero by the driver thanks to TRANSFER_DST + a one-shot fill.
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = sizeof(uint32_t) * 4;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                  | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateBuffer(m_ctx->allocator(), &bci, &aci,
                            &pool.counterBuf, &pool.counterAlloc, nullptr) != VK_SUCCESS)
            throw std::runtime_error("VfxSystem: counter buffer alloc failed");
    }

    // ── Descriptor sets ───────────────────────────────────────────
    pool.computeSet    = m_descriptors->allocate(*m_ctx, m_computeLayout);
    pool.renderPoolSet = m_descriptors->allocate(*m_ctx, m_renderPoolLayout);
    pool.renderTexSet  = m_descriptors->allocate(*m_ctx, m_renderTexLayout);

    // Compute set: pool, spawns, counter.
    VkDescriptorBufferInfo bi[3]{};
    bi[0].buffer = pool.particleBuf; bi[0].range = sizeof(GpuParticle)    * def->maxParticles;
    bi[1].buffer = pool.spawnBuf;    bi[1].range = sizeof(GpuSpawnRequest) * def->maxParticles;
    bi[2].buffer = pool.counterBuf;  bi[2].range = sizeof(uint32_t) * 4;
    VkWriteDescriptorSet w[4]{};
    for (int i = 0; i < 3; ++i) {
        w[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet          = pool.computeSet;
        w[i].dstBinding      = static_cast<uint32_t>(i);
        w[i].descriptorCount = 1;
        w[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[i].pBufferInfo     = &bi[i];
    }
    // Render pool set (set=0, binding=0) — same particle SSBO as compute.
    w[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[3].dstSet          = pool.renderPoolSet;
    w[3].dstBinding      = 0;
    w[3].descriptorCount = 1;
    w[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[3].pBufferInfo     = &bi[0];
    vkUpdateDescriptorSets(m_ctx->device(), 4, w, 0, nullptr);

    // Texture set — bind the sprite's atlas texture (or a blank fallback
    // if the system author hasn't picked a sprite yet). We also stash
    // the UV rect so render-time push constants can subsample.
    if (def->sprite.isValid()) {
        const Sprite* sprite = m_resources->getSprite(def->sprite);
        if (sprite) {
            const Texture* tex = m_resources->getTexture(sprite->texture);
            if (tex && tex->isValid()) {
                VkDescriptorImageInfo ii{};
                ii.sampler     = tex->sampler();
                ii.imageView   = tex->view();
                ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet tw{};
                tw.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                tw.dstSet          = pool.renderTexSet;
                tw.dstBinding      = 0;
                tw.descriptorCount = 1;
                tw.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                tw.pImageInfo      = &ii;
                vkUpdateDescriptorSets(m_ctx->device(), 1, &tw, 0, nullptr);

                const float texW = float(tex->width());
                const float texH = float(tex->height());
                pool.uvRect = {
                    (float(sprite->x)                 + 0.5f) / texW,
                    (float(sprite->y)                 + 0.5f) / texH,
                    (float(sprite->x + sprite->width) - 0.5f) / texW,
                    (float(sprite->y + sprite->height)- 0.5f) / texH,
                };
            }
        }
    }

    pool.pendingSpawns.reserve(def->maxParticles);
    auto [ins, _ok] = m_pools.emplace(id, std::move(pool));
    return ins->second;
}

void VfxSystem::destroyPool(VulkanContext& ctx, Pool& pool) {
    if (pool.particleBuf) vmaDestroyBuffer(ctx.allocator(), pool.particleBuf, pool.particleAlloc);
    if (pool.spawnBuf)    vmaDestroyBuffer(ctx.allocator(), pool.spawnBuf,    pool.spawnAlloc);
    if (pool.counterBuf)  vmaDestroyBuffer(ctx.allocator(), pool.counterBuf,  pool.counterAlloc);
    pool = {};
}

// ── CPU spawn API ───────────────────────────────────────────────────────────

static GpuSpawnRequest makeSpawn(const ParticleSystem& def, glm::vec2 position) {
    GpuSpawnRequest r{};
    r.position   = position;
    r.velocity   = { frand(def.velocityMin.x, def.velocityMax.x),
                     frand(def.velocityMin.y, def.velocityMax.y) };
    r.colorStart = def.colorStart;
    r.colorEnd   = def.colorEnd;
    r.lifetime   = frand(def.lifetimeMin, def.lifetimeMax);
    r.sizeStart  = def.sizeStart;
    r.sizeEnd    = def.sizeEnd;
    r.gravity    = def.gravity;
    return r;
}

void VfxSystem::spawnOnce(ParticleSystemID systemId, glm::vec2 position,
                          uint32_t count) {
    if (count == 0) return;
    Pool& pool = acquirePool(systemId);
    const uint32_t cap = pool.spawnCapacity;
    // Don't exceed what the spawn buffer can hold this frame; oldest
    // queued spawns win — newer ones are dropped silently.
    const uint32_t room = (pool.pendingSpawns.size() < cap)
                        ? (cap - static_cast<uint32_t>(pool.pendingSpawns.size()))
                        : 0;
    const uint32_t take = std::min(count, room);
    for (uint32_t i = 0; i < take; ++i) {
        pool.pendingSpawns.push_back(makeSpawn(*pool.def, position));
    }
}

void VfxSystem::tickEmitters(entt::registry& reg, float dt) {
    FP_PROFILE_SCOPE("VfxSystem::tickEmitters");
    auto view = reg.view<ParticleEmitterComponent, TransformComponent>();
    for (auto e : view) {
        auto& ec = view.get<ParticleEmitterComponent>(e);
        auto& tc = view.get<TransformComponent>(e);
        if (!ec.active) continue;

        Pool& pool = acquirePool(ec.system);
        const ParticleSystem& def = *pool.def;

        // One-shot burst on first activation. Reset firedBurst when the
        // user toggles `active = false → true` (game code's contract).
        if (!ec.firedBurst && def.burstCount > 0) {
            ec.firedBurst = true;
            spawnOnce(ec.system,
                      { tc.position.x, tc.position.y },
                      def.burstCount);
        }

        if (def.emitRate <= 0.0f) continue;
        ec.timeAccum += dt;
        const float interval = 1.0f / def.emitRate;
        while (ec.timeAccum >= interval) {
            ec.timeAccum -= interval;
            const uint32_t room = (pool.pendingSpawns.size() < pool.spawnCapacity)
                                ? 1u : 0u;
            if (room == 0) break;
            pool.pendingSpawns.push_back(
                makeSpawn(def, { tc.position.x, tc.position.y }));
        }
    }
}

// ── GPU recording ───────────────────────────────────────────────────────────

void VfxSystem::recordCompute(VkCommandBuffer cmd, float dt) {
    FP_PROFILE_SCOPE("VfxSystem::recordCompute");
    FP_GPU_SCOPE(cmd, "VfxCompute");

    for (auto& [id, pool] : m_pools) {
        // Upload pending spawns (host-visible buffer, persistently mapped).
        const uint32_t spawnCount = static_cast<uint32_t>(pool.pendingSpawns.size());
        if (spawnCount > 0 && pool.spawnMapped) {
            std::memcpy(pool.spawnMapped, pool.pendingSpawns.data(),
                        spawnCount * sizeof(GpuSpawnRequest));
        }

        // Spawn pass. Skipped entirely when there's nothing pending —
        // simulate still runs to advance live particles.
        if (spawnCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              m_spawnPipeline.handle());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_spawnPipeline.layout(), 0, 1,
                                    &pool.computeSet, 0, nullptr);
            SpawnPushConstants spc{ pool.spawnCapacity, spawnCount };
            vkCmdPushConstants(cmd, m_spawnPipeline.layout(),
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(spc), &spc);
            const uint32_t groups =
                (spawnCount + kComputeWorkgroupSize - 1) / kComputeWorkgroupSize;
            vkCmdDispatch(cmd, groups, 1, 1);

            // Memory barrier: spawn writes → simulate reads. SSBO is the
            // same buffer; we need a stage-to-stage dependency.
            VkMemoryBarrier mb{};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
                             | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &mb, 0, nullptr, 0, nullptr);
        }

        // Simulate pass — always.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_simulatePipeline.handle());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_simulatePipeline.layout(), 0, 1,
                                &pool.computeSet, 0, nullptr);
        SimulatePushConstants pc{ dt, pool.spawnCapacity };
        vkCmdPushConstants(cmd, m_simulatePipeline.layout(),
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);
        const uint32_t simGroups =
            (pool.spawnCapacity + kComputeWorkgroupSize - 1) / kComputeWorkgroupSize;
        vkCmdDispatch(cmd, simGroups, 1, 1);

        // Compute → vertex shader (render). Buffer is now in steady
        // state; the swap pass / scene render reads it through the
        // pool descriptor set.
        VkMemoryBarrier mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0, 1, &mb, 0, nullptr, 0, nullptr);

        pool.pendingSpawns.clear();
    }
}

// ── Render submission ───────────────────────────────────────────────────────

void VfxSystem::writeFrameUbo(entt::registry& reg) {
    auto view = reg.view<const ActiveCameraTag,
                         const TransformComponent,
                         const CameraComponent2D>();
    for (auto e : view) {
        const auto& tc = view.get<const TransformComponent>(e);
        const auto& cc = view.get<const CameraComponent2D>(e);
        FrameUniforms* uni = m_frameUbo.mapped<FrameUniforms>();
        uni->viewProjection = buildViewProjection(tc, cc);
        return;
    }
}

void VfxSystem::submit(RenderQueue& queue, const Swapchain& swapchain,
                       entt::registry& registry) {
    FP_PROFILE_SCOPE("VfxSystem::submit");
    if (m_pools.empty()) return;

    writeFrameUbo(registry);
    m_currentExtent = swapchain.extent();

    // One command per system. We render every system every frame —
    // dead particles collapse to degenerate quads in the vertex shader,
    // so empty pools cost a discarded triangle per slot. Worth fixing
    // only if profiling shows it (the per-pool draw setup is a few
    // hundred ns).
    for (auto& [id, pool] : m_pools) {
        VfxBatchCmd cmd{};
        cmd.poolPtr = &pool;
        queue.submit<VfxBatchCmd>(this, RenderLayer::Effects, /*order*/ 0,
                                   /*z*/ 0.0f, cmd);
    }
}

void VfxSystem::executeBatch(VkCommandBuffer cmd,
                             const RenderCommand* commands,
                             size_t commandCount) {
    FP_PROFILE_SCOPE("VfxSystem::executeBatch");
    FP_GPU_SCOPE(cmd, "VfxRender");
    if (commandCount == 0) return;

    const float w = float(m_currentExtent.width);
    const float h = float(m_currentExtent.height);
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, m_currentExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipeline.handle());

    // Frame UBO (set=1) is constant across every system.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_renderPipeline.layout(), 1, 1,
                            &m_renderFrameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    for (size_t i = 0; i < commandCount; ++i) {
        VfxBatchCmd bc;
        std::memcpy(&bc, commands[i].payload, sizeof(bc));
        Pool* pool = static_cast<Pool*>(bc.poolPtr);
        if (!pool) continue;

        // Pool SSBO (set=0) and texture (set=2) vary per system.
        VkDescriptorSet sets[2] = { pool->renderPoolSet, pool->renderTexSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_renderPipeline.layout(), 0, 1, &sets[0], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_renderPipeline.layout(), 2, 1, &sets[1], 0, nullptr);

        RenderPushConstants pc{ pool->uvRect };
        vkCmdPushConstants(cmd, m_renderPipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        // 6 vertices for the unit quad, 'capacity' instances. The vertex
        // shader collapses dead particles to a degenerate quad so we
        // don't pay fragment cost for empty slots.
        vkCmdDraw(cmd, 6, pool->spawnCapacity, 0, 0);
    }
}

} // namespace engine
