#include "UniformBuffer.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stdexcept>

namespace engine {

void UniformBuffer::init(const VulkanContext& ctx, VkDeviceSize sizeBytes) {
    FP_CORE_ASSERT(sizeBytes > 0, "UniformBuffer::init called with zero size");
    m_sizeBytes = sizeBytes;

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size        = sizeBytes;
    bufferCI.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI{};
    // CPU_TO_GPU = host-visible + coherent where possible. VMA picks the
    // best available memory type. Mapped persistently so we avoid per-frame
    // map/unmap overhead.
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(ctx.allocator(), &bufferCI, &allocCI,
                        &m_buffer, &m_allocation, &allocInfo) != VK_SUCCESS)
        throw std::runtime_error("vmaCreateBuffer failed (UniformBuffer)");

    m_mapped = allocInfo.pMappedData;
    FP_CORE_ASSERT(m_mapped != nullptr, "UBO allocation did not return a mapped pointer");
    FP_CORE_TRACE("UniformBuffer created ({} bytes, persistent mapped)", sizeBytes);
}

void UniformBuffer::destroy(const VulkanContext& ctx) {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator(), m_buffer, m_allocation);
        m_buffer     = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mapped     = nullptr;
        m_sizeBytes  = 0;
    }
}

} // namespace engine
