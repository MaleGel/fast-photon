#include "GridRenderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RendererEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RenderPass.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/DescriptorAllocator.h"
#include "Renderer/Texture.h"
#include "Renderer/Sprite.h"
#include "Gameplay/GridMap/GridMap.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/Camera/CameraComponent2D.h"
#include "Scene/Components/Camera/ActiveCameraTag.h"
#include "Scene/Components/Camera/CameraMath.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <array>

namespace engine {

// Local unit quad: [-0.5, 0.5] position, [0, 1] uv.
static constexpr std::array<float, 6 * 4> k_unitQuad = {
    -0.5f, -0.5f,   0.f, 0.f,
     0.5f, -0.5f,   1.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,

    -0.5f, -0.5f,   0.f, 0.f,
     0.5f,  0.5f,   1.f, 1.f,
    -0.5f,  0.5f,   0.f, 1.f,
};

struct TileColor { float r, g, b, a; };

static TileColor colorForType(TileType type) {
    switch (type) {
        case TileType::Grass:    return { 0.70f, 1.00f, 0.70f, 1.f };
        case TileType::Forest:   return { 0.40f, 0.70f, 0.40f, 1.f };
        case TileType::Mountain: return { 0.80f, 0.80f, 0.80f, 1.f };
        case TileType::Water:    return { 0.50f, 0.70f, 1.00f, 1.f };
    }
    return { 1.f, 0.f, 1.f, 1.f };
}

// Matches grid.vert / grid.frag push_constant block.
struct alignas(16) GridPushConstants {
    float worldOffsetX, worldOffsetY;
    float worldSizeX,   worldSizeY;
    float r, g, b, a;
    float u0, v0, u1, v1;
};

struct FrameUniforms {
    glm::mat4 viewProjection;
};

// GridRenderer enqueues a single, payload-less command. The whole grid is
// drawn inside executeBatch from the captured m_currentMap pointer.
struct GridBatchCmd { /* intentionally empty */ };
static_assert(sizeof(GridBatchCmd) <= RenderCommand::kPayloadSize, "");

// ── init / shutdown ──────────────────────────────────────────────────────────

void GridRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                        const ResourceManager& resources, DescriptorAllocator& descriptors,
                        const GridMap& /*map*/, SpriteID tileSprite, EventBus& bus) {
    m_ctx        = &ctx;
    m_renderPass = &renderPass;
    m_resources  = &resources;

    const Sprite* sprite = resources.getSprite(tileSprite);
    FP_CORE_ASSERT(sprite != nullptr, "Tile sprite '{}' not registered", tileSprite.c_str());

    const Texture* tex = resources.getTexture(sprite->texture);
    FP_CORE_ASSERT(tex && tex->isValid(),
                   "Sprite '{}' references missing texture '{}'",
                   tileSprite.c_str(), sprite->texture.c_str());

    const float texW = static_cast<float>(tex->width());
    const float texH = static_cast<float>(tex->height());
    m_uvRect[0] = (float(sprite->x)                  + 0.5f) / texW;
    m_uvRect[1] = (float(sprite->y)                  + 0.5f) / texH;
    m_uvRect[2] = (float(sprite->x + sprite->width)  - 0.5f) / texW;
    m_uvRect[3] = (float(sprite->y + sprite->height) - 0.5f) / texH;

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
    m_pipelineCfg.vertShader        = ShaderID("grid_vert");
    m_pipelineCfg.fragShader        = ShaderID("grid_frag");
    m_pipelineCfg.pushConstantSize  = sizeof(GridPushConstants);
    m_pipelineCfg.descriptorLayouts = { m_frameLayout, m_materialLayout };
    m_pipeline.init(ctx, renderPass.handle(), resources, m_pipelineCfg);

    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_pipelineCfg.vertShader || e.id == m_pipelineCfg.fragShader) {
                rebuildPipeline();
            }
        });

    m_frameSet    = descriptors.allocate(ctx, m_frameLayout);
    m_materialSet = descriptors.allocate(ctx, m_materialLayout);

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

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = tex->sampler();
    imageInfo.imageView   = tex->view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet texWrite{};
    texWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    texWrite.dstSet          = m_materialSet;
    texWrite.dstBinding      = 0;
    texWrite.descriptorCount = 1;
    texWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texWrite.pImageInfo      = &imageInfo;

    VkWriteDescriptorSet writes[] = { uboWrite, texWrite };
    vkUpdateDescriptorSets(ctx.device(), 2, writes, 0, nullptr);

    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    FP_CORE_INFO("GridRenderer initialized (sprite='{}')", tileSprite.c_str());
}

void GridRenderer::rebuildPipeline() {
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_pipelineCfg);
    FP_CORE_INFO("GridRenderer: pipeline rebuilt after shader reload");
}

void GridRenderer::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    m_frameUbo.destroy(ctx);
    m_quadBuffer.destroy(ctx);
    m_pipeline.shutdown(ctx);
}

// ── submit / executeBatch ────────────────────────────────────────────────────

void GridRenderer::submit(RenderQueue& queue, const Swapchain& swapchain,
                          const GridMap& map, entt::registry& registry) {
    FP_PROFILE_SCOPE("GridRenderer::submit");
    // Resolve active camera and refresh the UBO once per frame.
    auto view = registry.view<const ActiveCameraTag,
                              const TransformComponent,
                              const CameraComponent2D>();
    glm::mat4 vp(1.f);
    bool haveCamera = false;
    for (auto e : view) {
        vp = buildViewProjection(view.get<const TransformComponent>(e),
                                 view.get<const CameraComponent2D>(e));
        haveCamera = true;
        break;
    }
    FP_CORE_ASSERT(haveCamera, "No ActiveCameraTag entity found");

    FrameUniforms* uni = m_frameUbo.mapped<FrameUniforms>();
    uni->viewProjection = vp;

    m_currentMap    = &map;
    m_currentExtent = swapchain.extent();

    queue.submit<GridBatchCmd>(this, RenderLayer::Terrain, /*order*/ 0,
                               /*z*/ 0.f, GridBatchCmd{});
}

void GridRenderer::executeBatch(VkCommandBuffer cmd,
                                const RenderCommand* /*commands*/,
                                size_t /*commandCount*/) {
    FP_PROFILE_SCOPE("GridRenderer::executeBatch");
    FP_GPU_SCOPE(cmd, "GridRenderer");
    FP_CORE_ASSERT(m_currentMap != nullptr, "GridRenderer: submit() wasn't called this frame");

    const float w = float(m_currentExtent.width);
    const float h = float(m_currentExtent.height);
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, m_currentExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    VkDescriptorSet sets[] = { m_frameSet, m_materialSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(), 0, 2, sets, 0, nullptr);

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    const GridMap& map = *m_currentMap;
    for (int32_t row = 0; row < map.height(); ++row) {
        for (int32_t col = 0; col < map.width(); ++col) {
            const Tile& tile = map.at(col, row);
            TileColor c = colorForType(tile.type);

            GridPushConstants pc{};
            pc.worldOffsetX = float(col) + 0.5f;
            pc.worldOffsetY = float(row) + 0.5f;
            pc.worldSizeX   = 1.0f;
            pc.worldSizeY   = 1.0f;
            pc.r = c.r; pc.g = c.g; pc.b = c.b; pc.a = c.a;
            pc.u0 = m_uvRect[0]; pc.v0 = m_uvRect[1];
            pc.u1 = m_uvRect[2]; pc.v1 = m_uvRect[3];

            vkCmdPushConstants(cmd, m_pipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }
}

} // namespace engine
