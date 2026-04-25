#pragma once
#include "ResourceTypes.h"
#include "ComputePipeline.h"
#include "Core/Events/Subscription.h"

#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;
class ResourceManager;
class DescriptorAllocator;
class HdrTarget;
class BrightImage;
class EventBus;

// Compute backend that reads the resolved HDR scene and writes a "brights
// only" image with the same format. Currently a one-pass extract — the
// blur + composite stages will sit on top of this in a future step.
//
// Lifecycle inside a frame:
//     // (after scene render pass ends; HDR resolve is SHADER_READ_ONLY)
//     pass.record(cmd);
//     // (before swap pass; bright image lives in GENERAL, post.frag may
//     // sample it via SHADER_READ_ONLY transition done by FrameRenderer)
class BrightExtractPass {
public:
    struct Settings {
        float threshold = 1.0f;
        float softKnee  = 0.25f;
    };

    Settings settings;

    void init(const VulkanContext& ctx, const ResourceManager& resources,
              DescriptorAllocator& descriptors,
              const HdrTarget& hdr, const BrightImage& bright,
              EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // After a swapchain resize the HDR + bright views change, so the
    // descriptor set's image bindings need to point at the fresh views.
    void onAttachmentsRecreated(const VulkanContext& ctx,
                                const HdrTarget& hdr, const BrightImage& bright);

    // Record dispatches into the command buffer. Caller is responsible for
    // surrounding pipeline barriers (HDR-readable on entry, GENERAL→
    // SHADER_READ_ONLY on bright image after).
    void record(VkCommandBuffer cmd);

private:
    void rebuildPipeline();
    void writeDescriptors(const VulkanContext& ctx,
                          const HdrTarget& hdr, const BrightImage& bright);

    ComputePipeline       m_pipeline;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet       m_set    = VK_NULL_HANDLE;

    VkExtent2D            m_extent = { 0, 0 };

    // Captured for shader hot-reload.
    const VulkanContext*   m_ctx        = nullptr;
    const ResourceManager* m_resources  = nullptr;
    ComputePipelineConfig  m_config;
    Subscription           m_shaderReloadSub;
};

} // namespace engine
