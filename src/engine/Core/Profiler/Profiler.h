#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

// Scope-based per-frame profiler.
//
// Usage:
//     void GridRenderer::submit(...) {
//         FP_PROFILE_SCOPE("GridRenderer::submit");
//         // ... work ...
//     }  // on scope exit, (name, duration, depth) is recorded.
//
// One instance per process — the whole runtime is single-threaded. Names
// are expected to be string literals; the profiler stores the pointer only.
class Profiler {
public:
    using Clock    = std::chrono::steady_clock;
    using TimePt   = std::chrono::time_point<Clock>;

    struct Sample {
        const char* name;
        double      durationMs;
        uint16_t    depth;   // nesting level inside the frame
    };

    // Max number of past frame durations kept for the time graph.
    static constexpr size_t kHistorySize = 120;

    // Number of frames over which per-scope durations are averaged for the
    // smoothed view. 60 frames ≈ 1 s at 60 FPS / ~0.3 s at 180 FPS — short
    // enough to notice real spikes, long enough to settle visually.
    static constexpr size_t kSmoothingWindow = 60;

    // Called by GameLoop once per frame.
    static void beginFrame();
    static void endFrame();

    // Called by FP_PROFILE_SCOPE macro — do not invoke directly.
    static void pushScope(const char* name);
    static void popScope();

    // Read-only view of the last completed frame's samples (raw durations).
    static const std::vector<Sample>& lastFrameSamples();

    // Same shape as lastFrameSamples(), but each durationMs is replaced by
    // the per-scope mean over the last kSmoothingWindow frames. Scopes that
    // disappeared this frame aren't reported — this view tracks whatever
    // was captured *this* frame. Keyed by scope name pointer (string-literal
    // identity, so two FP_PROFILE_SCOPE("Foo") calls at different sites
    // merge only if the compiler pools the literal — typically yes).
    static const std::vector<Sample>& smoothedSamples();

    // Smoothed mean of frame duration over the same window.
    static double smoothedFrameMs();

    // Rolling ring buffer of the last N frame durations, in ms.
    // Entry [0] is the oldest, [kHistorySize-1] is the most recent.
    static const std::array<double, kHistorySize>& frameTimeHistory();
    static double lastFrameMs();

private:
    struct Pending {
        const char* name;
        TimePt      start;
        uint16_t    depth;
        size_t      sampleSlot;   // index into m_samples we'll write on pop
    };

    // Double-buffered — pending list is the one currently being built,
    // samples list is the last fully captured frame (returned to UI).
    static std::vector<Sample>   s_pending;
    static std::vector<Sample>   s_samples;
    static std::vector<Pending>  s_stack;

    // Per-scope rolling sum of the last kSmoothingWindow durations (ms).
    // We keep the ring plus a running sum so the average is O(1) per frame,
    // regardless of window size.
    struct ScopeWindow {
        std::array<double, kSmoothingWindow> ring{};
        size_t head  = 0;
        size_t count = 0;       // min(frames seen, kSmoothingWindow)
        double sum   = 0.0;
    };
    static std::unordered_map<const char*, ScopeWindow> s_scopeWindows;

    // Rebuilt every frame from s_samples with durations swapped for means.
    static std::vector<Sample>   s_smoothedSamples;

    static std::array<double, kHistorySize> s_history;
    static size_t                s_historyHead;   // next write slot
    static double                s_lastFrameMs;
    static double                s_smoothedFrameMs;
    static ScopeWindow           s_frameWindow;   // smoothing for frame total
    static TimePt                s_frameStart;
};

// RAII helper used by the macro.
class ProfileScope {
public:
    explicit ProfileScope(const char* name) { Profiler::pushScope(name); }
    ~ProfileScope()                          { Profiler::popScope(); }
    ProfileScope(const ProfileScope&)            = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
};

} // namespace engine

// Token-pasting trick guarantees unique variable names if a function has
// multiple scopes at the same line (rare but possible through macros).
#define FP_PROFILE_CONCAT_INNER(a, b) a##b
#define FP_PROFILE_CONCAT(a, b)       FP_PROFILE_CONCAT_INNER(a, b)
#define FP_PROFILE_SCOPE(name) \
    ::engine::ProfileScope FP_PROFILE_CONCAT(_profile_scope_, __LINE__){ name }
