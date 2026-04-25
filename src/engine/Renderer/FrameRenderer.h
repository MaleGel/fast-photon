#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <functional>

namespace engine {

class VulkanContext;
class Swapchain;
class RenderPass;
class SwapchainRenderPass;
class BrightImage;
class EventBus;

// Owns per-frame Vulkan synchronisation and command buffers.
//
// Double-buffered: we keep kMaxFramesInFlight parallel "frame slots", each
// with its own command buffer, fence and semaphore pair. While the GPU is
// rendering slot 0, the CPU can already record slot 1. When slot 1 is done
// recording, we ping-pong back to slot 0 — at which point slot 0's GPU
// work is (hopefully) complete and its fence has been signalled.
//
// Render flow, per frame:
//   1. beginFrame() — wait on this slot's fence, acquire next swapchain image.
//      At this point all CPU-visible resources touched last time by this
//      slot (UBOs etc.) are safe to overwrite.
//   2. Caller runs its renderer.submit() logic — writes UBOs, builds queues.
//   3. render() — records both render passes into this slot's cmd buffer
//      and submits. Signalling the slot's fence happens on GPU completion;
//      the next time this slot is reused, beginFrame() blocks on that fence.
class FrameRenderer {
public:
    // 1 keeps input → display latency at the minimum (one vsync). 2 lets
    // the CPU build frame N+1 while the GPU is still on N — useful when
    // the CPU is the bottleneck, but on this scene the CPU finishes far
    // under the vsync window, so the extra slot only buys input lag.
    // Bump back to 2 if profiler ever shows CPU-bound frames.
    static constexpr uint32_t kMaxFramesInFlight = 1;

    using RecordFn = std::function<void(VkCommandBuffer)>;

    void init(VulkanContext& ctx, EventBus& bus);
    void shutdown(VulkanContext& ctx);

    // Wait until the current slot's GPU work is finished, then acquire the
    // next swapchain image. Returns false if the frame was skipped (swapchain
    // out of date or not presentable) — caller should not proceed to render().
    bool beginFrame(VulkanContext& ctx, Swapchain& swapchain);

    // Record + submit + present. beginFrame() must have returned true.
    //
    // Four caller-supplied phases:
    //   recordPreCompute  — outside any render pass, *before* the scene
    //                       pass. For compute that produces buffers
    //                       consumed by scene rendering (VFX simulate).
    //   recordScene       — inside the MSAA HDR render pass.
    //   recordPostCompute — outside any render pass, *between* scene and
    //                       swap. brightImage is in GENERAL layout for
    //                       the duration; scene HDR is SHADER_READ_ONLY.
    //   recordSwap        — inside the 1× swapchain render pass.
    void render(VulkanContext& ctx, Swapchain& swapchain,
                RenderPass& sceneRenderPass, SwapchainRenderPass& swapRenderPass,
                BrightImage& brightImage,
                const std::array<float, 4>& clearColor,
                const RecordFn& recordPreCompute,
                const RecordFn& recordScene,
                const RecordFn& recordPostCompute,
                const RecordFn& recordSwap);

private:
    struct FrameSlot {
        VkCommandBuffer cmdBuffer      = VK_NULL_HANDLE;
        VkSemaphore     imageAvailable = VK_NULL_HANDLE;
        VkSemaphore     renderFinished = VK_NULL_HANDLE;
        VkFence         inFlight       = VK_NULL_HANDLE;
    };

    VkCommandPool   m_cmdPool = VK_NULL_HANDLE;
    std::array<FrameSlot, kMaxFramesInFlight> m_slots{};

    uint32_t m_currentSlot = 0;      // cycles 0..kMaxFramesInFlight-1
    uint32_t m_imageIndex  = 0;      // swapchain image for this frame
    bool     m_frameActive = false;  // beginFrame succeeded → render() allowed

    EventBus* m_bus = nullptr;
};

} // namespace engine
