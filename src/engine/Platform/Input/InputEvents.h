#pragma once
#include <SDL2/SDL_keycode.h>
#include <cstdint>

namespace engine {

// ── Keyboard ─────────────────────────────────────────────────────────────────

// Fired once on the frame the key transitioned from up → down.
struct KeyPressedEvent {
    SDL_Keycode key;
};

// Fired once on the frame the key transitioned from down → up.
struct KeyReleasedEvent {
    SDL_Keycode key;
};

// ── Mouse ────────────────────────────────────────────────────────────────────

struct MouseMovedEvent {
    int32_t x;    // absolute window position
    int32_t y;
    int32_t dx;   // relative to previous position
    int32_t dy;
};

struct MouseButtonPressedEvent {
    uint8_t button;   // SDL_BUTTON_LEFT / _RIGHT / _MIDDLE
    int32_t x;
    int32_t y;
};

struct MouseButtonReleasedEvent {
    uint8_t button;
    int32_t x;
    int32_t y;
};

struct MouseWheelEvent {
    int32_t dx;
    int32_t dy;
};

} // namespace engine
