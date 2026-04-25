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
    // create VkImageView and a default VkSampler (linear, clamp-to-edge).
    bool loadFromFile(const VulkanContext& ctx, const std::string& path);

    // Upload an already-decoded RGBA bitmap (4 bytes/pixel, tightly packed,
    // row-major, no padding) straight to a DEVICE_LOCAL VkImage. Used by
    // the font system, where glyphs are rasterised in memory.
    bool loadFromMemoryRGBA(const VulkanContext& ctx,
                            const uint8_t* pixels,
                            uint32_t width, uint32_t height);

    void destroy(const VulkanContext& ctx);

    VkImage     image()     const { return m_image;     }
    VkImageView view()      const { return m_view;      }
    VkSampler   sampler()   const { return m_sampler;   }
    uint32_t    width()     const { return m_width;     }
    uint32_t    height()    const { return m_height;    }
    bool        isValid()   const { return m_image != VK_NULL_HANDLE; }

private:
    // Shared upload path used by both loadFromFile and loadFromMemoryRGBA.
    // Creates the VkImage + view + sampler, copies 'pixels' (w*h*4 bytes) via
    // a staging buffer, transitions the image to SHADER_READ_ONLY.
    //
    // 'srgb' = true when pixels encode perceptual (sRGB) data such as a
    // PNG photo/art. The GPU sampler then converts to linear on read, which
    // is what our HDR shader math expects. Pass false for generated data
    // (font atlas, masks, noise) — those stay linear.
    bool uploadRgba(const VulkanContext& ctx, const uint8_t* pixels,
                    uint32_t width, uint32_t height, bool srgb);

    VkImage       m_image      = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView   m_view       = VK_NULL_HANDLE;
    VkSampler     m_sampler    = VK_NULL_HANDLE;
    uint32_t      m_width      = 0;
    uint32_t      m_height     = 0;
};

} // namespace engine
