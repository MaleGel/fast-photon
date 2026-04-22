#pragma once
#include <cstdint>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>

union SDL_Event;

namespace engine {

class EventBus;

// SDL type aliases — avoids SDL_ prefix leaking into game code
using KeyCode   = SDL_Keycode;
using MouseBtn  = uint8_t;

namespace Mouse {
    inline constexpr MouseBtn Left   = 1;  // SDL_BUTTON_LEFT
    inline constexpr MouseBtn Middle = 2;  // SDL_BUTTON_MIDDLE
    inline constexpr MouseBtn Right  = 3;  // SDL_BUTTON_RIGHT
}

// Hybrid Input: edge transitions (press/release) are published as events;
// continuous state (is-held / absolute position) stays pollable.
// Subscribers:   KeyPressedEvent, MouseMovedEvent, etc — see InputEvents.h
// Polling API:   isKeyDown(), getMouseX(), etc
class Input {
public:
    static void beginFrame();

    // Forward one SDL event. Publishes the appropriate input event on 'bus'.
    static void processEvent(const SDL_Event& event, EventBus& bus);

    static bool isKeyDown(KeyCode key);
    static bool isMouseDown(MouseBtn btn);

    static int32_t getMouseX();
    static int32_t getMouseY();
    static int32_t getMouseDeltaX();
    static int32_t getMouseDeltaY();

private:
    // index = SDL_Scancode, value = 1/0
    static const uint8_t* s_keysCurrent;

    static uint32_t s_mouseCurrent;
    static int32_t  s_mouseX,  s_mouseY;
    static int32_t  s_mousePrevX, s_mousePrevY;
};

} // namespace engine

// Polling helpers — edge-triggered checks now come from InputEvents.
#define FP_KEY_DOWN(key)    engine::Input::isKeyDown(key)
#define FP_MOUSE_DOWN(btn)  engine::Input::isMouseDown(btn)
#define FP_MOUSE_X()        engine::Input::getMouseX()
#define FP_MOUSE_Y()        engine::Input::getMouseY()
