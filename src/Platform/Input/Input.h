#pragma once
#include <cstdint>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>

union SDL_Event;

namespace engine {

// SDL type aliases — avoids SDL_ prefix leaking into game code
using KeyCode   = SDL_Keycode;
using MouseBtn  = uint8_t;

namespace Mouse {
    inline constexpr MouseBtn Left   = 1;  // SDL_BUTTON_LEFT
    inline constexpr MouseBtn Middle = 2;  // SDL_BUTTON_MIDDLE
    inline constexpr MouseBtn Right  = 3;  // SDL_BUTTON_RIGHT
}

class Input {
public:
    // Call once per frame before SDL_PollEvent
    static void beginFrame();

    // Pass every SDL_Event from the poll loop
    static void processEvent(const SDL_Event& event);

    static bool isKeyDown(KeyCode key);      // held this frame
    static bool isKeyPressed(KeyCode key);   // first frame down
    static bool isKeyReleased(KeyCode key);  // first frame up

    static bool isMouseDown(MouseBtn btn);
    static bool isMousePressed(MouseBtn btn);
    static bool isMouseReleased(MouseBtn btn);

    static int32_t getMouseX();
    static int32_t getMouseY();
    static int32_t getMouseDeltaX();
    static int32_t getMouseDeltaY();

private:
    // index = SDL_Scancode, value = 1/0
    static const uint8_t* s_keysCurrent;
    static uint8_t        s_keysPrev[512];

    static uint32_t s_mouseCurrent;
    static uint32_t s_mousePrev;
    static int32_t  s_mouseX,  s_mouseY;
    static int32_t  s_mousePrevX, s_mousePrevY;
};

} // namespace engine

#define FP_KEY_DOWN(key)      engine::Input::isKeyDown(key)
#define FP_KEY_PRESSED(key)   engine::Input::isKeyPressed(key)
#define FP_KEY_RELEASED(key)  engine::Input::isKeyReleased(key)
#define FP_MOUSE_DOWN(btn)    engine::Input::isMouseDown(btn)
#define FP_MOUSE_PRESSED(btn) engine::Input::isMousePressed(btn)
#define FP_MOUSE_X()          engine::Input::getMouseX()
#define FP_MOUSE_Y()          engine::Input::getMouseY()
