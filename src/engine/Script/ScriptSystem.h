#pragma once
#include "Core/Events/Subscription.h"

#include <entt/fwd.hpp>
#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace engine {

class EventBus;
class FileWatcher;
class AudioDevice;
class ResourceManager;
class VfxSystem;

// Owns the single sol::state used by every gameplay script.
//
// Each entity with a ScriptComponent gets its own sol::environment — a
// Lua table that acts as the script's global namespace. Loading a script
// (re-)runs its file with that environment as `_ENV`, so global functions
// and variables defined in the file land inside the env table without
// leaking into other scripts' namespaces.
//
// Hooks the system calls when present in an environment:
//   on_init(self)                       — once, the first time the script attaches.
//   on_update(self, dt)                 — every frame.
//   on_turn_start(self, faction_name)   — when any faction's turn starts.
//   on_turn_end  (self, ended, next)    — when any faction's turn ends.
//   on_event(self, name, payload)       — broadcast for `event.emit` from Lua.
//
// References to the hooks are cached at load time, so per-frame and
// per-event dispatch doesn't pay for table lookup.
//
// Note: turn hooks fire on every script regardless of whose turn it is.
// Scripts filter by comparing the passed faction name to their own
// `self:faction()` if they only care about their own side.
class ScriptSystem {
public:
    void init(entt::registry& reg, EventBus& bus, FileWatcher& watcher,
              AudioDevice& audio, ResourceManager& resources, VfxSystem& vfx);
    void shutdown();

    // Per-frame: runs on_init for newly-attached scripts, then on_update
    // for everything with a script bound.
    void update(entt::registry& reg, float dt);

    // Force a re-run of one script file. Hot-reload path uses this; tests
    // and tooling can call it directly.
    void reloadScript(entt::registry& reg, const std::string& path);

    // Read-only access for diagnostics / future binding code.
    sol::state& state() { return m_lua; }

private:
    // Per-entity Lua state. The environment owns the script's globals;
    // the cached hooks are sol references resolved once at load — looking
    // them up by name every frame would be the difference between a
    // script tick and a few hundred nanoseconds extra per entity.
    struct Binding {
        std::unique_ptr<sol::environment>        env;
        // All hook references are optional — if the script doesn't define
        // the corresponding global function, the unique_ptr stays null.
        std::unique_ptr<sol::protected_function> on_update;
        std::unique_ptr<sol::protected_function> on_turn_start;
        std::unique_ptr<sol::protected_function> on_turn_end;
        std::unique_ptr<sol::protected_function> on_event;
        std::string                              path;
    };

    bool loadScriptForEntity(entt::registry& reg, entt::entity entity,
                             const std::string& path);

    // Set up the global tables (log.*, audio.*) and usertypes
    // (ScriptEntity, transform, sprite) on m_lua. Called once from init.
    void registerBindings();

    sol::state m_lua;
    std::unordered_map<entt::entity, Binding> m_bindings;
    std::unordered_map<std::string, bool>     m_watchedPaths;

    // Subscriptions kept alive by the system so they release on shutdown.
    Subscription                              m_turnStartedSub;
    Subscription                              m_turnEndedSub;

    FileWatcher*     m_watcher   = nullptr;
    EventBus*        m_bus       = nullptr;
    AudioDevice*     m_audio     = nullptr;
    ResourceManager* m_resources = nullptr;
    VfxSystem*       m_vfx       = nullptr;
    entt::registry*  m_registry  = nullptr;
};

} // namespace engine
