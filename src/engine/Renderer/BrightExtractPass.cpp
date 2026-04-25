#include "BrightExtractPass.h"
#include "RendererEvents.h"
#include "VulkanContext.h"
#include "ResourceManager.h"
#include "DescriptorAllocator.h"
#include "HdrTarget.h"
#include "BrightImage.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"
#include "Core/Log/Log.h"

namespace engine {

// Workgroup size in the shader is 16×16. Dispatch counts are
// ceil(extent / 16) so we cover every pixel.
static constexpr uint32_t kWorkgroupSize = 16;

struct alignas(16) BrightPushConstants {
    float threshold;
    float softKnee;
};

void BrightExtractPass::init(const VulkanContext& ctx, const ResourceManager& resources,
                             DescriptorAllocator& descriptors,
                             const HdrTarget& hdr, const BrightImage& bright,
                             EventBus& bus) {
    m_ctx       = &ctx;
    m_resources = &resources;

    // ── Descriptor layout: HDR (sampled) + bright (storage image) ──
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    m_layout = descriptors.createLayout(ctx, { bindings[0], bindings[1] });
    m_set    = descriptors.allocate(ctx, m_layout);

    writeDescriptors(ctx, hdr, bright);

    // ── Pipeline ───────────────────────────────────────────────────
    m_config = ComputePipelineConfig{};
    m_config.computeShader     = ShaderID("bright_extract_comp");
    m_config.pushConstantSize  = sizeof(BrightPushConstants);
    m_config.descriptorLayouts = { m_layout };

    m_pipeline.init(ctx, resources, m_config);

    m_shaderReloadSub = bus.subscribe<ShaderReloadedEvent>(
        [this](const ShaderReloadedEvent& e) {
            if (e.id == m_config.computeShader) rebuildPipeline();
        });

    m_extent = bright.extent();
    FP_CORE_INFO("BrightExtractPass initialized");
}

void BrightExtractPass::shutdown(const VulkanContext& ctx) {
    m_shaderReloadSub.release();
    m_pipeline.shutdown(ctx);
    FP_CORE_TRACE("BrightExtractPass destroyed");
}

void BrightExtractPass::onAttachmentsRecreated(const VulkanContext& ctx,
                                               const HdrTarget& hdr,
                                               const BrightImage& bright) {
    writeDescriptors(ctx, hdr, bright);
    m_extent = bright.extent();
}

void BrightExtractPass::rebuildPipeline() {
    m_pipeline.shutdown(*m_ctx);
    m_pipeline.init(*m_ctx, *m_resources, m_config);
    FP_CORE_INFO("BrightExtractPass: pipeline rebuilt after shader reload");
}

void BrightExtractPass::writeDescriptors(const VulkanContext& ctx,
                                         const HdrTarget& hdr,
                                         const BrightImage& bright) {
    VkDescriptorImageInfo hdrInfo{};
    hdrInfo.sampler     = hdr.sampler();
    hdrInfo.imageView   = hdr.view();
    // The scene render pass leaves HDR resolve in SHADER_READ_ONLY_OPTIMAL,
    // matching what we declare here.
    hdrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo brightInfo{};
    brightInfo.imageView   = bright.view();
    // Storage images expect GENERAL when written. The caller transitions
    // it before our dispatch.
    brightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_set;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo      = &hdrInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_set;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo      = &brightInfo;

    vkUpdateDescriptorSets(ctx.device(), 2, writes, 0, nullptr);
}

void BrightExtractPass::record(VkCommandBuffer cmd) {
    FP_PROFILE_SCOPE("BrightExtractPass::record");
    FP_GPU_SCOPE(cmd, "BrightExtract");

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pipeline.layout(), 0, 1, &m_set, 0, nullptr);

    BrightPushConstants pc{};
    pc.threshold = settings.threshold;
    pc.softKnee  = settings.softKnee;
    vkCmdPushConstants(cmd, m_pipeline.layout(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Round up so we cover non-multiples of the workgroup; the shader's
    // bounds check throws away over-edge invocations.
    const uint32_t gx = (m_extent.width  + kWorkgroupSize - 1) / kWorkgroupSize;
    const uint32_t gy = (m_extent.height + kWorkgroupSize - 1) / kWorkgroupSize;
    vkCmdDispatch(cmd, gx, gy, 1);
}

} // namespace engine
