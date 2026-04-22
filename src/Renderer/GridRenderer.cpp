#include "GridRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "Texture.h"
#include "Sprite.h"
#include "Gameplay/GridMap/GridMap.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <array>

namespace engine {

// NDC square: two triangles forming a unit quad centered at origin.
// Vertex layout: vec2 position + vec2 uv (in [0,1] over the quad itself).
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

// Push-constant block — matches grid.vert / grid.frag layout exactly.
// Total: 48 bytes (well under the 128-byte Vulkan minimum).
struct alignas(16) GridPushConstants {
    float offsetX, offsetY;
    float scaleX,  scaleY;
    float r, g, b, a;
    float u0, v0, u1, v1;   // uv sub-rect inside the bound texture
};

// ── Public ────────────────────────────────────────────────────────────────────

void GridRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                        const ResourceManager& resources, DescriptorAllocator& descriptors,
                        const GridMap& /*map*/, SpriteID tileSprite) {
    // ── 1. Resolve sprite → texture + pixel rect → normalized UV rect ───────
    const Sprite* sprite = resources.getSprite(tileSprite);
    FP_CORE_ASSERT(sprite != nullptr, "Tile sprite '{}' not registered", tileSprite.c_str());

    const Texture* tex = resources.getTexture(sprite->texture);
    FP_CORE_ASSERT(tex && tex->isValid(),
                   "Sprite '{}' references missing texture '{}'",
                   tileSprite.c_str(), sprite->texture.c_str());

    // Half-pixel inset: with linear filtering, sampling exactly at the
    // sub-rect boundary blends in neighbouring atlas pixels (transparent
    // or from adjacent sprites). Nudging by 0.5 px keeps samples strictly
    // inside the sprite's own pixels.
    const float texW = static_cast<float>(tex->width());
    const float texH = static_cast<float>(tex->height());
    m_uvRect[0] = (static_cast<float>(sprite->x)                + 0.5f) / texW;
    m_uvRect[1] = (static_cast<float>(sprite->y)                + 0.5f) / texH;
    m_uvRect[2] = (static_cast<float>(sprite->x + sprite->width)  - 0.5f) / texW;
    m_uvRect[3] = (static_cast<float>(sprite->y + sprite->height) - 0.5f) / texH;

    // ── 2. Descriptor set layout: one combined image+sampler for fragment ───
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorLayout = descriptors.createLayout(ctx, { binding });

    // ── 3. Pipeline knows about this layout ─────────────────────────────────
    PipelineConfig cfg;
    cfg.vertShader       = ShaderID("grid_vert");
    cfg.fragShader       = ShaderID("grid_frag");
    cfg.pushConstantSize = sizeof(GridPushConstants);
    cfg.descriptorLayout = m_descriptorLayout;
    m_pipeline.init(ctx, renderPass, resources, cfg);

    // ── 4. Allocate descriptor set ──────────────────────────────────────────
    m_descriptorSet = descriptors.allocate(ctx, m_descriptorLayout);
    FP_CORE_ASSERT(m_descriptorSet != VK_NULL_HANDLE, "Grid descriptor set allocation failed");

    // ── 5. Write texture into binding 0 ─────────────────────────────────────
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = tex->sampler();
    imageInfo.imageView   = tex->view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);

    // ── 6. Upload quad vertex data ──────────────────────────────────────────
    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    FP_CORE_INFO("GridRenderer initialized (sprite='{}', uv=[{:.3f},{:.3f} .. {:.3f},{:.3f}])",
                 tileSprite.c_str(),
                 m_uvRect[0], m_uvRect[1], m_uvRect[2], m_uvRect[3]);
}

void GridRenderer::shutdown(const VulkanContext& ctx) {
    // m_descriptorLayout and m_descriptorSet are owned by DescriptorAllocator
    m_quadBuffer.destroy(ctx);
    m_pipeline.shutdown(ctx);
}

void GridRenderer::draw(VkCommandBuffer cmd, const Swapchain& swapchain,
                        const GridMap& map) const {
    const VkExtent2D ext = swapchain.extent();
    const float w = static_cast<float>(ext.width);
    const float h = static_cast<float>(ext.height);

    const float cellW = 2.f / static_cast<float>(map.width());
    const float cellH = 2.f / static_cast<float>(map.height());

    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, ext };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(),
                            0,
                            1, &m_descriptorSet,
                            0, nullptr);

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    for (int32_t row = 0; row < map.height(); ++row) {
        for (int32_t col = 0; col < map.width(); ++col) {
            const Tile& tile = map.at(col, row);

            const float ndcX = -1.f + cellW * (static_cast<float>(col) + 0.5f);
            const float ndcY = -1.f + cellH * (static_cast<float>(row) + 0.5f);

            TileColor c = colorForType(tile.type);

            GridPushConstants pc{};
            pc.offsetX = ndcX;
            pc.offsetY = ndcY;
            pc.scaleX  = cellW;
            pc.scaleY  = cellH;
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
