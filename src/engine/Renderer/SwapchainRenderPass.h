#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class Swapchain;

// Final presentation pass. Single single-sample color attachment (the
// current swapchain image) — no depth, no MSAA. The post-process backend
// samples the scene pass's HDR target and writes here; ImGui draws on top.
//
// One framebuffer per swapchain image (unlike the scene pass which has
// one persistent HDR target).
class SwapchainRenderPass {
public:
    void init(const VulkanContext& ctx, const Swapchain& swapchain);
    void shutdown(const VulkanContext& ctx);

    // Rebuild framebuffers for a recreated swapchain.
    void recreateFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain);

    VkRenderPass                      handle()       const { return m_renderPass;   }
    const std::vector<VkFramebuffer>& framebuffers() const { return m_framebuffers; }

private:
    void createRenderPass(const VulkanContext& ctx, const Swapchain& swapchain);
    void createFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain);
    void destroyFramebuffers(const VulkanContext& ctx);

    VkRenderPass               m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};

} // namespace engine
