#include "TextRenderer.h"
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
#include "Font.h"
#include "Texture.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"
#include "Core/StringUtil/Utf8.h"

#include <glm/gtc/matrix_transform.hpp>
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

struct alignas(16) TextPushConstants {
    float posX,  posY;
    float sizeX, sizeY;
    float r, g, b, a;
    float u0, v0, u1, v1;
};

struct FrameUniforms {
    glm::mat4 screenProjection;
};

// Payload carries just a handle into the per-frame string storage plus
// layout params — std::string itself cannot live in the trivially-copyable
// payload buffer.
struct TextBatchCmd {
    uint32_t stringIndex;   // index into TextRenderer::m_strings
    float    x;
    float    y;
    float    r, g, b, a;
};
static_assert(sizeof(TextBatchCmd) <= RenderCommand::kPayloadSize, "");
static_assert(std::is_trivially_copyable_v<TextBatchCmd>, "");

// ── init / shutdown ──────────────────────────────────────────────────────────

void TextRenderer::init(const VulkanContext& ctx, const RenderPass& renderPass,
                        const ResourceManager& resources,
                        DescriptorAllocator& descriptors, FontID fontId,
                        EventBus& bus) {
    m_ctx        = &ctx;
    m_renderPass = &renderPass;
    m_resources  = &resources;

    m_font = resources.getFont(fontId);
    FP_CORE_ASSERT(m_font != nullptr, "TextRenderer: font '{}' not registered", fontId.c_str());

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
    m_pipelineCfg.vertShader        = ShaderID("text_vert");
    m_pipelineCfg.fragShader        = ShaderID("text_frag");
    m_pipelineCfg.pushConstantSize  = sizeof(TextPushConstants);
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
    imageInfo.sampler     = m_font->texture().sampler();
    imageInfo.imageView   = m_font->texture().view();
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

    FP_CORE_INFO("TextRenderer initialized (font='{}')", fontId.c_str());
}

void TextRenderer::rebuildPipeline() {
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_pipelineCfg);
    FP_CORE_INFO("TextRenderer: pipeline rebuilt after shader reload");
}

void TextRenderer::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    m_frameUbo.destroy(ctx);
    m_quadBuffer.destroy(ctx);
    m_pipeline.shutdown(ctx);
    m_font = nullptr;
    m_strings.clear();
}

// ── Frame lifecycle ──────────────────────────────────────────────────────────

void TextRenderer::beginFrame(const Swapchain& swapchain) {
    m_currentExtent = swapchain.extent();

    FrameUniforms* uni = m_frameUbo.mapped<FrameUniforms>();
    uni->screenProjection = glm::ortho(0.0f, float(m_currentExtent.width),
                                       0.0f, float(m_currentExtent.height),
                                       -1.0f, 1.0f);

    m_strings.clear();
}

void TextRenderer::drawText(RenderQueue& queue, std::string_view text,
                            float x, float y, const glm::vec4& color) {
    TextBatchCmd cmd{};
    cmd.stringIndex = static_cast<uint32_t>(m_strings.size());
    cmd.x = x; cmd.y = y;
    cmd.r = color.r; cmd.g = color.g; cmd.b = color.b; cmd.a = color.a;
    m_strings.emplace_back(text);

    queue.submit<TextBatchCmd>(this, RenderLayer::Hud, /*order*/ 0,
                               /*z*/ 0.f, cmd);
}

// ── executeBatch ─────────────────────────────────────────────────────────────

void TextRenderer::executeBatch(VkCommandBuffer cmd,
                                const RenderCommand* commands,
                                size_t commandCount) {
    FP_PROFILE_SCOPE("TextRenderer::executeBatch");
    FP_GPU_SCOPE(cmd, "TextRenderer");
    if (commandCount == 0 || !m_font) return;

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

    const float texW = float(m_font->texture().width());
    const float texH = float(m_font->texture().height());

    for (size_t i = 0; i < commandCount; ++i) {
        TextBatchCmd bc;
        std::memcpy(&bc, commands[i].payload, sizeof(bc));

        FP_CORE_ASSERT(bc.stringIndex < m_strings.size(),
                       "TextRenderer: stale string index {}", bc.stringIndex);
        const std::string& text = m_strings[bc.stringIndex];

        const auto codepoints = utf8Decode(text);
        float penX = bc.x;
        const float baselineY = bc.y;

        for (char32_t cp : codepoints) {
            const Glyph* g = m_font->getGlyph(cp);
            if (!g) continue;

            if (g->w > 0 && g->h > 0) {
                TextPushConstants pc{};
                pc.posX  = penX + float(g->bearingX);
                pc.posY  = baselineY - float(g->bearingY);
                pc.sizeX = float(g->w);
                pc.sizeY = float(g->h);
                pc.r = bc.r; pc.g = bc.g; pc.b = bc.b; pc.a = bc.a;
                pc.u0 = (float(g->x)        + 0.5f) / texW;
                pc.v0 = (float(g->y)        + 0.5f) / texH;
                pc.u1 = (float(g->x + g->w) - 0.5f) / texW;
                pc.v1 = (float(g->y + g->h) - 0.5f) / texH;

                vkCmdPushConstants(cmd, m_pipeline.layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 6, 1, 0, 0);
            }

            penX += float(g->advance);
        }
    }
}

} // namespace engine
