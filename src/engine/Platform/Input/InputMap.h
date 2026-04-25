#pragma once
#include "Core/Events/Subscription.h"
#include "Core/StringAtom/StringAtom.h"
#include <SDL2/SDL_keycode.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

class EventBus;

// Abstract identifier for a gameplay-level action ("camera.pan_left").
using ActionID = StringAtom;

// One physical input that can trigger an action.
struct Binding {
    enum class Type : uint8_t {
        Key,
        MouseButton,
        MouseWheelUp,
        MouseWheelDown,
    };
    Type        type   = Type::Key;
    SDL_Keycode key    = 0;    // only for Type::Key
    uint8_t     button = 0;    // SDL_BUTTON_LEFT/RIGHT/MIDDLE (1/2/3); for Type::MouseButton

    static Binding keyboard   (SDL_Keycode k) { return { Type::Key,            k, 0 }; }
    static Binding mouseButton(uint8_t b)     { return { Type::MouseButton,    0, b }; }
    static Binding wheelUp()                  { return { Type::MouseWheelUp,   0, 0 }; }
    static Binding wheelDown()                { return { Type::MouseWheelDown, 0, 0 }; }
};

// Published when an action fires (key down transition, wheel tick,
// mouse button down).
struct ActionTriggeredEvent { ActionID action; };

// Published when a digital action transitions back to inactive
// (key up or mouse button up).
struct ActionReleasedEvent  { ActionID action; };

// Maps physical input (keys, mouse buttons, mouse wheel) to abstract actions.
//
// Two APIs:
//   * Polling — isActionDown(id) for continuous state (WASD held, etc).
//   * Events  — ActionTriggeredEvent / ActionReleasedEvent on transitions.
class InputMap {
public:
    void init(EventBus& bus);
    void shutdown();

    // Register a binding. Multiple bindings per action are allowed
    // (e.g. both 'A' and LeftArrow pan camera left).
    void bindAction(ActionID action, Binding binding);

    // Load a JSON file with the structure:
    //   { "actions": { "camera.pan_left": ["A", "Left"], ... } }
    void loadBindings(const std::string& path);

    // True while any binding associated with 'action' is currently held.
    bool isActionDown(ActionID action) const;

    // Drop every binding — useful when reloading the config.
    void clearBindings();

private:
    void onKeyPressed   (SDL_Keycode key);
    void onKeyReleased  (SDL_Keycode key);
    void onMouseDown    (uint8_t button);
    void onMouseUp      (uint8_t button);
    void onMouseWheel   (int dx, int dy);

    // All bindings for every action, flat. Index into m_actionBindings by ActionID.
    std::unordered_map<ActionID, std::vector<Binding>> m_actionBindings;

    // Reverse lookups: given a physical source, which actions it could trigger.
    std::unordered_map<SDL_Keycode, std::unordered_set<ActionID>> m_keyToActions;
    std::unordered_map<uint8_t,     std::unordered_set<ActionID>> m_mouseButtonToActions;

    // Wheel has no per-direction keycode — collect actions directly.
    std::unordered_set<ActionID> m_wheelUpActions;
    std::unordered_set<ActionID> m_wheelDownActions;

    // For polling: actions currently held (any of their held-bindings down).
    std::unordered_set<ActionID> m_actionsDown;

    EventBus*    m_bus = nullptr;
    Subscription m_keyDownSub;
    Subscription m_keyUpSub;
    Subscription m_mouseDownSub;
    Subscription m_mouseUpSub;
    Subscription m_wheelSub;
};

} // namespace engine
