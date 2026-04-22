#include "Texture.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stb_image.h>
#include <cstring>
#include <stdexcept>

namespace engine {

// ── Helpers ───────────────────────────────────────────────────────────────────

// One-time command buffer for layout transitions and copies.
// Blocking: submit → wait idle → destroy. Fine for load-time uploads.
static VkCommandBuffer beginOneTimeCmd(const VulkanContext& ctx, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.device(), &ai, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

static void endOneTimeCmd(const VulkanContext& ctx, VkCommandPool pool, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx.graphicsQueue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue());

    vkFreeCommandBuffers(ctx.device(), pool, 1, &cmd);
}

// Image layout transition via pipeline barrier.
// Layouts matter: UNDEFINED → TRANSFER_DST (for vkCmdCopyBufferToImage),
// then TRANSFER_DST → SHADER_READ_ONLY (so fragment shader can sample it).
static void transitionLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        FP_CORE_ASSERT(false, "Unsupported image layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ── Public ────────────────────────────────────────────────────────────────────

bool Texture::loadFromFile(const VulkanContext& ctx, const std::string& path) {
    // Force 4 channels (RGBA8) — simplest Vulkan format, widest GPU support.
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        FP_CORE_ERROR("Failed to load texture '{}': {}", path, stbi_failure_reason());
        return false;
    }

    m_width  = static_cast<uint32_t>(w);
    m_height = static_cast<uint32_t>(h);
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    // ── Staging buffer (CPU visible, same pattern as VertexBuffer) ────────────
    VkBufferCreateInfo stagingCI{};
    stagingCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size        = imageSize;
    stagingCI.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocCI{};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer      stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc  = VK_NULL_HANDLE;
    vmaCreateBuffer(ctx.allocator(), &stagingCI, &stagingAllocCI,
                    &stagingBuffer, &stagingAlloc, nullptr);

    void* mapped = nullptr;
    vmaMapMemory(ctx.allocator(), stagingAlloc, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(ctx.allocator(), stagingAlloc);

    stbi_image_free(pixels);

    // ── Device-local image (GPU VRAM) ─────────────────────────────────────────
    VkImageCreateInfo imageCI{};
    imageCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType     = VK_IMAGE_TYPE_2D;
    imageCI.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageCI.extent.width  = m_width;
    imageCI.extent.height = m_height;
    imageCI.extent.depth  = 1;
    imageCI.mipLevels     = 1;
    imageCI.arrayLayers   = 1;
    imageCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAllocCI{};
    imageAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator(), &imageCI, &imageAllocCI,
                       &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(ctx.allocator(), stagingBuffer, stagingAlloc);
        FP_CORE_ERROR("vmaCreateImage failed for '{}'", path);
        return false;
    }

    // ── Transient command pool for upload ─────────────────────────────────────
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = ctx.graphicsFamily();
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(ctx.device(), &poolCI, nullptr, &pool);

    // UNDEFINED → TRANSFER_DST → copy → SHADER_READ_ONLY
    VkCommandBuffer cmd = beginOneTimeCmd(ctx, pool);
    transitionLayout(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0; // tightly packed
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = { 0, 0, 0 };
    region.imageExtent                     = { m_width, m_height, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transitionLayout(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    endOneTimeCmd(ctx, pool, cmd);

    vkDestroyCommandPool(ctx.device(), pool, nullptr);
    vmaDestroyBuffer(ctx.allocator(), stagingBuffer, stagingAlloc);

    // ── Image view ────────────────────────────────────────────────────────────
    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = m_image;
    viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel   = 0;
    viewCI.subresourceRange.levelCount     = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount     = 1;
    vkCreateImageView(ctx.device(), &viewCI, nullptr, &m_view);

    // ── Default sampler (linear filter, clamp-to-edge wrap) ───────────────────
    // CLAMP_TO_EDGE is the right default for atlased sprites: addressing
    // outside the sub-rect returns the edge pixel rather than the next
    // sprite / wrapped region, avoiding bleeding at neighbor boundaries.
    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter    = VK_FILTER_LINEAR;
    samplerCI.minFilter    = VK_FILTER_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.anisotropyEnable = VK_FALSE;
    samplerCI.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable    = VK_FALSE;
    samplerCI.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(ctx.device(), &samplerCI, nullptr, &m_sampler);

    FP_CORE_INFO("Texture loaded: '{}' ({}x{}, {} bytes)", path, m_width, m_height, imageSize);
    return true;
}

void Texture::destroy(const VulkanContext& ctx) {
    if (m_sampler) vkDestroySampler(ctx.device(), m_sampler, nullptr);
    if (m_view)    vkDestroyImageView(ctx.device(), m_view, nullptr);
    if (m_image)   vmaDestroyImage(ctx.allocator(), m_image, m_allocation);

    m_sampler    = VK_NULL_HANDLE;
    m_view       = VK_NULL_HANDLE;
    m_image      = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_width = m_height = 0;
}

} // namespace engine
