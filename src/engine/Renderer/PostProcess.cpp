#include "PostProcess.h"
#include "RenderQueue.h"
#include "RenderLayer.h"
#include "RendererEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "SwapchainRenderPass.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "HdrTarget.h"
#include "Core/Log/Log.h"

#include <cstring>

namespace engine {

// Empty payload — the single fullscreen triangle carries no per-draw data.
// Settings live on the PostProcess instance and are pushed as push constants.
struct PostBatchCmd {};
static_assert(sizeof(PostBatchCmd) <= RenderCommand::kPayloadSize, "");

struct alignas(16) PostPushConstants {
    float exposure;
    float vignetteRadius;
    float vignetteSoftness;
    float vignetteIntensity;
};

void PostProcess::init(const VulkanContext& ctx, const SwapchainRenderPass& renderPass,
                       const ResourceManager& resources,
                       DescriptorAllocator& descriptors,
                       const HdrTarget& hdr, EventBus& bus) {
    m_ctx        = &ctx;
    m_renderPass = &renderPass;
    m_resources  = &resources;

    // ── Descriptor: set=0 binding=0 combined-image-sampler (HDR scene) ──
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_samplerLayout = descriptors.createLayout(ctx, { samplerBinding });
    m_samplerSet    = descriptors.allocate(ctx, m_samplerLayout);

    writeSamplerDescriptor(ctx, hdr);

    // ── Pipeline ───────────────────────────────────────────────────
    m_pipelineCfg = PipelineConfig{};
    m_pipelineCfg.vertShader        = ShaderID("post_vert");
    m_pipelineCfg.fragShader        = ShaderID("post_frag");
    m_pipelineCfg.pushConstantSize  = sizeof(PostPushConstants);
    m_pipelineCfg.descriptorLayouts = { m_samplerLayout };

    // No vertex buffer: the vertex shader builds positions from gl_VertexIndex.
    m_pipelineCfg.noVertexInput = true;

    // Post runs in the swapchain pass which has no depth attachment.
    m_pipelineCfg.depthTest  = false;
    m_pipelineCfg.depthWrite = false;

    m_pipeline.init(ctx, renderPass.handle(), resources, m_pipelineCfg);

    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_pipelineCfg.vertShader || e.id == m_pipelineCfg.fragShader) {
                rebuildPipeline();
            }
        });

    FP_CORE_INFO("PostProcess initialized (passthrough)");
}

void PostProcess::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    m_pipeline.shutdown(ctx);
    FP_CORE_TRACE("PostProcess destroyed");
}

void PostProcess::onHdrRecreated(const VulkanContext& ctx, const HdrTarget& hdr) {
    writeSamplerDescriptor(ctx, hdr);
}

void PostProcess::rebuildPipeline() {
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, m_renderPass->handle(), *m_resources, m_pipelineCfg);
    FP_CORE_INFO("PostProcess: pipeline rebuilt after shader reload");
}

void PostProcess::writeSamplerDescriptor(const VulkanContext& ctx, const HdrTarget& hdr) {
    VkDescriptorImageInfo info{};
    info.sampler     = hdr.sampler();
    info.imageView   = hdr.view();
    // The scene render pass transitions the resolve attachment to
    // SHADER_READ_ONLY_OPTIMAL as its final layout, so no manual barrier.
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_samplerSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &info;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);
}

void PostProcess::submit(RenderQueue& queue, const Swapchain& swapchain) {
    FP_PROFILE_SCOPE("PostProcess::submit");
    m_currentExtent = swapchain.extent();
    queue.submit<PostBatchCmd>(this, RenderLayer::Background, /*order*/ 0,
                               /*z*/ 0.0f, PostBatchCmd{});
}

void PostProcess::executeBatch(VkCommandBuffer cmd,
                               const RenderCommand* /*commands*/,
                               size_t /*commandCount*/) {
    FP_PROFILE_SCOPE("PostProcess::executeBatch");
    FP_GPU_SCOPE(cmd, "PostProcess");

    const float w = float(m_currentExtent.width);
    const float h = float(m_currentExtent.height);
    VkViewport viewport{ 0.f, 0.f, w, h, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ { 0, 0 }, m_currentExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline.layout(), 0, 1, &m_samplerSet, 0, nullptr);

    PostPushConstants pc{};
    pc.exposure          = settings.exposure;
    pc.vignetteRadius    = settings.vignetteRadius;
    pc.vignetteSoftness  = settings.vignetteSoftness;
    pc.vignetteIntensity = settings.vignetteIntensity;
    vkCmdPushConstants(cmd, m_pipeline.layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    // 3 vertices, no vertex buffer — post.vert synthesises positions.
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace engine
