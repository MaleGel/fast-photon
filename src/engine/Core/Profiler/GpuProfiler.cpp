#include "GpuProfiler.h"
#include "Renderer/VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <array>

namespace engine {

// ── Static storage ──────────────────────────────────────────────────────────

bool                                GpuProfiler::s_enabled            = false;
VkDevice                            GpuProfiler::s_device             = VK_NULL_HANDLE;
float                               GpuProfiler::s_timestampPeriodNs  = 0.0f;

std::array<GpuProfiler::FrameSlot, GpuProfiler::kMaxFramesInFlight>
                                    GpuProfiler::s_slots{};
uint32_t                            GpuProfiler::s_currentSlot = 0;

std::vector<uint32_t>               GpuProfiler::s_stack;
std::vector<GpuProfiler::Sample>    GpuProfiler::s_samples;

std::unordered_map<const char*, GpuProfiler::ScopeWindow>
                                    GpuProfiler::s_scopeWindows;
std::vector<GpuProfiler::Sample>    GpuProfiler::s_smoothedSamples;

// Same rolling-window helper as CPU profiler.
template<typename W>
static double pushWindow(W& w, double ms) {
    if (w.count == w.ring.size()) w.sum -= w.ring[w.head];
    else                          ++w.count;
    w.ring[w.head] = ms;
    w.sum        += ms;
    w.head        = (w.head + 1) % w.ring.size();
    return w.sum / static_cast<double>(w.count);
}

// ── Init / shutdown ─────────────────────────────────────────────────────────

bool GpuProfiler::init(VulkanContext& ctx) {
    s_device = ctx.device();

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &props);
    s_timestampPeriodNs = props.limits.timestampPeriod;

    // Check that our graphics queue actually supports timestamps. Some
    // transfer-only / compute-only queue families don't.
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qProps(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &qCount, qProps.data());

    const uint32_t validBits = qProps[ctx.graphicsFamily()].timestampValidBits;
    if (validBits == 0) {
        FP_CORE_WARN("GpuProfiler: graphics queue reports timestampValidBits=0 — disabled");
        return false;
    }
    if (s_timestampPeriodNs == 0.0f) {
        FP_CORE_WARN("GpuProfiler: timestampPeriod is 0 — disabled");
        return false;
    }

    // Each slot owns 2 × kMaxScopesPerFrame timestamps (begin + end pairs).
    VkQueryPoolCreateInfo qpci{};
    qpci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = kMaxScopesPerFrame * 2;

    for (auto& slot : s_slots) {
        if (vkCreateQueryPool(s_device, &qpci, nullptr, &slot.pool) != VK_SUCCESS) {
            FP_CORE_ERROR("GpuProfiler: vkCreateQueryPool failed");
            return false;
        }
    }

    s_enabled = true;
    FP_CORE_INFO("GpuProfiler initialized (timestampPeriod={} ns)", s_timestampPeriodNs);
    return true;
}

void GpuProfiler::shutdown(VulkanContext& ctx) {
    for (auto& slot : s_slots) {
        if (slot.pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(ctx.device(), slot.pool, nullptr);
            slot.pool = VK_NULL_HANDLE;
        }
        slot.scopes.clear();
        slot.hasData   = false;
        slot.nextQuery = 0;
    }
    s_stack.clear();
    s_samples.clear();
    s_smoothedSamples.clear();
    s_scopeWindows.clear();
    s_enabled = false;
    s_device  = VK_NULL_HANDLE;
    FP_CORE_TRACE("GpuProfiler destroyed");
}

// ── Frame lifecycle ─────────────────────────────────────────────────────────

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t slot) {
    if (!s_enabled) return;
    FP_CORE_ASSERT(slot < kMaxFramesInFlight, "GpuProfiler: slot {} out of range", slot);

    s_currentSlot = slot;
    FrameSlot& s = s_slots[slot];

    // This slot cycled back around: its previous GPU work is finished
    // (FrameRenderer waited on its fence in beginFrame), so the queries
    // written last time round are readable now.
    if (s.hasData) {
        resolveSlot(s);
        s.hasData = false;
    }

    // Reset must be done *on the command buffer* — the pool cannot be reset
    // on the host without vkResetQueryPool (Vulkan 1.2+ host query reset
    // extension). Recording the reset here keeps the API version minimum low.
    vkCmdResetQueryPool(cmd, s.pool, 0, kMaxScopesPerFrame * 2);

    s.nextQuery = 0;
    s.scopes.clear();
    s_stack.clear();
}

void GpuProfiler::endFrame() {
    if (!s_enabled) return;
    FP_CORE_ASSERT(s_stack.empty(),
                   "GpuProfiler: endFrame with {} open scope(s)", s_stack.size());
    // Mark the slot ready to resolve next time it's used. We do NOT resolve
    // here — the GPU hasn't finished the work yet.
    s_slots[s_currentSlot].hasData = true;
}

// ── Scope push/pop ──────────────────────────────────────────────────────────

void GpuProfiler::pushScope(VkCommandBuffer cmd, const char* name) {
    if (!s_enabled) return;
    FrameSlot& slot = s_slots[s_currentSlot];

    // Two queries per scope (begin + end). If we'd overflow, drop the scope
    // silently — better than corrupting the pool indices.
    if (slot.nextQuery + 2 > kMaxScopesPerFrame * 2) return;

    PendingScope ps;
    ps.name       = name;
    ps.beginQuery = slot.nextQuery++;
    ps.endQuery   = slot.nextQuery++;
    ps.depth      = static_cast<uint16_t>(s_stack.size());

    // TOP_OF_PIPE = "as early as the GPU sees this command" — gives us the
    // latest meaningful begin. The Vulkan spec allows any pipeline stage;
    // TOP is the lightest-weight choice because it doesn't insert barriers.
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        slot.pool, ps.beginQuery);

    const uint32_t index = static_cast<uint32_t>(slot.scopes.size());
    slot.scopes.push_back(ps);
    s_stack.push_back(index);
}

void GpuProfiler::popScope(VkCommandBuffer cmd) {
    if (!s_enabled) return;
    if (s_stack.empty()) return;   // matches a silently-dropped pushScope

    FrameSlot& slot = s_slots[s_currentSlot];
    const uint32_t index = s_stack.back();
    s_stack.pop_back();

    const PendingScope& ps = slot.scopes[index];
    // BOTTOM_OF_PIPE = "after everything's done" — catches the full
    // duration of the scope's GPU work, including fragment shading.
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        slot.pool, ps.endQuery);
}

// ── Resolve queries into Sample list ────────────────────────────────────────

void GpuProfiler::resolveSlot(FrameSlot& slot) {
    const uint32_t count = slot.nextQuery;
    if (count == 0) {
        s_samples.clear();
        s_smoothedSamples.clear();
        return;
    }

    // Read as 64-bit uints. WAIT_BIT would block if queries aren't ready;
    // we've already waited on the slot's fence upstream, so all results are
    // final — no flags needed (status reliably VK_SUCCESS).
    std::vector<uint64_t> raw(count);
    VkResult res = vkGetQueryPoolResults(
        s_device, slot.pool, 0, count,
        raw.size() * sizeof(uint64_t), raw.data(),
        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (res != VK_SUCCESS) {
        // Transient availability miss — swallow it, next frame we'll try again.
        return;
    }

    s_samples.clear();
    s_samples.reserve(slot.scopes.size());
    s_smoothedSamples.clear();
    s_smoothedSamples.reserve(slot.scopes.size());

    for (const auto& ps : slot.scopes) {
        const uint64_t dt_ticks = raw[ps.endQuery] - raw[ps.beginQuery];
        const double   dt_ms    = (static_cast<double>(dt_ticks) *
                                   static_cast<double>(s_timestampPeriodNs)) / 1'000'000.0;

        s_samples.push_back({ ps.name, dt_ms, ps.depth });

        ScopeWindow& w = s_scopeWindows[ps.name];
        const double mean = pushWindow(w, dt_ms);
        s_smoothedSamples.push_back({ ps.name, mean, ps.depth });
    }
}

} // namespace engine
