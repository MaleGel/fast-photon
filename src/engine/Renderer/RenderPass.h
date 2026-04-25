#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace engine {

class VulkanContext;
class Swapchain;
class HdrTarget;

// Scene render pass: MSAA HDR color + MSAA depth/stencil, with an automatic
// resolve into a single-sample HDR target (see HdrTarget). The resolved
// image is then sampled by the swapchain pass for post-processing.
//
// Unlike earlier revisions, this pass does NOT write to the swapchain image
// directly — the swapchain pass is responsible for that. A single
// framebuffer covers the whole pass, because the resolve target is a single
// persistent image (not one per swapchain image).
class RenderPass {
public:
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    void init(const VulkanContext& ctx, const Swapchain& swapchain,
              const HdrTarget& hdr);
    void shutdown(const VulkanContext& ctx);

    // Rebuild MSAA attachments + framebuffer to match the swapchain's new
    // extent. The HDR target is passed in because the framebuffer binds
    // its view — the caller is responsible for recreating HdrTarget first.
    void recreateFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain,
                              const HdrTarget& hdr);

    VkRenderPass   handle()      const { return m_renderPass;  }
    VkFramebuffer  framebuffer() const { return m_framebuffer; }

private:
    void createRenderPass(const VulkanContext& ctx, const Swapchain& swapchain);
    void createAttachments(const VulkanContext& ctx, const Swapchain& swapchain);
    void destroyAttachments(const VulkanContext& ctx);
    void createFramebuffer(const VulkanContext& ctx, const Swapchain& swapchain,
                           const HdrTarget& hdr);
    void destroyFramebuffer(const VulkanContext& ctx);

    VkRenderPass   m_renderPass  = VK_NULL_HANDLE;
    VkFramebuffer  m_framebuffer = VK_NULL_HANDLE;

    VkImage        m_colorImage      = VK_NULL_HANDLE;
    VmaAllocation  m_colorAllocation = VK_NULL_HANDLE;
    VkImageView    m_colorView       = VK_NULL_HANDLE;

    VkImage        m_depthImage      = VK_NULL_HANDLE;
    VmaAllocation  m_depthAllocation = VK_NULL_HANDLE;
    VkImageView    m_depthView       = VK_NULL_HANDLE;
};

} // namespace engine
