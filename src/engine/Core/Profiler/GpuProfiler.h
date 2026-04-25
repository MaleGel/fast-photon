#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;

// GPU-side scope profiler. Uses vkCmdWriteTimestamp pairs, recorded into
// a per-frame-slot query pool, and resolved when that slot comes back
// around — so GPU timings displayed on screen are always 1–2 CPU frames
// behind. That's intrinsic to timestamp queries, not a bug.
//
// Usage, inside a recorded command buffer:
//     {
//         FP_GPU_SCOPE(cmd, "Scene pass");
//         vkCmdBeginRenderPass(...); ... vkCmdEndRenderPass(...);
//     }
//
// Lifecycle, per frame:
//     beginFrame(cmd, slot) — reset pool for 'slot', collect results from
//         the slot we're about to overwrite (ready now since we've since
//         run kMaxFramesInFlight - 1 frames).
//     ... FP_GPU_SCOPE scopes record pairs of timestamps ...
//     endFrame() — finalise the frame's scope list into s_samples.
class GpuProfiler {
public:
    // Matches FrameRenderer::kMaxFramesInFlight. Kept in sync manually — if
    // that changes, this must too.
    static constexpr uint32_t kMaxFramesInFlight = 2;

    // Hard cap on timestamp pairs per frame. One scope = two timestamps.
    static constexpr uint32_t kMaxScopesPerFrame = 32;

    // Sliding-window length for the smoothed view (same policy as the CPU
    // profiler for UI consistency).
    static constexpr size_t kSmoothingWindow = 60;

    struct Sample {
        const char* name;
        double      durationMs;
        uint16_t    depth;
    };

    static bool init(VulkanContext& ctx);
    static void shutdown(VulkanContext& ctx);

    // Called by FrameRenderer once per frame, after vkBeginCommandBuffer
    // and before any FP_GPU_SCOPE. 'slot' is the current frame slot index.
    static void beginFrame(VkCommandBuffer cmd, uint32_t slot);

    // Called after the last GPU scope of this frame closes (before
    // vkEndCommandBuffer).
    static void endFrame();

    // Internal — FP_GPU_SCOPE uses these.
    static void pushScope(VkCommandBuffer cmd, const char* name);
    static void popScope (VkCommandBuffer cmd);

    // Returns true when the subsystem initialised successfully — some GPUs
    // (or queue families) don't support timestamps.
    static bool enabled() { return s_enabled; }

    // Read-only views for the UI. Samples here describe the most recently
    // *resolved* frame, which is kMaxFramesInFlight - 1 frames behind CPU.
    static const std::vector<Sample>& latestSamples()   { return s_samples;         }
    static const std::vector<Sample>& smoothedSamples() { return s_smoothedSamples; }

private:
    struct PendingScope {
        const char* name;
        uint32_t    beginQuery;   // index into s_slots[slot].pool
        uint32_t    endQuery;
        uint16_t    depth;
    };

    struct FrameSlot {
        VkQueryPool               pool = VK_NULL_HANDLE;
        uint32_t                  nextQuery = 0;   // grows during recording
        std::vector<PendingScope> scopes;          // flat, in begin order
        bool                      hasData = false; // skip resolve before first use
    };

    struct ScopeWindow {
        std::array<double, kSmoothingWindow> ring{};
        size_t head  = 0;
        size_t count = 0;
        double sum   = 0.0;
    };

    static void resolveSlot(FrameSlot& slot);

    static bool                  s_enabled;
    static VkDevice              s_device;
    static float                 s_timestampPeriodNs;  // ns per tick

    static std::array<FrameSlot, kMaxFramesInFlight> s_slots;
    static uint32_t              s_currentSlot;

    // Scope stack for the frame currently being recorded.
    static std::vector<uint32_t> s_stack;

    // Latest resolved frame's samples (displayed in raw view).
    static std::vector<Sample>   s_samples;

    // Per-scope rolling window + built smoothed view (same shape as s_samples).
    static std::unordered_map<const char*, ScopeWindow> s_scopeWindows;
    static std::vector<Sample>   s_smoothedSamples;
};

// RAII helper — pushScope on construction, popScope on destruction. Matches
// the CPU FP_PROFILE_SCOPE ergonomics.
class GpuScope {
public:
    GpuScope(VkCommandBuffer cmd, const char* name) : m_cmd(cmd) {
        GpuProfiler::pushScope(m_cmd, name);
    }
    ~GpuScope() { GpuProfiler::popScope(m_cmd); }
    GpuScope(const GpuScope&)            = delete;
    GpuScope& operator=(const GpuScope&) = delete;

private:
    VkCommandBuffer m_cmd;
};

} // namespace engine

#define FP_GPU_CONCAT_INNER(a, b) a##b
#define FP_GPU_CONCAT(a, b)       FP_GPU_CONCAT_INNER(a, b)
#define FP_GPU_SCOPE(cmd, name) \
    ::engine::GpuScope FP_GPU_CONCAT(_gpu_scope_, __LINE__){ cmd, name }
