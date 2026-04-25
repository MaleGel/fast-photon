#include "FrameRenderer.h"
#include "VulkanContext.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "SwapchainRenderPass.h"
#include "BrightImage.h"
#include "RendererEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Profiler/GpuProfiler.h"

#include <stdexcept>

namespace engine {

void FrameRenderer::init(VulkanContext& ctx, EventBus& bus) {
    m_bus = &bus;

    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = ctx.graphicsFamily();
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx.device(), &pci, nullptr, &m_cmdPool) != VK_SUCCESS)
        throw std::runtime_error("FrameRenderer: vkCreateCommandPool failed");

    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fenCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    // Signalled initial state: the first beginFrame() waits on the fence
    // but no prior submit has run. Signalled = wait returns immediately.
    fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBufferAllocateInfo abai{};
    abai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    abai.commandPool        = m_cmdPool;
    abai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    abai.commandBufferCount = 1;

    for (auto& slot : m_slots) {
        if (vkAllocateCommandBuffers(ctx.device(), &abai, &slot.cmdBuffer) != VK_SUCCESS)
            throw std::runtime_error("FrameRenderer: vkAllocateCommandBuffers failed");
        vkCreateSemaphore(ctx.device(), &semCI, nullptr, &slot.imageAvailable);
        vkCreateSemaphore(ctx.device(), &semCI, nullptr, &slot.renderFinished);
        vkCreateFence    (ctx.device(), &fenCI, nullptr, &slot.inFlight);
    }

    FP_CORE_INFO("FrameRenderer initialized ({} frames in flight)", kMaxFramesInFlight);
}

void FrameRenderer::shutdown(VulkanContext& ctx) {
    for (auto& slot : m_slots) {
        if (slot.inFlight       != VK_NULL_HANDLE) vkDestroyFence    (ctx.device(), slot.inFlight,       nullptr);
        if (slot.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(ctx.device(), slot.renderFinished, nullptr);
        if (slot.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(ctx.device(), slot.imageAvailable, nullptr);
        slot = {};
    }
    if (m_cmdPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx.device(), m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
    }
    m_bus = nullptr;
    FP_CORE_TRACE("FrameRenderer destroyed");
}

bool FrameRenderer::beginFrame(VulkanContext& ctx, Swapchain& swapchain) {
    FP_PROFILE_SCOPE("FrameRenderer::beginFrame");

    if (!swapchain.canPresent()) return false;

    FrameSlot& slot = m_slots[m_currentSlot];

    {
        // This is the safe point for CPU to touch per-slot data (UBOs etc.).
        // If the fence is already signalled (GPU idle), returns immediately.
        FP_PROFILE_SCOPE("GPU wait");
        vkWaitForFences(ctx.device(), 1, &slot.inFlight, VK_TRUE, UINT64_MAX);
    }

    VkResult acquireRes = vkAcquireNextImageKHR(
        ctx.device(), swapchain.handle(), UINT64_MAX,
        slot.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);

    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
        m_bus->publish(SwapchainOutOfDateEvent{});
        return false;
    }

    // Reset the fence only after a successful acquire — otherwise we'd reset
    // without a matching submit and the next wait would block forever.
    vkResetFences(ctx.device(), 1, &slot.inFlight);
    m_frameActive = true;
    return true;
}

void FrameRenderer::render(VulkanContext& ctx, Swapchain& swapchain,
                           RenderPass& sceneRenderPass,
                           SwapchainRenderPass& swapRenderPass,
                           BrightImage& brightImage,
                           const std::array<float, 4>& clearColor,
                           const RecordFn& recordScene,
                           const RecordFn& recordCompute,
                           const RecordFn& recordSwap) {
    FP_PROFILE_SCOPE("FrameRenderer::render");
    FP_CORE_ASSERT(m_frameActive, "FrameRenderer::render called without a successful beginFrame");

    FrameSlot& slot = m_slots[m_currentSlot];

    vkResetCommandBuffer(slot.cmdBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(slot.cmdBuffer, &beginInfo);

    // Kick off GPU profiling for this frame — query pool reset is recorded
    // into the command buffer, so this must come after vkBeginCommandBuffer.
    GpuProfiler::beginFrame(slot.cmdBuffer, m_currentSlot);

    // ── Scene pass ───────────────────────────────────────────────
    {
        FP_GPU_SCOPE(slot.cmdBuffer, "Scene pass");

        VkClearValue clears[3]{};
        clears[0].color        = {{ clearColor[0], clearColor[1], clearColor[2], clearColor[3] }};
        clears[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = sceneRenderPass.handle();
        rpBegin.framebuffer       = sceneRenderPass.framebuffer();
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount   = 3;
        rpBegin.pClearValues      = clears;

        vkCmdBeginRenderPass(slot.cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        if (recordScene) recordScene(slot.cmdBuffer);
        vkCmdEndRenderPass(slot.cmdBuffer);
    }

    // ── Compute phase ────────────────────────────────────────────
    // Bright image: UNDEFINED/SHADER_READ_ONLY → GENERAL so the compute
    // shader can imageStore into it. We also gate the compute stage on the
    // scene pass writing the HDR resolve (COLOR_ATTACHMENT_OUTPUT →
    // COMPUTE_SHADER), even though the render pass's finalLayout transition
    // does most of the layout work for HDR — this barrier is specifically
    // for the bright image, which lives outside any render pass.
    {
        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.srcAccessMask       = 0;
        toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image               = brightImage.image();
        toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toGeneral.subresourceRange.levelCount = 1;
        toGeneral.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(slot.cmdBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toGeneral);

        if (recordCompute) recordCompute(slot.cmdBuffer);

        // GENERAL → SHADER_READ_ONLY_OPTIMAL so post.frag can sample it.
        VkImageMemoryBarrier toRead{};
        toRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        toRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
        toRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image               = brightImage.image();
        toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toRead.subresourceRange.levelCount = 1;
        toRead.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(slot.cmdBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);
    }

    // ── Swapchain pass ───────────────────────────────────────────
    {
        FP_GPU_SCOPE(slot.cmdBuffer, "Swap pass");

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass        = swapRenderPass.handle();
        rpBegin.framebuffer       = swapRenderPass.framebuffers()[m_imageIndex];
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount   = 0;
        rpBegin.pClearValues      = nullptr;

        vkCmdBeginRenderPass(slot.cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        if (recordSwap) recordSwap(slot.cmdBuffer);
        vkCmdEndRenderPass(slot.cmdBuffer);
    }

    GpuProfiler::endFrame();
    vkEndCommandBuffer(slot.cmdBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &slot.imageAvailable;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &slot.cmdBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &slot.renderFinished;
    vkQueueSubmit(ctx.graphicsQueue(), 1, &submit, slot.inFlight);

    VkSwapchainKHR sc = swapchain.handle();
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &slot.renderFinished;
    present.swapchainCount     = 1;
    present.pSwapchains        = &sc;
    present.pImageIndices      = &m_imageIndex;
    VkResult presentRes = vkQueuePresentKHR(ctx.graphicsQueue(), &present);

    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
        m_bus->publish(SwapchainOutOfDateEvent{});
    }

    m_currentSlot = (m_currentSlot + 1) % kMaxFramesInFlight;
    m_frameActive = false;
}

} // namespace engine
