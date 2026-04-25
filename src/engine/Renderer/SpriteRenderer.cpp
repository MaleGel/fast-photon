#include "SpriteRenderer.h"
#include "RenderQueue.h"
#include "RendererEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "Texture.h"
#include "Sprite.h"

#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/SpriteComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"

#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstring>

namespace engine {

static constexpr std::array<float, 6 * 4> k_unitQuad = {
    -0.5f, -0.5f,   0.f, 0.f,
     0.5f, -0.5f,   1.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,

    -0.5f, -0.5f,   0.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,
    -0.5f,  0.5f,   0.f, 1.f,
};

struct alignas(16) SpritePushConstants {
    float worldOffsetX, worldOffsetY;
    float worldSizeX,   worldSizeY;
    float r, g, b, a;
    float u0, v0, u1, v1;
};

struct FrameUniforms {
    glm::mat4 viewProjection;
};

// Per-sprite payload packed into RenderCommand::payload.
// 56 bytes — under the 64-byte limit.
struct SpriteBatchCmd {
    uint32_t textureIdHash;   // StringAtom::hash() — cheap compare
    SpritePushConstants pc;   // 48 bytes
};
static_assert(sizeof(SpriteBatchCmd) <= RenderCommand::kPayloadSize,
              "SpriteBatchCmd exceeds payload size");
static_assert(std::is_trivially_copyable_v<SpriteBatchCmd>, "");

// ── init / shutdown ──────────────────────────────────────────────────────────

void SpriteRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                          const ResourceManager& resources,
                          DescriptorAllocator& descriptors,
                          EventBus& bus) {
    m_ctx        = &ctx;
    m_renderPass = &renderPass;
    m_resources  = &resources;

    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding         = 0;
    frameBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    m_frameLayout = descriptors.createLayout(ctx, { frameBinding });

    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding         = 0;
    texBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_materialLayout = descriptors.createLayout(ctx, { texBinding });

    m_pipelineCfg = PipelineConfig{};
    m_pipelineCfg.vertShader        = ShaderID("sprite_vert");
    m_pipelineCfg.fragShader        = ShaderID("sprite_frag");
    m_pipelineCfg.pushConstantSize  = sizeof(SpritePushConstants);
    m_pipelineCfg.descriptorLayouts = { m_frameLayout, m_materialLayout };
    m_pipeline.init(ctx, renderPass.handle(), resources, m_pipelineCfg);

    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_pipelineCfg.vertShader || e.id == m_pipelineCfg.fragShader) {
                rebuildPipeline();
            }
        });

    m_frameSet = descriptors.allocate(ctx, m_frameLayout);
    m_frameUbo.init(ctx, sizeof(FrameUniforms));

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUbo.handle();
    bufInfo.range  = sizeof(FrameUniforms);

    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet          = m_frameSet;
    uboWrite.dstBinding      = 0;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo     = &bufInfo;
    vkUpdateDescriptorSets(ctx.device(), 1, &uboWrite, 0, nullptr);

    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    FP_CORE_INFO("SpriteRenderer initialized");
    (void)resources;
}

void SpriteRenderer::rebuildPipeline() {
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_pipelineCfg);
    FP_CORE_INFO("SpriteRenderer: pipeline rebuilt after shader reload");
}

void SpriteRenderer::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    m_frameUbo.destroy(ctx);
    m_quadBuffer.destroy(ctx);
    m_pipeline.shutdown(ctx);
    m_materialSets.clear();
}

// ── Material set resolver ────────────────────────────────────────────────────

VkDescriptorSet SpriteRenderer::resolveMaterialSet(const VulkanContext& ctx,
                                                   DescriptorAllocator& descriptors,
                                                   const ResourceManager& resources,
                                                   TextureID textureId) {
    auto it = m_materialSets.find(textureId);
    if (it != m_materialSets.end()) return it->second;

    const Texture* tex = resources.getTexture(textureId);
    FP_CORE_ASSERT(tex && tex->isValid(),
                   "SpriteRenderer: texture '{}' missing", textureId.c_str());

    VkDescriptorSet set = descriptors.allocate(ctx, m_materialLayout);
    FP_CORE_ASSERT(set != VK_NULL_HANDLE, "Failed to allocate sprite material set");

    VkDescriptorImageInfo info{};
    info.sampler     = tex->sampler();
    info.imageView   = tex->view();
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &info;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);

    m_materialSets.emplace(textureId, set);
    FP_CORE_TRACE("SpriteRenderer: cached material set for '{}'", textureId.c_str());
    return set;
}

// ── submit / executeBatch ────────────────────────────────────────────────────

void SpriteRenderer::submit(RenderQueue& queue, const VulkanContext& ctx,
                            DescriptorAllocator& descriptors,
                            const Swapchain& swapchain,
                            const ResourceManager& resources,
                            entt::registry& registry) {
    FP_PROFILE_SCOPE("SpriteRenderer::submit");
    // Update frame UBO with the active camera's VP.
    auto camView = registry.view<const ActiveCameraTag,
                                 const TransformComponent,
                                 const CameraComponent2D>();
    glm::mat4 vp(1.f);
    bool haveCamera = false;
    for (auto e : camView) {
        vp = buildViewProjection(camView.get<const TransformComponent>(e),
                                 camView.get<const CameraComponent2D>(e));
        haveCamera = true;
        break;
    }
    if (!haveCamera) return;

    FrameUniforms* uni = m_frameUbo.mapped<FrameUniforms>();
    uni->viewProjection = vp;

    m_currentExtent = swapchain.extent();

    auto view = registry.view<const TransformComponent, const SpriteComponent>();
    for (auto entity : view) {
        const auto& tc = view.get<const TransformComponent>(entity);
        const auto& sc = view.get<const SpriteComponent>(entity);
        if (!sc.visible) continue;

        const Sprite* sprite = resources.getSprite(sc.sprite);
        if (!sprite) continue;
        const Texture* tex = resources.getTexture(sprite->texture);
        if (!tex || !tex->isValid()) continue;

        // Ensure a material set exists for this texture.
        resolveMaterialSet(ctx, descriptors, resources, sprite->texture);

        // Build the per-sprite command payload.
        SpriteBatchCmd cmd{};
        cmd.textureIdHash = sprite->texture.hash();

        const float texW = float(tex->width());
        const float texH = float(tex->height());
        cmd.pc.worldOffsetX = tc.position.x;
        cmd.pc.worldOffsetY = tc.position.y;
        cmd.pc.worldSizeX   = sc.size.x;
        cmd.pc.worldSizeY   = sc.size.y;
        cmd.pc.r = sc.tint.r; cmd.pc.g = sc.tint.g;
        cmd.pc.b = sc.tint.b; cmd.pc.a = sc.tint.a;
        cmd.pc.u0 = (float(sprite->x)                  + 0.5f) / texW;
        cmd.pc.v0 = (float(sprite->y)                  + 0.5f) / texH;
        cmd.pc.u1 = (float(sprite->x + sprite->width)  - 0.5f) / texW;
        cmd.pc.v1 = (float(sprite->y + sprite->height) - 0.5f) / texH;

        queue.submit<SpriteBatchCmd>(this, sc.layer, sc.orderInLayer,
                                     tc.position.z, cmd);
    }
}

void SpriteRenderer::executeBatch(VkCommandBuffer cmd,
                                  const RenderCommand* commands,
                                  size_t commandCount) {
    FP_PROFILE_SCOPE("SpriteRenderer::executeBatch");
    FP_GPU_SCOPE(cmd, "SpriteRenderer");
    if (commandCount == 0) return;

    const float w = float(m_currentExtent.width);
    const float h = float(m_currentExtent.height);
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, m_currentExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    // Frame set stays bound across all sprites.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(), 0, 1, &m_frameSet, 0, nullptr);

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    // Rebind material set only when texture hash changes.
    uint32_t currentHash = 0;
    for (size_t i = 0; i < commandCount; ++i) {
        const auto& rc = commands[i];
        SpriteBatchCmd sc;
        std::memcpy(&sc, rc.payload, sizeof(sc));

        if (sc.textureIdHash != currentHash) {
            // Look up the cached material set via the recorded hash.
            // We stored the StringAtom hash; map is keyed by StringAtom but
            // hashing is identity on the internal hash, so we rebuild the key.
            // Quick path: scan the small map.
            VkDescriptorSet matSet = VK_NULL_HANDLE;
            for (const auto& kv : m_materialSets) {
                if (kv.first.hash() == sc.textureIdHash) { matSet = kv.second; break; }
            }
            FP_CORE_ASSERT(matSet != VK_NULL_HANDLE,
                           "SpriteRenderer: material set missing for texture hash {}",
                           sc.textureIdHash);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipeline.layout(), 1, 1, &matSet, 0, nullptr);
            currentHash = sc.textureIdHash;
        }

        vkCmdPushConstants(cmd, m_pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SpritePushConstants), &sc.pc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
}

} // namespace engine
