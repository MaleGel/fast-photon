#pragma once
#include "ResourceTypes.h"
#include "Pipeline.h"
#include "IRenderBackend.h"
#include "Core/Events/Subscription.h"

#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;
class ResourceManager;
class DescriptorAllocator;
class HdrTarget;
class SwapchainRenderPass;
class RenderQueue;
class Swapchain;
class EventBus;

// Samples the HDR scene target and writes the final image into the
// swapchain. Step A: pure passthrough. Step B will add vignette +
// tone mapping in the post.frag shader.
//
// Draws as a single 3-vertex triangle that covers the whole screen with no
// vertex buffer — see post.vert for why one oversized triangle beats a
// two-triangle quad.
class PostProcess final : public IRenderBackend {
public:
    // Runtime-tunable parameters. Match the push_constant block in post.frag.
    // Owner (main.cpp / debug UI) mutates these directly between frames.
    struct Settings {
        float exposure          = 1.0f;
        float vignetteRadius    = 0.55f;
        float vignetteSoftness  = 0.35f;
        float vignetteIntensity = 0.6f;
    };

    Settings settings;

    void init(const VulkanContext& ctx, const SwapchainRenderPass& renderPass,
              const ResourceManager& resources, DescriptorAllocator& descriptors,
              const HdrTarget& hdr, EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // HDR target handle can change on swapchain resize (recreated). Call
    // this after a recreate so the sampler descriptor points at the fresh view.
    void onHdrRecreated(const VulkanContext& ctx, const HdrTarget& hdr);

    // Enqueue one draw on the queue. Layer is Hud−1 so post runs before any
    // UI that routes through this queue in later iterations. Step A uses a
    // dedicated queue, so the exact layer is mostly cosmetic.
    void submit(RenderQueue& queue, const Swapchain& swapchain);

    void executeBatch(VkCommandBuffer cmd,
                      const RenderCommand* commands,
                      size_t commandCount) override;

private:
    void rebuildPipeline();
    void writeSamplerDescriptor(const VulkanContext& ctx, const HdrTarget& hdr);

    Pipeline              m_pipeline;
    VkDescriptorSetLayout m_samplerLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_samplerSet    = VK_NULL_HANDLE;

    VkExtent2D            m_currentExtent = { 0, 0 };

    // Captured for the shader hot-reload path.
    const VulkanContext*        m_ctx         = nullptr;
    const SwapchainRenderPass*  m_renderPass  = nullptr;
    const ResourceManager*      m_resources   = nullptr;
    PipelineConfig              m_pipelineCfg;
    Subscription                m_shaderReloadSub;
};

} // namespace engine
