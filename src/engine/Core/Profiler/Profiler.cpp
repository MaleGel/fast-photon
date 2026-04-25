#include "Profiler.h"
#include "Core/Assert/Assert.h"

namespace engine {

// ── Static state ─────────────────────────────────────────────────────────────

std::vector<Profiler::Sample>   Profiler::s_pending;
std::vector<Profiler::Sample>   Profiler::s_samples;
std::vector<Profiler::Pending>  Profiler::s_stack;

std::unordered_map<const char*, Profiler::ScopeWindow> Profiler::s_scopeWindows;
std::vector<Profiler::Sample>   Profiler::s_smoothedSamples;

std::array<double, Profiler::kHistorySize> Profiler::s_history{};
size_t                          Profiler::s_historyHead = 0;
double                          Profiler::s_lastFrameMs     = 0.0;
double                          Profiler::s_smoothedFrameMs = 0.0;
Profiler::ScopeWindow           Profiler::s_frameWindow{};
Profiler::TimePt                Profiler::s_frameStart  = {};
std::thread::id                 Profiler::s_mainThreadId{};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Push a new value into a ring + running sum. Returns the updated mean.
// Templated so it works with any ScopeWindow regardless of visibility rules.
template<typename W>
static double pushWindow(W& w, double ms) {
    if (w.count == w.ring.size()) {
        w.sum -= w.ring[w.head];
    } else {
        ++w.count;
    }
    w.ring[w.head] = ms;
    w.sum        += ms;
    w.head        = (w.head + 1) % w.ring.size();
    return w.sum / static_cast<double>(w.count);
}

// ── Frame lifecycle ──────────────────────────────────────────────────────────

void Profiler::beginFrame() {
    // First call locks in which thread is "main". Anything else is
    // treated as a worker and silently ignored by push/popScope so the
    // scope stack stays single-threaded.
    if (s_mainThreadId == std::thread::id{}) {
        s_mainThreadId = std::this_thread::get_id();
    }

    FP_CORE_ASSERT(s_stack.empty(),
                   "Profiler: {} scope(s) still open across frame boundary — missing FP_PROFILE_SCOPE close",
                   s_stack.size());

    s_pending.clear();
    s_frameStart = Clock::now();
}

void Profiler::endFrame() {
    FP_CORE_ASSERT(s_stack.empty(),
                   "Profiler: endFrame with {} scope(s) still open",
                   s_stack.size());

    const TimePt now = Clock::now();
    s_lastFrameMs = std::chrono::duration<double, std::milli>(now - s_frameStart).count();

    s_history[s_historyHead] = s_lastFrameMs;
    s_historyHead = (s_historyHead + 1) % kHistorySize;

    s_smoothedFrameMs = pushWindow(s_frameWindow, s_lastFrameMs);

    // Publish this frame's samples. swap() keeps the allocation alive.
    s_samples.swap(s_pending);
    s_pending.clear();

    // Update per-scope windows and build the smoothed view. Scopes that fired
    // multiple times this frame (e.g. executeBatch per-backend) each feed
    // their window; that's fine — each call is an independent measurement.
    s_smoothedSamples.clear();
    s_smoothedSamples.reserve(s_samples.size());
    for (const auto& s : s_samples) {
        ScopeWindow& w = s_scopeWindows[s.name];
        const double mean = pushWindow(w, s.durationMs);
        s_smoothedSamples.push_back({ s.name, mean, s.depth });
    }
}

// ── Scope push/pop ───────────────────────────────────────────────────────────

void Profiler::pushScope(const char* name) {
    // Worker threads call FP_PROFILE_SCOPE too (e.g. inside parallel_for
    // bodies). The scope state is single-threaded so we can't safely
    // record their work — silently no-op instead. Per-thread profiling
    // is a future improvement.
    if (s_mainThreadId != std::thread::id{}
        && std::this_thread::get_id() != s_mainThreadId) {
        return;
    }

    // Reserve a slot in the output list with a placeholder duration; popScope
    // will fill it in. This keeps samples in begin order, matching call flow.
    Sample placeholder{};
    placeholder.name       = name;
    placeholder.depth      = static_cast<uint16_t>(s_stack.size());
    placeholder.durationMs = 0.0;

    const size_t slot = s_pending.size();
    s_pending.push_back(placeholder);

    Pending p;
    p.name       = name;
    p.start      = Clock::now();
    p.depth      = placeholder.depth;
    p.sampleSlot = slot;
    s_stack.push_back(p);
}

void Profiler::popScope() {
    if (s_mainThreadId != std::thread::id{}
        && std::this_thread::get_id() != s_mainThreadId) {
        return;
    }
    FP_CORE_ASSERT(!s_stack.empty(), "Profiler: popScope with empty stack");

    const Pending p   = s_stack.back();
    s_stack.pop_back();

    const TimePt end  = Clock::now();
    s_pending[p.sampleSlot].durationMs =
        std::chrono::duration<double, std::milli>(end - p.start).count();
}

// ── Accessors ────────────────────────────────────────────────────────────────

const std::vector<Profiler::Sample>& Profiler::lastFrameSamples() {
    return s_samples;
}

const std::vector<Profiler::Sample>& Profiler::smoothedSamples() {
    return s_smoothedSamples;
}

const std::array<double, Profiler::kHistorySize>& Profiler::frameTimeHistory() {
    return s_history;
}

double Profiler::lastFrameMs()     { return s_lastFrameMs;     }
double Profiler::smoothedFrameMs() { return s_smoothedFrameMs; }

} // namespace engine
