#include "GridRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "Texture.h"
#include "Gameplay/GridMap/GridMap.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <array>
#include <vector>

namespace engine {

// NDC square: two triangles forming a unit quad centered at origin.
// Vertex layout: vec2 position + vec2 uv.
// UV origin (0,0) at top-left corner of the quad — matches Vulkan image coords.
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

// ── Public ────────────────────────────────────────────────────────────────────

void GridRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                        const ResourceManager& resources, DescriptorAllocator& descriptors,
                        const GridMap& /*map*/, TextureID tileTexture) {
    // ── 1. Descriptor set layout: one combined image+sampler for fragment ────
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorLayout = descriptors.createLayout(ctx, { binding });

    // ── 2. Pipeline knows about this layout ──────────────────────────────────
    PipelineConfig cfg;
    cfg.vertShader       = ShaderID("grid_vert");
    cfg.fragShader       = ShaderID("grid_frag");
    cfg.pushConstantSize = sizeof(float) * 8;          // vec2 offset + vec2 scale + vec4 color
    cfg.descriptorLayout = m_descriptorLayout;
    m_pipeline.init(ctx, renderPass, resources, cfg);

    // ── 3. Allocate the descriptor set ───────────────────────────────────────
    m_descriptorSet = descriptors.allocate(ctx, m_descriptorLayout);
    FP_CORE_ASSERT(m_descriptorSet != VK_NULL_HANDLE, "Grid descriptor set allocation failed");

    // ── 4. Write the texture into binding 0 ──────────────────────────────────
    const Texture* tex = resources.getTexture(tileTexture);
    FP_CORE_ASSERT(tex && tex->isValid(), "Tile texture '{}' not available", tileTexture.c_str());

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

    // ── 5. Upload quad vertex data ───────────────────────────────────────────
    m_quadBuffer.upload(ctx, k_unitQuad.data(), sizeof(k_unitQuad));

    FP_CORE_INFO("GridRenderer initialized (texture='{}')", tileTexture.c_str());
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

    // Bind the descriptor set — must happen AFTER vkCmdBindPipeline.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(),
                            0,                    // firstSet
                            1, &m_descriptorSet,
                            0, nullptr);          // no dynamic offsets

    VkDeviceSize offset = 0;
    VkBuffer vb = m_quadBuffer.handle();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    struct PushConstants {
        float offsetX, offsetY;
        float scaleX,  scaleY;
        float r, g, b, a;
    };

    for (int32_t row = 0; row < map.height(); ++row) {
        for (int32_t col = 0; col < map.width(); ++col) {
            const Tile& tile = map.at(col, row);

            const float ndcX = -1.f + cellW * (static_cast<float>(col) + 0.5f);
            const float ndcY = -1.f + cellH * (static_cast<float>(row) + 0.5f);

            TileColor c = colorForType(tile.type);

            PushConstants pc{};
            pc.offsetX = ndcX;
            pc.offsetY = ndcY;
            pc.scaleX  = cellW;
            pc.scaleY  = cellH;
            pc.r = c.r; pc.g = c.g; pc.b = c.b; pc.a = c.a;

            vkCmdPushConstants(cmd, m_pipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);

            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }
}

} // namespace engine
