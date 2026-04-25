#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace engine {

class VulkanContext;

class Swapchain {
public:
    void init(const VulkanContext& ctx, uint32_t width, uint32_t height);
    void shutdown(const VulkanContext& ctx);

    // Destroy the current swapchain + image views and rebuild them at the
    // requested size. If width or height is 0 (minimized window), the
    // swapchain is left destroyed and canPresent() returns false until the
    // next call with a valid size.
    void recreate(const VulkanContext& ctx, uint32_t width, uint32_t height);

    // True when the swapchain is in a state that can acquire + present.
    bool canPresent() const { return m_swapchain != VK_NULL_HANDLE; }

    VkSwapchainKHR             handle()      const { return m_swapchain;  }
    VkFormat                   format()      const { return m_format;     }
    VkExtent2D                 extent()      const { return m_extent;     }
    const std::vector<VkImage>&     images()      const { return m_images;     }
    const std::vector<VkImageView>& imageViews()  const { return m_imageViews; }

    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    void createSwapchain(const VulkanContext& ctx, uint32_t width, uint32_t height);
    void createImageViews(const VulkanContext& ctx);
    void destroyResources(const VulkanContext& ctx);

    VkSwapchainKHR        m_swapchain  = VK_NULL_HANDLE;
    VkFormat              m_format     = VK_FORMAT_UNDEFINED;
    VkExtent2D            m_extent     = {};
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
};

} // namespace engine
