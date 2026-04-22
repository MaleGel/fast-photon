#include "GameLoop.h"
#include "AppController.h"
#include "Core/Time/Time.h"
#include "Core/Log/Log.h"

namespace engine {

void GameLoop::run(AppController& app, const GameLoopCallbacks& cb) {
    FP_CORE_INFO("Entering main loop");

    while (!app.shouldQuit()) {
        Time::tick();

        if (cb.onPumpEvents) cb.onPumpEvents();

        // ── Fixed-timestep catch-up ─────────────────────────────────
        // Time::fixedUpdate() returns true once per accumulated fixedDt.
        // Max delta inside Time::tick prevents a spiral of death after
        // a long hitch (a 2s hiccup won't trigger 120 fixed steps).
        const float fixedDt = Time::fixedDeltaTime();
        if (cb.onFixedUpdate) {
            while (Time::fixedUpdate()) {
                cb.onFixedUpdate(fixedDt);
            }
        }

        if (cb.onUpdate) cb.onUpdate(Time::deltaTime());
        if (cb.onRender) cb.onRender(Time::interpolationAlpha());
    }

    FP_CORE_INFO("Main loop exited");
}

} // namespace engine
