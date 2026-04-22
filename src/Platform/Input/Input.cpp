#include "Input.h"
#include "InputEvents.h"
#include "Core/Events/EventBus.h"

#include <SDL2/SDL.h>

namespace engine {

const uint8_t* Input::s_keysCurrent = nullptr;

uint32_t Input::s_mouseCurrent = 0;
int32_t  Input::s_mouseX       = 0;
int32_t  Input::s_mouseY       = 0;
int32_t  Input::s_mousePrevX   = 0;
int32_t  Input::s_mousePrevY   = 0;

void Input::beginFrame() {
    s_keysCurrent = SDL_GetKeyboardState(nullptr);
    s_mousePrevX  = s_mouseX;
    s_mousePrevY  = s_mouseY;
    s_mouseCurrent = SDL_GetMouseState(&s_mouseX, &s_mouseY);
}

void Input::processEvent(const SDL_Event& event, EventBus& bus) {
    switch (event.type) {
    case SDL_KEYDOWN:
        // SDL auto-repeats held keys at OS repeat rate — we only care about
        // the initial transition for edge events.
        if (event.key.repeat == 0) {
            bus.publish(KeyPressedEvent{ event.key.keysym.sym });
        }
        break;

    case SDL_KEYUP:
        bus.publish(KeyReleasedEvent{ event.key.keysym.sym });
        break;

    case SDL_MOUSEMOTION:
        bus.publish(MouseMovedEvent{
            event.motion.x, event.motion.y,
            event.motion.xrel, event.motion.yrel,
        });
        break;

    case SDL_MOUSEBUTTONDOWN:
        bus.publish(MouseButtonPressedEvent{
            event.button.button, event.button.x, event.button.y,
        });
        break;

    case SDL_MOUSEBUTTONUP:
        bus.publish(MouseButtonReleasedEvent{
            event.button.button, event.button.x, event.button.y,
        });
        break;

    case SDL_MOUSEWHEEL:
        bus.publish(MouseWheelEvent{ event.wheel.x, event.wheel.y });
        break;

    default: break;
    }
}

bool Input::isKeyDown(KeyCode key) {
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    return sc < 512 && s_keysCurrent && s_keysCurrent[sc];
}

bool Input::isMouseDown(MouseBtn btn) {
    return (s_mouseCurrent & SDL_BUTTON(btn)) != 0;
}

int32_t Input::getMouseX()      { return s_mouseX; }
int32_t Input::getMouseY()      { return s_mouseY; }
int32_t Input::getMouseDeltaX() { return s_mouseX - s_mousePrevX; }
int32_t Input::getMouseDeltaY() { return s_mouseY - s_mousePrevY; }

} // namespace engine
