#include "BrightImage.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"

#include <stdexcept>

namespace engine {

void BrightImage::init(const VulkanContext& ctx, VkExtent2D extent) {
    create(ctx, extent);
    FP_CORE_INFO("BrightImage created ({}x{})", extent.width, extent.height);
}

void BrightImage::shutdown(const VulkanContext& ctx) {
    destroy(ctx);
    FP_CORE_TRACE("BrightImage destroyed");
}

void BrightImage::recreate(const VulkanContext& ctx, VkExtent2D extent) {
    destroy(ctx);
    if (extent.width == 0 || extent.height == 0) {
        m_extent = { 0, 0 };
        return;
    }
    create(ctx, extent);
    FP_CORE_TRACE("BrightImage recreated ({}x{})", extent.width, extent.height);
}

void BrightImage::create(const VulkanContext& ctx, VkExtent2D extent) {
    m_extent = extent;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = kFormat;
    ici.extent        = { extent.width, extent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    // STORAGE: compute shader writes via image2D.
    // SAMPLED: fragment shader reads via texture() in a later pass.
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator(), &ici, &aci,
                       &m_image, &m_allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("BrightImage: vmaCreateImage failed");

    VkImageViewCreateInfo ivci{};
    ivci.sType                        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image                        = m_image;
    ivci.viewType                     = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format                       = kFormat;
    ivci.subresourceRange.aspectMask  = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount  = 1;
    ivci.subresourceRange.layerCount  = 1;
    if (vkCreateImageView(ctx.device(), &ivci, nullptr, &m_view) != VK_SUCCESS)
        throw std::runtime_error("BrightImage: vkCreateImageView failed");

    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(ctx.device(), &sci, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("BrightImage: vkCreateSampler failed");
}

void BrightImage::destroy(const VulkanContext& ctx) {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.device(), m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.device(), m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.allocator(), m_image, m_allocation);
        m_image      = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    m_extent = { 0, 0 };
}

} // namespace engine
