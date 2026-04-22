#include "AppController.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Platform/Input/InputMap.h"
#include "Platform/Window/WindowEvents.h"

namespace engine {

static const ActionID kAppQuit("app.quit");

void AppController::init(EventBus& bus) {
    m_quitRequested = false;

    m_quitSub = bus.subscribe<AppQuitRequestedEvent>(
        [this](const AppQuitRequestedEvent&) {
            FP_CORE_INFO("AppController: quit requested (window close)");
            requestQuit();
        });

    m_escapeSub = bus.subscribe<ActionTriggeredEvent>(
        [this](const ActionTriggeredEvent& e) {
            if (e.action == kAppQuit) {
                FP_CORE_INFO("AppController: quit requested (action '{}')",
                             e.action.c_str());
                requestQuit();
            }
        });
}

void AppController::shutdown() {
    m_quitSub.release();
    m_escapeSub.release();
}

} // namespace engine
