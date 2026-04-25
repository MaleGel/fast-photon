#include "VertexBuffer.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <cstring>
#include <stdexcept>

namespace engine {

void VertexBuffer::upload(const VulkanContext& ctx, const void* data, size_t sizeBytes) {
    FP_CORE_ASSERT(sizeBytes > 0, "VertexBuffer::upload called with zero size");

    m_sizeBytes = sizeBytes;

    // ── Staging buffer (CPU visible) ──────────────────────────────
    VkBufferCreateInfo stagingCI{};
    stagingCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size        = sizeBytes;
    stagingCI.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocCI{};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer      stagingBuffer     = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    if (vmaCreateBuffer(ctx.allocator(), &stagingCI, &stagingAllocCI,
                        &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create staging buffer");

    // Write data into staging buffer
    void* mapped = nullptr;
    vmaMapMemory(ctx.allocator(), stagingAllocation, &mapped);
    std::memcpy(mapped, data, sizeBytes);
    vmaUnmapMemory(ctx.allocator(), stagingAllocation);

    // ── Device-local buffer (GPU VRAM) ────────────────────────────
    VkBufferCreateInfo deviceCI{};
    deviceCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    deviceCI.size        = sizeBytes;
    deviceCI.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    deviceCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo deviceAllocCI{};
    deviceAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(ctx.allocator(), &deviceCI, &deviceAllocCI,
                        &m_buffer, &m_allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create device-local vertex buffer");

    // ── Copy staging → device via one-time command buffer ─────────
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = ctx.graphicsFamily();
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool   pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd  = VK_NULL_HANDLE;
    vkCreateCommandPool(ctx.device(), &poolCI, nullptr, &pool);

    VkCommandBufferAllocateInfo cmdAI{};
    cmdAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAI.commandPool        = pool;
    cmdAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;
    vkAllocateCommandBuffers(ctx.device(), &cmdAI, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy region{ 0, 0, sizeBytes };
    vkCmdCopyBuffer(cmd, stagingBuffer, m_buffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    // Submit and wait — upload is a one-time blocking operation
    vkQueueSubmit(ctx.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue());

    vkDestroyCommandPool(ctx.device(), pool, nullptr);
    vmaDestroyBuffer(ctx.allocator(), stagingBuffer, stagingAllocation);

    FP_CORE_INFO("VertexBuffer uploaded ({} bytes, DEVICE_LOCAL)", sizeBytes);
}

void VertexBuffer::destroy(const VulkanContext& ctx) {
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator(), m_buffer, m_allocation);
        m_buffer     = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_sizeBytes  = 0;
    }
}

} // namespace engine
