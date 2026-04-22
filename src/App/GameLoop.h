#pragma once
#include <functional>

namespace engine {

class AppController;

// Callbacks the GameLoop fires in a fixed order every iteration.
// Leaving a callback null skips that phase — the loop still advances.
struct GameLoopCallbacks {
    // Drain OS/window events (SDL_PollEvent, Input dispatch, ImGui processing).
    std::function<void()>      onPumpEvents;

    // Deterministic simulation step. May fire 0..N times per iteration to
    // catch up with real elapsed time. 'fixedDt' is constant.
    std::function<void(float)> onFixedUpdate;

    // Presentation-domain update, fires exactly once per iteration.
    // 'realDt' is the real elapsed time since the last iteration, clamped
    // to Time's max delta.
    std::function<void(float)> onUpdate;

    // Render one frame. 'alpha' is the interpolation factor (0..1) between
    // the latest and previous fixed snapshots — use it for smooth visuals.
    std::function<void(float)> onRender;
};

// Thin orchestrator around the Time subsystem. Implements the classic
// Gregory loop: pump → fixed-catchup → variable-update → render, honouring
// AppController::shouldQuit() as the termination condition.
class GameLoop {
public:
    void run(AppController& app, const GameLoopCallbacks& cb);
};

} // namespace engine
