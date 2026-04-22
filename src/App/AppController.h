#pragma once
#include "Core/Events/Subscription.h"

namespace engine {

class EventBus;

// Owns the top-level "should the game keep running?" flag and translates
// app-level events into a lifecycle decision.
//
// Subscriptions held here:
//   AppQuitRequestedEvent  → requestQuit()
//   KeyPressedEvent(ESC)   → requestQuit()
class AppController {
public:
    void init(EventBus& bus);
    void shutdown();

    bool shouldQuit() const { return m_quitRequested; }

    // Can be called programmatically too (e.g. from a menu).
    void requestQuit() { m_quitRequested = true; }

private:
    bool                       m_quitRequested = false;
    Subscription               m_quitSub;
    Subscription               m_escapeSub;
};

} // namespace engine
