#include "GridRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "Texture.h"
#include "Sprite.h"
#include "Gameplay/GridMap/GridMap.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/Camera/CameraComponent2D.h"
#include "ECS/Components/Camera/ActiveCameraTag.h"
#include "ECS/Components/Camera/CameraMath.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstring>

namespace engine {

// NDC square: two triangles forming a unit quad centered at origin.
// Now in local/world-unit space: range [-0.5, 0.5]. Camera VP handles the rest.
static constexpr std::array<float, 6 * 4> k_unitQuad = {
    // pos          uv
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

// Per-draw push constant block — must match grid.vert/frag exactly.
struct alignas(16) GridPushConstants {
    float worldOffsetX, worldOffsetY;
    float worldSizeX,   worldSizeY;
    float r, g, b, a;
    float u0, v0, u1, v1;
};

// Layout of the frame UBO (set=0). Single mat4 for now.
struct FrameUniforms {
    glm::mat4 viewProjection;
};

// ── Public ────────────────────────────────────────────────────────────────────

void GridRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                        const ResourceManager& resources, DescriptorAllocator& descriptors,
                        const GridMap& /*map*/, SpriteID tileSprite) {
    // ── 1. Resolve sprite → texture + UV rect ───────────────────────────────
    const Sprite* sprite = resources.getSprite(tileSprite);
    FP_CORE_ASSERT(sprite != nullptr, "Tile sprite '{}' not registered", tileSprite.c_str());

    const Texture* tex = resources.getTexture(sprite->texture);
    FP_CORE_ASSERT(tex && tex->isValid(),
                   "Sprite '{}' references missing texture '{}'",
                   tileSprite.c_str(), sprite->texture.c_str());

    const float texW = static_cast<float>(tex->width());
    const float texH = static_cast<float>(tex->height());
    m_uvRect[0] = (static_cast<float>(sprite->x)                  + 0.5f) / texW;
    m_uvRect[1] = (static_cast<float>(sprite->y)                  + 0.5f) / texH;
    m_uvRect[2] = (static_cast<float>(sprite->x + sprite->width)  - 0.5f) / texW;
    m_uvRect[3] = (static_cast<float>(sprite->y + sprite->height) - 0.5f) / texH;

    // ── 2. Descriptor set layouts ──────────────────────────────────────────
    // set=0 — FrameUniforms UBO, visible to vertex stage.
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding         = 0;
    frameBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    m_frameLayout = descriptors.createLayout(ctx, { frameBinding });

    // set=1 — texture, visible to fragment.
    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding         = 0;
    texBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_materialLayout = descriptors.createLayout(ctx, { texBinding });

    // ── 3. Pipeline uses both layouts in set order ─────────────────────────
    PipelineConfig cfg;
    cfg.vertShader        = ShaderID("grid_vert");
    cfg.fragShader        = ShaderID("grid_frag");
    cfg.pushConstantSize  = sizeof(GridPushConstants);
    cfg.descriptorLayouts = { m_frameLayout, m_materialLayout };
    m_pipeline.init(ctx, renderPass, resources, cfg);

    // ── 4. Allocate descriptor sets ────────────────────────────────────────
    m_frameSet    = descriptors.allocate(ctx, m_frameLayout);
    m_materialSet = descriptors.allocate(ctx, m_materialLayout);
    FP_CORE_ASSERT(m_frameSet    != VK_NULL_HANDLE, "Frame descriptor set allocation failed");
    FP_CORE_ASSERT(m_materialSet != VK_NULL_HANDLE, "Material descriptor set allocation failed");

    // ── 5. Create frame UBO + wire it into the frame set ───────────────────
    m_frameUbo.init(ctx, sizeof(FrameUniforms));

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUbo.handle();
    bufInfo.offset = 0;
    bufInfo.range  = sizeof(FrameUniforms);

    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet          = m_frameSet;
    uboWrite.dstBinding      = 0;
    uboWrite.descriptorCount = 1;
    uboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.pBufferInfo     = &bufInfo;

    // ── 6. Wire texture into material set ──────────────────────────────────
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

    // ── 7. Upload quad vertex data ────────────────────────────────────────
    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    FP_CORE_INFO("GridRenderer initialized (sprite='{}', uv=[{:.3f},{:.3f} .. {:.3f},{:.3f}])",
                 tileSprite.c_str(),
                 m_uvRect[0], m_uvRect[1], m_uvRect[2], m_uvRect[3]);
}

void GridRenderer::shutdown(const VulkanContext& ctx) {
    m_frameUbo.destroy(ctx);
    m_quadBuffer.destroy(ctx);
    m_pipeline.shutdown(ctx);
}

void GridRenderer::draw(VkCommandBuffer cmd, const Swapchain& swapchain,
                        const GridMap& map, entt::registry& registry,
                        float /*alpha*/) const {
    const VkExtent2D ext = swapchain.extent();
    const float w = static_cast<float>(ext.width);
    const float h = static_cast<float>(ext.height);

    // ── Query the active camera and build VP ────────────────────────────────
    auto view = registry.view<const ActiveCameraTag,
                              const TransformComponent,
                              const CameraComponent2D>();

    glm::mat4 vp(1.0f);
    bool haveCamera = false;
    for (auto entity : view) {
        const auto& transform = view.get<const TransformComponent>(entity);
        const auto& cam       = view.get<const CameraComponent2D>(entity);
        vp = buildViewProjection(transform, cam);
        haveCamera = true;
        break;  // expect exactly one ActiveCameraTag
    }
    FP_CORE_ASSERT(haveCamera, "No ActiveCameraTag entity found — nothing would render");

    // Write VP into the persistently-mapped UBO.
    FrameUniforms* uniforms = m_frameUbo.mapped<FrameUniforms>();
    uniforms->viewProjection = vp;

    // ── Viewport/scissor ───────────────────────────────────────────────────
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, ext };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    // Bind both descriptor sets in one call (set=0 frame, set=1 material).
    VkDescriptorSet sets[] = { m_frameSet, m_materialSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(),
                            0,
                            2, sets,
                            0, nullptr);

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    // ── Tile grid: 1 cell = 1 world unit, origin at (0,0) ──────────────────
    for (int32_t row = 0; row < map.height(); ++row) {
        for (int32_t col = 0; col < map.width(); ++col) {
            const Tile& tile = map.at(col, row);

            TileColor c = colorForType(tile.type);

            GridPushConstants pc{};
            // Tile center in world units — quad is local [-0.5..0.5],
            // scaled by worldSize=1 and shifted by worldOffset=(col+0.5, row+0.5).
            pc.worldOffsetX = static_cast<float>(col) + 0.5f;
            pc.worldOffsetY = static_cast<float>(row) + 0.5f;
            pc.worldSizeX   = 1.0f;
            pc.worldSizeY   = 1.0f;
            pc.r = c.r; pc.g = c.g; pc.b = c.b; pc.a = c.a;
            pc.u0 = m_uvRect[0];
            pc.v0 = m_uvRect[1];
            pc.u1 = m_uvRect[2];
            pc.v1 = m_uvRect[3];

            vkCmdPushConstants(cmd, m_pipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }
}

} // namespace engine
