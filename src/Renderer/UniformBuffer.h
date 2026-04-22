#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstddef>

namespace engine {

class VulkanContext;

// VMA-backed uniform buffer, HOST_VISIBLE + HOST_COHERENT + persistently
// mapped. Designed for per-frame constants (camera VP, time, ...) that
// change every frame.
//
// Usage:
//     UniformBuffer ubo;
//     ubo.init(ctx, sizeof(MyData));
//     MyData* data = ubo.mapped<MyData>();
//     *data = { ... };            // direct write — no flush thanks to COHERENT
class UniformBuffer {
public:
    void init(const VulkanContext& ctx, VkDeviceSize sizeBytes);
    void destroy(const VulkanContext& ctx);

    VkBuffer     handle()    const { return m_buffer;    }
    VkDeviceSize sizeBytes() const { return m_sizeBytes; }
    void*        mappedPtr() const { return m_mapped;    }

    template<typename T>
    T* mapped() const { return static_cast<T*>(m_mapped); }

private:
    VkBuffer      m_buffer     = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize  m_sizeBytes  = 0;
    void*         m_mapped     = nullptr;
};

} // namespace engine
