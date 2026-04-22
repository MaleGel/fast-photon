#include "Input.h"
#include <SDL2/SDL.h>
#include <cstring>

namespace engine {

const uint8_t* Input::s_keysCurrent = nullptr;
uint8_t        Input::s_keysPrev[512] = {};

uint32_t Input::s_mouseCurrent = 0;
uint32_t Input::s_mousePrev    = 0;
int32_t  Input::s_mouseX       = 0;
int32_t  Input::s_mouseY       = 0;
int32_t  Input::s_mousePrevX   = 0;
int32_t  Input::s_mousePrevY   = 0;

void Input::beginFrame() {
    if (s_keysCurrent)
        std::memcpy(s_keysPrev, s_keysCurrent, sizeof(s_keysPrev));

    s_keysCurrent = SDL_GetKeyboardState(nullptr);

    s_mousePrev  = s_mouseCurrent;
    s_mousePrevX = s_mouseX;
    s_mousePrevY = s_mouseY;
    s_mouseCurrent = SDL_GetMouseState(&s_mouseX, &s_mouseY);
}

void Input::processEvent(const SDL_Event& /*event*/) {
    // Reserved for scroll wheel and other event-driven input
}

bool Input::isKeyDown(KeyCode key) {
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    return sc < 512 && s_keysCurrent && s_keysCurrent[sc];
}

bool Input::isKeyPressed(KeyCode key) {
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    return sc < 512 && s_keysCurrent && s_keysCurrent[sc] && !s_keysPrev[sc];
}

bool Input::isKeyReleased(KeyCode key) {
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    return sc < 512 && s_keysCurrent && !s_keysCurrent[sc] && s_keysPrev[sc];
}

bool Input::isMouseDown(MouseBtn btn) {
    return (s_mouseCurrent & SDL_BUTTON(btn)) != 0;
}

bool Input::isMousePressed(MouseBtn btn) {
    return (s_mouseCurrent & SDL_BUTTON(btn)) != 0 &&
           (s_mousePrev    & SDL_BUTTON(btn)) == 0;
}

bool Input::isMouseReleased(MouseBtn btn) {
    return (s_mouseCurrent & SDL_BUTTON(btn)) == 0 &&
           (s_mousePrev    & SDL_BUTTON(btn)) != 0;
}

int32_t Input::getMouseX()      { return s_mouseX; }
int32_t Input::getMouseY()      { return s_mouseY; }
int32_t Input::getMouseDeltaX() { return s_mouseX - s_mousePrevX; }
int32_t Input::getMouseDeltaY() { return s_mouseY - s_mousePrevY; }

} // namespace engine
