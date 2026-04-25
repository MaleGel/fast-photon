#include "GameLoop.h"
#include "AppController.h"
#include "Core/Time/Time.h"
#include "Core/Log/Log.h"
#include "Core/Profiler/Profiler.h"

namespace engine {

void GameLoop::run(AppController& app, const GameLoopCallbacks& cb) {
    FP_CORE_INFO("Entering main loop");

    while (!app.shouldQuit()) {
        Profiler::beginFrame();
        {
            FP_PROFILE_SCOPE("Frame");

            Time::tick();

            if (cb.onPumpEvents) {
                FP_PROFILE_SCOPE("onPumpEvents");
                cb.onPumpEvents();
            }

            // ── Fixed-timestep catch-up ─────────────────────────────────
            const float fixedDt = Time::fixedDeltaTime();
            if (cb.onFixedUpdate) {
                FP_PROFILE_SCOPE("onFixedUpdate");
                while (Time::fixedUpdate()) {
                    cb.onFixedUpdate(fixedDt);
                }
            }

            if (cb.onUpdate) {
                FP_PROFILE_SCOPE("onUpdate");
                cb.onUpdate(Time::deltaTime());
            }
            if (cb.onRender) {
                FP_PROFILE_SCOPE("onRender");
                cb.onRender(Time::interpolationAlpha());
            }
        }
        Profiler::endFrame();
    }

    FP_CORE_INFO("Main loop exited");
}

} // namespace engine
