#include "AppController.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Platform/Input/InputEvents.h"
#include "Platform/Window/WindowEvents.h"

#include <SDL2/SDL_keycode.h>

namespace engine {

void AppController::init(EventBus& bus) {
    m_quitRequested = false;

    m_quitSub = bus.subscribe<AppQuitRequestedEvent>(
        [this](const AppQuitRequestedEvent&) {
            FP_CORE_INFO("AppController: quit requested (window close)");
            requestQuit();
        });

    m_escapeSub = bus.subscribe<KeyPressedEvent>(
        [this](const KeyPressedEvent& e) {
            if (e.key == SDLK_ESCAPE) {
                FP_CORE_INFO("AppController: quit requested (Escape)");
                requestQuit();
            }
        });
}

void AppController::shutdown() {
    m_quitSub.release();
    m_escapeSub.release();
}

} // namespace engine
