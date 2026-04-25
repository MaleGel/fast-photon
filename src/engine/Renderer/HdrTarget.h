#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace engine {

class VulkanContext;

// Offscreen 1× HDR color image used as the scene-pass resolve target. The
// scene render pass resolves MSAA color into this image, then the swapchain
// pass samples it as a texture to produce the final presented frame.
//
// Format: R16G16B16A16_SFLOAT — 16 bits per channel, half-precision floats.
// Values outside [0, 1] are preserved, which we'll need for bloom + tone
// mapping later. Step A uses passthrough sampling, so the HDR precision is
// overkill but costs nothing extra to set up now.
class HdrTarget {
public:
    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    void init(const VulkanContext& ctx, VkExtent2D extent);
    void shutdown(const VulkanContext& ctx);

    // Destroy and re-create at a new size — swapchain resize path.
    void recreate(const VulkanContext& ctx, VkExtent2D extent);

    VkImage     image()   const { return m_image;   }
    VkImageView view()    const { return m_view;    }
    VkExtent2D  extent()  const { return m_extent;  }
    VkSampler   sampler() const { return m_sampler; }

private:
    void create(const VulkanContext& ctx, VkExtent2D extent);
    void destroy(const VulkanContext& ctx);

    VkImage       m_image      = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView   m_view       = VK_NULL_HANDLE;
    // One linear sampler shared across post-processing passes — nothing here
    // needs per-draw sampler state, and creating a sampler is cheap anyway.
    VkSampler     m_sampler    = VK_NULL_HANDLE;
    VkExtent2D    m_extent     = { 0, 0 };
};

} // namespace engine
