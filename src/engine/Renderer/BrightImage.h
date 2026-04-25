#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace engine {

class VulkanContext;

// Storage image written by a compute shader and (optionally) sampled later
// in the swapchain pass. Shares HDR's pixel format so we can keep
// brightness > 1.0 around for future bloom blur passes.
//
// Layout policy: image lives in VK_IMAGE_LAYOUT_GENERAL once it has data.
// The first transition (UNDEFINED → GENERAL) is performed by the caller
// before the first compute dispatch each frame.
class BrightImage {
public:
    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    void init(const VulkanContext& ctx, VkExtent2D extent);
    void shutdown(const VulkanContext& ctx);

    void recreate(const VulkanContext& ctx, VkExtent2D extent);

    VkImage     image()   const { return m_image;   }
    VkImageView view()    const { return m_view;    }
    VkSampler   sampler() const { return m_sampler; }
    VkExtent2D  extent()  const { return m_extent;  }

private:
    void create(const VulkanContext& ctx, VkExtent2D extent);
    void destroy(const VulkanContext& ctx);

    VkImage       m_image      = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView   m_view       = VK_NULL_HANDLE;
    VkSampler     m_sampler    = VK_NULL_HANDLE;
    VkExtent2D    m_extent     = { 0, 0 };
};

} // namespace engine
