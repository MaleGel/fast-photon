#include "SwapchainRenderPass.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "Core/Log/Log.h"

#include <stdexcept>

namespace engine {

void SwapchainRenderPass::init(const VulkanContext& ctx, const Swapchain& swapchain) {
    createRenderPass(ctx, swapchain);
    createFramebuffers(ctx, swapchain);
    FP_CORE_INFO("SwapchainRenderPass initialized ({} framebuffers)",
                 m_framebuffers.size());
}

void SwapchainRenderPass::shutdown(const VulkanContext& ctx) {
    destroyFramebuffers(ctx);
    vkDestroyRenderPass(ctx.device(), m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
    FP_CORE_TRACE("SwapchainRenderPass destroyed");
}

void SwapchainRenderPass::recreateFramebuffers(const VulkanContext& ctx,
                                               const Swapchain& swapchain) {
    destroyFramebuffers(ctx);
    if (!swapchain.canPresent()) return;
    createFramebuffers(ctx, swapchain);
    FP_CORE_TRACE("SwapchainRenderPass framebuffers recreated ({})",
                  m_framebuffers.size());
}

// ── Private ───────────────────────────────────────────────────────

void SwapchainRenderPass::createRenderPass(const VulkanContext& ctx,
                                           const Swapchain& swapchain) {
    VkAttachmentDescription color{};
    color.format         = swapchain.format();
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    // DONT_CARE: the post-process pass is a full-screen quad that covers
    // every pixel, so any previous content would be overwritten anyway.
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Wait until both the swapchain image is acquired AND the scene pass
    // has finished writing to the HDR target we'll sample here.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &color;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (vkCreateRenderPass(ctx.device(), &rpci, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass(swapchain) failed");
}

void SwapchainRenderPass::createFramebuffers(const VulkanContext& ctx,
                                             const Swapchain& swapchain) {
    const auto& views = swapchain.imageViews();
    m_framebuffers.resize(views.size());

    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass      = m_renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments    = &views[i];
        fci.width           = swapchain.extent().width;
        fci.height          = swapchain.extent().height;
        fci.layers          = 1;

        if (vkCreateFramebuffer(ctx.device(), &fci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer(swapchain) failed");
    }
}

void SwapchainRenderPass::destroyFramebuffers(const VulkanContext& ctx) {
    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(ctx.device(), fb, nullptr);
    m_framebuffers.clear();
}

} // namespace engine
