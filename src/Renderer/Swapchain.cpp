#include "Swapchain.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <algorithm>
#include <stdexcept>

namespace engine {

void Swapchain::init(const VulkanContext& ctx, uint32_t width, uint32_t height) {
    createSwapchain(ctx, width, height);
    createImageViews(ctx);
    FP_CORE_INFO("Swapchain initialized ({}x{})", m_extent.width, m_extent.height);
}

void Swapchain::shutdown(const VulkanContext& ctx) {
    for (auto iv : m_imageViews)
        vkDestroyImageView(ctx.device(), iv, nullptr);

    vkDestroySwapchainKHR(ctx.device(), m_swapchain, nullptr);
    FP_CORE_TRACE("Swapchain destroyed");
}

// ── Private ───────────────────────────────────────────────────────

void Swapchain::createSwapchain(const VulkanContext& ctx, uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice(), ctx.surface(), &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice(), ctx.surface(), &fmtCount, nullptr);
    FP_CORE_ASSERT(fmtCount > 0, "No surface formats available");

    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice(), ctx.surface(), &fmtCount, formats.data());

    // Prefer sRGB, fall back to first available
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    m_format = chosen.format;
    m_extent = (caps.currentExtent.width != UINT32_MAX)
        ? caps.currentExtent
        : VkExtent2D{ width, height };

    uint32_t imageCount = std::min(
        caps.minImageCount + 1,
        caps.maxImageCount > 0 ? caps.maxImageCount : UINT32_MAX
    );

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = ctx.surface();
    sci.minImageCount    = imageCount;
    sci.imageFormat      = m_format;
    sci.imageColorSpace  = chosen.colorSpace;
    sci.imageExtent      = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.device(), &sci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &imgCount, m_images.data());
}

void Swapchain::createImageViews(const VulkanContext& ctx) {
    m_imageViews.resize(m_images.size());
    for (uint32_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                           = m_images[i];
        ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                          = m_format;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(ctx.device(), &ivci, nullptr, &m_imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView failed");
    }
}

} // namespace engine
