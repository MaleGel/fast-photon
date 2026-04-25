#include "RenderPass.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "HdrTarget.h"
#include "Core/Log/Log.h"

#include <array>
#include <stdexcept>

namespace engine {

void RenderPass::init(const VulkanContext& ctx, const Swapchain& swapchain,
                      const HdrTarget& hdr) {
    createRenderPass(ctx, swapchain);
    createAttachments(ctx, swapchain);
    createFramebuffer(ctx, swapchain, hdr);
    FP_CORE_INFO("RenderPass initialized (MSAA {}×, depth D24S8, resolve → HDR)",
                 static_cast<int>(ctx.msaaSamples()));
}

void RenderPass::shutdown(const VulkanContext& ctx) {
    destroyFramebuffer(ctx);
    destroyAttachments(ctx);
    vkDestroyRenderPass(ctx.device(), m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
    FP_CORE_TRACE("RenderPass destroyed");
}

void RenderPass::recreateFramebuffers(const VulkanContext& ctx, const Swapchain& swapchain,
                                      const HdrTarget& hdr) {
    destroyFramebuffer(ctx);
    destroyAttachments(ctx);

    if (!swapchain.canPresent()) return;

    createAttachments(ctx, swapchain);
    createFramebuffer(ctx, swapchain, hdr);
    FP_CORE_TRACE("RenderPass attachments + framebuffer recreated");
}

// ── Private ───────────────────────────────────────────────────────

void RenderPass::createRenderPass(const VulkanContext& ctx, const Swapchain& swapchain) {
    const VkSampleCountFlagBits samples = ctx.msaaSamples();

    // Attachment 0: multisampled HDR color (what pipelines draw into).
    VkAttachmentDescription color{};
    color.format         = HdrTarget::kFormat;
    color.samples        = samples;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Attachment 1: multisampled depth/stencil.
    VkAttachmentDescription depth{};
    depth.format         = kDepthFormat;
    depth.samples        = samples;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Attachment 2: single-sample HDR resolve target.
    // finalLayout SHADER_READ_ONLY_OPTIMAL so the swapchain pass can
    // sample it without a manual layout barrier.
    VkAttachmentDescription resolve{};
    resolve.format         = HdrTarget::kFormat;
    resolve.samples        = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef  { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL         };
    VkAttachmentReference depthRef  { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentReference resolveRef{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL         };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    subpass.pResolveAttachments     = &resolveRef;

    // External → subpass dependency. dstStageMask also flags FRAGMENT_SHADER
    // because the next pass will sample from the resolve, but that
    // synchronisation is a subpass-external-→-EXTERNAL barrier handled by
    // the next render pass's own dependency chain. Here we just need the
    // clears to happen before we start drawing.
    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 3> attachments{ color, depth, resolve };

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments    = attachments.data();
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (vkCreateRenderPass(ctx.device(), &rpci, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass failed");
}

void RenderPass::createAttachments(const VulkanContext& ctx, const Swapchain& swapchain) {
    const VkSampleCountFlagBits samples = ctx.msaaSamples();
    const VkExtent2D extent             = swapchain.extent();

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // ── MSAA color (HDR format) ──────────────────────────────────
    {
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = HdrTarget::kFormat;
        ici.extent        = { extent.width, extent.height, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = samples;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
                          | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vmaCreateImage(ctx.allocator(), &ici, &aci,
                           &m_colorImage, &m_colorAllocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("RenderPass: vmaCreateImage(MSAA color) failed");

        VkImageViewCreateInfo ivci{};
        ivci.sType                        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                        = m_colorImage;
        ivci.viewType                     = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                       = HdrTarget::kFormat;
        ivci.subresourceRange.aspectMask  = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount  = 1;
        ivci.subresourceRange.layerCount  = 1;

        if (vkCreateImageView(ctx.device(), &ivci, nullptr, &m_colorView) != VK_SUCCESS)
            throw std::runtime_error("RenderPass: vkCreateImageView(MSAA color) failed");
    }

    // ── MSAA depth ───────────────────────────────────────────────
    {
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = kDepthFormat;
        ici.extent        = { extent.width, extent.height, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = samples;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vmaCreateImage(ctx.allocator(), &ici, &aci,
                           &m_depthImage, &m_depthAllocation, nullptr) != VK_SUCCESS)
            throw std::runtime_error("RenderPass: vmaCreateImage(depth) failed");

        VkImageViewCreateInfo ivci{};
        ivci.sType                        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                        = m_depthImage;
        ivci.viewType                     = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                       = kDepthFormat;
        ivci.subresourceRange.aspectMask  = VK_IMAGE_ASPECT_DEPTH_BIT
                                          | VK_IMAGE_ASPECT_STENCIL_BIT;
        ivci.subresourceRange.levelCount  = 1;
        ivci.subresourceRange.layerCount  = 1;

        if (vkCreateImageView(ctx.device(), &ivci, nullptr, &m_depthView) != VK_SUCCESS)
            throw std::runtime_error("RenderPass: vkCreateImageView(depth) failed");
    }
}

void RenderPass::destroyAttachments(const VulkanContext& ctx) {
    if (m_depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.device(), m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.allocator(), m_depthImage, m_depthAllocation);
        m_depthImage      = VK_NULL_HANDLE;
        m_depthAllocation = VK_NULL_HANDLE;
    }
    if (m_colorView != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.device(), m_colorView, nullptr);
        m_colorView = VK_NULL_HANDLE;
    }
    if (m_colorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.allocator(), m_colorImage, m_colorAllocation);
        m_colorImage      = VK_NULL_HANDLE;
        m_colorAllocation = VK_NULL_HANDLE;
    }
}

void RenderPass::createFramebuffer(const VulkanContext& ctx, const Swapchain& swapchain,
                                   const HdrTarget& hdr) {
    // Attachment order matches createRenderPass:
    // 0 = MSAA HDR color, 1 = MSAA depth, 2 = HDR resolve (single-sample).
    std::array<VkImageView, 3> attachments{ m_colorView, m_depthView, hdr.view() };

    VkFramebufferCreateInfo fci{};
    fci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass      = m_renderPass;
    fci.attachmentCount = static_cast<uint32_t>(attachments.size());
    fci.pAttachments    = attachments.data();
    fci.width           = swapchain.extent().width;
    fci.height          = swapchain.extent().height;
    fci.layers          = 1;

    if (vkCreateFramebuffer(ctx.device(), &fci, nullptr, &m_framebuffer) != VK_SUCCESS)
        throw std::runtime_error("vkCreateFramebuffer(scene) failed");
}

void RenderPass::destroyFramebuffer(const VulkanContext& ctx) {
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(ctx.device(), m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
}

} // namespace engine
