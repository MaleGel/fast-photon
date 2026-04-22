#include "InputMap.h"
#include "InputEvents.h"
#include "Core/Events/EventBus.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace engine {

// ── Public ───────────────────────────────────────────────────────────────────

void InputMap::init(EventBus& bus) {
    m_bus = &bus;

    m_keyDownSub = bus.subscribe<KeyPressedEvent>(
        [this](const KeyPressedEvent& e)  { onKeyPressed(e.key);  });

    m_keyUpSub   = bus.subscribe<KeyReleasedEvent>(
        [this](const KeyReleasedEvent& e) { onKeyReleased(e.key); });

    m_wheelSub   = bus.subscribe<MouseWheelEvent>(
        [this](const MouseWheelEvent& e)  { onMouseWheel(e.dx, e.dy); });

    FP_CORE_INFO("InputMap initialized");
}

void InputMap::shutdown() {
    m_keyDownSub.release();
    m_keyUpSub.release();
    m_wheelSub.release();
    clearBindings();
    m_bus = nullptr;
}

void InputMap::bindAction(ActionID action, Binding binding) {
    m_actionBindings[action].push_back(binding);

    switch (binding.type) {
        case Binding::Type::Key:
            m_keyToActions[binding.key].insert(action);
            break;
        case Binding::Type::MouseWheelUp:
            m_wheelUpActions.insert(action);
            break;
        case Binding::Type::MouseWheelDown:
            m_wheelDownActions.insert(action);
            break;
    }
}

bool InputMap::isActionDown(ActionID action) const {
    return m_actionsDown.count(action) > 0;
}

void InputMap::clearBindings() {
    m_actionBindings.clear();
    m_keyToActions.clear();
    m_wheelUpActions.clear();
    m_wheelDownActions.clear();
    m_actionsDown.clear();
}

// ── JSON loader ──────────────────────────────────────────────────────────────

static Binding parseBinding(const std::string& token) {
    if (token == "MouseWheelUp")   return Binding::wheelUp();
    if (token == "MouseWheelDown") return Binding::wheelDown();

    SDL_Keycode key = SDL_GetKeyFromName(token.c_str());
    FP_CORE_ASSERT(key != SDLK_UNKNOWN,
                   "InputMap: unknown key name '{}' in bindings", token);
    return Binding::keyboard(key);
}

void InputMap::loadBindings(const std::string& path) {
    std::ifstream file(path);
    FP_CORE_ASSERT(file.is_open(), "Cannot open input bindings: {}", path);

    nlohmann::json doc = nlohmann::json::parse(file);
    const auto& actions = doc.at("actions");

    for (auto it = actions.begin(); it != actions.end(); ++it) {
        ActionID action(it.key().c_str());
        for (const auto& token : it.value()) {
            bindAction(action, parseBinding(token.get<std::string>()));
        }
    }

    FP_CORE_INFO("InputMap: loaded {} action(s) from {}",
                 m_actionBindings.size(), path);
}

// ── Event handlers ───────────────────────────────────────────────────────────

void InputMap::onKeyPressed(SDL_Keycode key) {
    auto it = m_keyToActions.find(key);
    if (it == m_keyToActions.end()) return;

    for (ActionID action : it->second) {
        m_actionsDown.insert(action);
        m_bus->publish(ActionTriggeredEvent{ action });
    }
}

void InputMap::onKeyReleased(SDL_Keycode key) {
    auto it = m_keyToActions.find(key);
    if (it == m_keyToActions.end()) return;

    for (ActionID action : it->second) {
        // An action may be bound to multiple keys — stays "down" while any
        // of them is still held. Re-check via the current keyboard state.
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        bool anyStillDown = false;
        for (const auto& binding : m_actionBindings[action]) {
            if (binding.type != Binding::Type::Key) continue;
            SDL_Scancode sc = SDL_GetScancodeFromKey(binding.key);
            if (sc < 512 && keys[sc]) { anyStillDown = true; break; }
        }
        if (!anyStillDown) {
            m_actionsDown.erase(action);
            m_bus->publish(ActionReleasedEvent{ action });
        }
    }
}

void InputMap::onMouseWheel(int /*dx*/, int dy) {
    // Each positive/negative tick fires its actions once per unit of scroll.
    // SDL can report multi-unit deltas — loop to respect that.
    if (dy > 0) {
        for (int i = 0; i < dy; ++i)
            for (ActionID action : m_wheelUpActions)
                m_bus->publish(ActionTriggeredEvent{ action });
    } else if (dy < 0) {
        for (int i = 0; i < -dy; ++i)
            for (ActionID action : m_wheelDownActions)
                m_bus->publish(ActionTriggeredEvent{ action });
    }
}

} // namespace engine
