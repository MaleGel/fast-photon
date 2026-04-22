#include "Time.h"
#include "Core/Log/Log.h"

namespace engine {

Time::TimePoint Time::s_startTime;
Time::TimePoint Time::s_lastFrameTime;

float Time::s_deltaTime      = 0.0f;
float Time::s_fixedDeltaTime = 1.0f / 60.0f;  // 60 Hz by default
float Time::s_maxDeltaTime   = 0.25f;          // max 250ms
float Time::s_totalTime      = 0.0f;
float Time::s_accumulator    = 0.0f;

float Time::s_fps            = 0.0f;
float Time::s_smoothFps      = 0.0f;
float Time::s_fpsSmoothing   = 0.9f;
int   Time::s_frameCount     = 0;

// ── init ──────────────────────────────────────────────────────────
void Time::init() {
    s_startTime     = Clock::now();
    s_lastFrameTime = s_startTime;
    s_accumulator   = 0.0f;
    s_frameCount    = 0;
    s_totalTime     = 0.0f;

    FP_CORE_INFO("Time system initialized (fixedDt={:.4f}s, maxDt={:.4f}s)", s_fixedDeltaTime, s_maxDeltaTime);
}

// ── tick ──────────────────────────────────────────────────────────
void Time::tick() {
    auto  now      = Clock::now();
    float raw_dt   = Duration(now - s_lastFrameTime).count();
    s_lastFrameTime = now;

    s_deltaTime  = (raw_dt > s_maxDeltaTime) ? s_maxDeltaTime : raw_dt;
    s_totalTime += s_deltaTime;
    s_accumulator += s_deltaTime;
    ++s_frameCount;

    s_fps = (s_deltaTime > 0.0f) ? 1.0f / s_deltaTime : 0.0f;

    s_smoothFps = s_fpsSmoothing * s_smoothFps + (1.0f - s_fpsSmoothing) * s_fps;
}

bool Time::fixedUpdate() {
    if (s_accumulator >= s_fixedDeltaTime) {
        s_accumulator -= s_fixedDeltaTime;
        return true;
    }
    return false;
}


} // namespace engine