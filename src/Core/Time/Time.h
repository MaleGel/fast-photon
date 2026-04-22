#pragma once
#include <chrono>
#include <functional>

namespace engine {

// Timer
struct Timer {
    float     duration  = 0.0f;  // sec
    float     elapsed   = 0.0f;
    bool      looping   = false;
    bool      finished  = false;

    std::function<void()> onFinish;

    Timer() = default;
    Timer(float duration, bool looping = false, std::function<void()> cb = nullptr) : duration(duration), looping(looping), onFinish(std::move(cb)) {}

    void tick(float dt) {
        if (finished && !looping) return;

        elapsed += dt;
        if (elapsed >= duration) {
            elapsed = looping ? elapsed - duration : duration;
            finished = !looping;
            if (onFinish) onFinish();
        }
    }

    void  reset()               { elapsed = 0.0f; finished = false; }
    float progress()      const { return elapsed / duration; }         // 0.0 → 1.0
    float remaining()     const { return duration - elapsed; }
    bool  isFinished()    const { return finished; }
    bool  isRunning()     const { return !finished; }
};

// ── Time ──────────────────────────────────────────────────────────
class Time {
public:
    using Clock     = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration  = std::chrono::duration<float>;

    static void init();

    // Tick
    static void tick();

    // Getters
    static float deltaTime()      { return s_deltaTime;      }
    static float fixedDeltaTime() { return s_fixedDeltaTime; }
    static float totalTime()      { return s_totalTime;      }
    static float fps()            { return s_fps;             }
    static float smoothFps()      { return s_smoothFps;       }
    static int   frameCount()     { return s_frameCount;      }

    // Fixed timestep
    static bool  fixedUpdate();

    // How far into the next fixed step the renderer is (0..1).
    // Use for interpolating between last and current fixed-state snapshots.
    static float interpolationAlpha();

    // Settings
    static void setFixedDeltaTime(float dt)  { s_fixedDeltaTime = dt; }
    static void setMaxDeltaTime(float maxDt) { s_maxDeltaTime = maxDt; }
    static void setFpsSmoothing(float alpha) { s_fpsSmoothing = alpha; }


private:
    static TimePoint s_startTime;
    static TimePoint s_lastFrameTime;
    static float     s_deltaTime;
    static float     s_fixedDeltaTime;
    static float     s_maxDeltaTime;
    static float     s_totalTime;
    static float     s_accumulator;

    // FPS
    static float s_fps;
    static float s_smoothFps;
    static float s_fpsSmoothing;
    static int   s_frameCount;

};

} // namespace engine

#define FP_DELTA_TIME       engine::Time::deltaTime()
#define FP_FIXED_DELTA_TIME engine::Time::fixedDeltaTime()
#define FP_TOTAL_TIME       engine::Time::totalTime()
#define FP_FPS              engine::Time::fps()
#define FP_FRAME_COUNT      engine::Time::frameCount()