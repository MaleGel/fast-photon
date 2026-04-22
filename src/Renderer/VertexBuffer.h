#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstddef>

namespace engine {

class VulkanContext;

class VertexBuffer {
public:
    // Upload vertex data to DEVICE_LOCAL memory via staging buffer
    void upload(const VulkanContext& ctx, const void* data, size_t sizeBytes);
    void destroy(const VulkanContext& ctx);

    VkBuffer handle()      const { return m_buffer;    }
    size_t   sizeBytes()   const { return m_sizeBytes; }
    bool     isValid()     const { return m_buffer != VK_NULL_HANDLE; }

private:
    VkBuffer      m_buffer      = VK_NULL_HANDLE;
    VmaAllocation m_allocation  = VK_NULL_HANDLE;
    size_t        m_sizeBytes   = 0;
};

} // namespace engine
