#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

class VulkanContext;
class Swapchain;

class RenderPass {
public:
    void init(const VulkanContext& ctx, const Swapchain& swapchain);
    void shutdown(const VulkanContext& ctx);

    // Rebuild framebuffers for the current swapchain image views (e.g. after
    // a swapchain recreate). The VkRenderPass itself stays the same.
    void recreateFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain);

    VkRenderPass                    handle()       const { return m_renderPass;  }
    const std::vector<VkFramebuffer>& framebuffers() const { return m_framebuffers; }

private:
    void createRenderPass(const VulkanContext& ctx, const Swapchain& swapchain);
    void createFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain);

    VkRenderPass              m_renderPass  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};

} // namespace engine
