#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <string>

namespace engine {

class VulkanContext;

class Texture {
public:
    // Load a PNG/JPG/etc. from disk, upload to DEVICE_LOCAL memory,
    // create VkImageView and a default VkSampler (linear, repeat).
    bool loadFromFile(const VulkanContext& ctx, const std::string& path);
    void destroy(const VulkanContext& ctx);

    VkImage     image()     const { return m_image;     }
    VkImageView view()      const { return m_view;      }
    VkSampler   sampler()   const { return m_sampler;   }
    uint32_t    width()     const { return m_width;     }
    uint32_t    height()    const { return m_height;    }
    bool        isValid()   const { return m_image != VK_NULL_HANDLE; }

private:
    VkImage       m_image      = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView   m_view       = VK_NULL_HANDLE;
    VkSampler     m_sampler    = VK_NULL_HANDLE;
    uint32_t      m_width      = 0;
    uint32_t      m_height     = 0;
};

} // namespace engine
