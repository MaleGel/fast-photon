#include "ScriptSystem.h"
#include "Scene/Components/ScriptComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/SpriteComponent.h"
#include "Audio/AudioDevice.h"
#include "Audio/Sound.h"
#include "Renderer/ResourceManager.h"
#include "Vfx/VfxSystem.h"
#include "Platform/FileWatcher.h"
#include "Core/Events/EventBus.h"
#include "Core/Profiler/Profiler.h"
#include "Core/Log/Log.h"

// Game-side header — engine-layer ScriptSystem reaches across the
// engine/game line for turn events. We accept this small leak rather
// than spinning up a separate game-side wrapper for one feature.
#include "Components/FactionComponent.h"
#include "Gameplay/TurnManager/TurnEvents.h"

#include <entt/entt.hpp>

#include <fstream>
#include <sstream>
#include <tuple>

namespace engine {

// ── Lua-facing handle types ──────────────────────────────────────────────────
//
// Each is a thin (registry*, entity) pair. The C++ side does the
// component lookup on every method call — adds an unordered_map probe but
// keeps Lua free from any concept of "stale references" if the entity is
// destroyed between calls (we just no-op gracefully).

namespace {

// Faction → string on the way out to Lua. Mirror of the JSON enum
// names used by JsonSceneLoader so authors see the same labels in C++,
// JSON and Lua.
const char* factionToLua(Faction f) {
    switch (f) {
        case Faction::Player:  return "Player";
        case Faction::Enemy:   return "Enemy";
        case Faction::Neutral: return "Neutral";
    }
    return "Neutral";
}

struct ScriptTransform {
    entt::registry* reg;
    entt::entity    e;

    // Returns (x, y, z). Lua callers usually ignore z.
    std::tuple<float, float, float> position() const {
        if (auto* tc = reg->try_get<TransformComponent>(e)) {
            return { tc->position.x, tc->position.y, tc->position.z };
        }
        return { 0.0f, 0.0f, 0.0f };
    }

    void set_position(float x, float y, sol::optional<float> z) {
        if (auto* tc = reg->try_get<TransformComponent>(e)) {
            tc->position.x = x;
            tc->position.y = y;
            if (z) tc->position.z = *z;
        }
    }

    void translate(float dx, float dy, sol::optional<float> dz) {
        if (auto* tc = reg->try_get<TransformComponent>(e)) {
            tc->position.x += dx;
            tc->position.y += dy;
            if (dz) tc->position.z += *dz;
        }
    }
};

struct ScriptSprite {
    entt::registry* reg;
    entt::entity    e;

    std::string id() const {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) {
            return std::string(sc->sprite.c_str());
        }
        return {};
    }

    void set_id(const std::string& sprite_id) {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) {
            sc->sprite = SpriteID(sprite_id.c_str());
        }
    }

    bool visible() const {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) return sc->visible;
        return false;
    }

    void set_visible(bool v) {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) sc->visible = v;
    }

    // Tint as four floats — RGBA, all 0..1.
    std::tuple<float, float, float, float> tint() const {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) {
            return { sc->tint.r, sc->tint.g, sc->tint.b, sc->tint.a };
        }
        return { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    void set_tint(float r, float g, float b, sol::optional<float> a) {
        if (auto* sc = reg->try_get<SpriteComponent>(e)) {
            sc->tint = { r, g, b, a.value_or(1.0f) };
        }
    }
};

struct ScriptEntity {
    entt::registry* reg;
    entt::entity    e;

    // Member properties — sol3 binds these as methods that return new
    // wrappers. The wrappers are tiny PODs so creating one per access is
    // cheap and hot-path use can hoist them with `local t = self.transform`.
    ScriptTransform transform() const { return { reg, e }; }
    ScriptSprite    sprite()    const { return { reg, e }; }

    // Returns the entity's faction as a string ("Player" / "Enemy" /
    // "Neutral"). Empty string when the entity has no FactionComponent.
    std::string faction() const {
        if (auto* fc = reg->try_get<FactionComponent>(e)) {
            return factionToLua(fc->faction);
        }
        return {};
    }

    bool valid() const {
        return reg != nullptr && reg->valid(e);
    }
};

} // namespace

// ── Init / shutdown ─────────────────────────────────────────────────────────

void ScriptSystem::init(entt::registry& reg, EventBus& bus, FileWatcher& watcher,
                        AudioDevice& audio, ResourceManager& resources,
                        VfxSystem& vfx) {
    m_watcher   = &watcher;
    m_bus       = &bus;
    m_audio     = &audio;
    m_resources = &resources;
    m_vfx       = &vfx;
    m_registry  = &reg;

    m_lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );

    registerBindings();

    // Subscribe to turn events. We dispatch to every script's turn hooks
    // (broadcast), with a per-entity warning when the script's entity is
    // missing FactionComponent — this catches authoring mistakes early.
    m_turnStartedSub = bus.subscribe<TurnStartedEvent>(
        [this](const TurnStartedEvent& e) {
            if (!m_registry) return;
            const char* fname = factionToLua(e.faction);
            for (auto& [entity, binding] : m_bindings) {
                if (!binding.on_turn_start) continue;
                if (!m_registry->valid(entity)) continue;
                if (!m_registry->all_of<FactionComponent>(entity)) {
                    FP_CORE_WARN("ScriptSystem: '{}' has on_turn_start but its "
                                 "entity has no FactionComponent — hook fired "
                                 "anyway, but self:faction() will return ''",
                                 binding.path);
                }
                ScriptEntity self{ m_registry, entity };
                auto result = (*binding.on_turn_start)(self, fname);
                if (!result.valid()) {
                    sol::error err = result;
                    FP_CORE_ERROR("ScriptSystem: on_turn_start error in '{}': {}",
                                  binding.path, err.what());
                }
            }
        });

    m_turnEndedSub = bus.subscribe<TurnEndedEvent>(
        [this](const TurnEndedEvent& e) {
            if (!m_registry) return;
            const char* ended = factionToLua(e.endedFaction);
            const char* next  = factionToLua(e.nextFaction);
            for (auto& [entity, binding] : m_bindings) {
                if (!binding.on_turn_end) continue;
                if (!m_registry->valid(entity)) continue;
                if (!m_registry->all_of<FactionComponent>(entity)) {
                    FP_CORE_WARN("ScriptSystem: '{}' has on_turn_end but its "
                                 "entity has no FactionComponent",
                                 binding.path);
                }
                ScriptEntity self{ m_registry, entity };
                auto result = (*binding.on_turn_end)(self, ended, next);
                if (!result.valid()) {
                    sol::error err = result;
                    FP_CORE_ERROR("ScriptSystem: on_turn_end error in '{}': {}",
                                  binding.path, err.what());
                }
            }
        });

    FP_CORE_INFO("ScriptSystem initialized");
}

void ScriptSystem::shutdown() {
    // Release subscriptions before bindings — the lambdas captured `this`
    // and reach into m_bindings; once those handlers stop firing it's
    // safe to drop the bindings + bus pointer.
    m_turnStartedSub.release();
    m_turnEndedSub.release();
    m_bindings.clear();
    m_watchedPaths.clear();
    m_watcher   = nullptr;
    m_bus       = nullptr;
    m_audio     = nullptr;
    m_resources = nullptr;
    m_vfx       = nullptr;
    m_registry  = nullptr;
    FP_CORE_TRACE("ScriptSystem destroyed");
}

// ── Bindings ────────────────────────────────────────────────────────────────

void ScriptSystem::registerBindings() {
    // Stringify any Lua value via Lua's own tostring(). Our previous
    // `v.as<std::string>()` only worked for actual strings — numbers,
    // booleans, tables would throw. Going through tostring() matches
    // print()'s standard semantics and works for every type.
    auto stringify_args = [this](sol::variadic_args args, char sep) {
        sol::function tostring = m_lua["tostring"];
        std::string line;
        for (auto v : args) {
            if (!line.empty()) line += sep;
            line += tostring(v).get<std::string>();
        }
        return line;
    };

    // print() goes through our logger so script output lands in the same
    // sink as engine logs (and we don't need io.* enabled).
    m_lua.set_function("print", [stringify_args](sol::variadic_args args) {
        FP_CORE_INFO("[lua] {}", stringify_args(args, '\t'));
    });

    // Component view types. New_usertype registers the class plus its
    // methods; Lua-side calls dispatch through these vtable-equivalents.
    m_lua.new_usertype<ScriptTransform>("ScriptTransform",
        sol::no_constructor,
        "position",      &ScriptTransform::position,
        "set_position",  &ScriptTransform::set_position,
        "translate",     &ScriptTransform::translate
    );

    m_lua.new_usertype<ScriptSprite>("ScriptSprite",
        sol::no_constructor,
        "id",           &ScriptSprite::id,
        "set_id",       &ScriptSprite::set_id,
        "visible",      &ScriptSprite::visible,
        "set_visible",  &ScriptSprite::set_visible,
        "tint",         &ScriptSprite::tint,
        "set_tint",     &ScriptSprite::set_tint
    );

    // The 'self' object scripts receive. Property-style: 'self.transform'
    // returns a fresh ScriptTransform handle (cheap POD).
    m_lua.new_usertype<ScriptEntity>("ScriptEntity",
        sol::no_constructor,
        "transform", sol::property(&ScriptEntity::transform),
        "sprite",    sol::property(&ScriptEntity::sprite),
        "faction",   &ScriptEntity::faction,
        "valid",     &ScriptEntity::valid
    );

    // log.* — explicit severity, space-separated stringification of every
    // argument. Uses the same tostring-based helper as print() so numbers
    // and booleans format the way scripts expect.
    sol::table log = m_lua.create_named_table("log");
    log.set_function("info", [stringify_args](sol::variadic_args args) {
        FP_CORE_INFO("[lua] {}", stringify_args(args, ' '));
    });
    log.set_function("warn", [stringify_args](sol::variadic_args args) {
        FP_CORE_WARN("[lua] {}", stringify_args(args, ' '));
    });
    log.set_function("error", [stringify_args](sol::variadic_args args) {
        FP_CORE_ERROR("[lua] {}", stringify_args(args, ' '));
    });

    // audio.play(sound_id_string) — looks up by qualified id and plays
    // via the engine's AudioDevice. Returns true when the lookup hit.
    sol::table audio = m_lua.create_named_table("audio");
    audio.set_function("play", [this](const std::string& sound_id) {
        if (!m_audio || !m_resources) return false;
        Sound* s = m_resources->getSound(SoundID(sound_id.c_str()));
        if (!s) {
            FP_CORE_WARN("[lua] audio.play: unknown sound '{}'", sound_id);
            return false;
        }
        m_audio->play(*s);
        return true;
    });

    // event.emit(name, payload?) — broadcast to every loaded script's
    // on_event(self, name, payload) hook. The payload is opaque to C++:
    // we hand it through unchanged, so any Lua value works (including
    // tables containing ScriptEntity references). Receivers filter by
    // name themselves.
    sol::table event = m_lua.create_named_table("event");
    event.set_function("emit", [this](const std::string& name,
                                      sol::object payload) {
        if (!m_registry) return;
        for (auto& [entity, binding] : m_bindings) {
            if (!binding.on_event) continue;
            if (!m_registry->valid(entity)) continue;
            ScriptEntity self{ m_registry, entity };
            auto result = (*binding.on_event)(self, name, payload);
            if (!result.valid()) {
                sol::error err = result;
                FP_CORE_ERROR("ScriptSystem: on_event error in '{}': {}",
                              binding.path, err.what());
            }
        }
    });

    // vfx.spawn(system_id, x, y[, count]) — fire a one-shot burst at a
    // world-space point. 'count' defaults to 1 to keep the call site
    // short; bigger bursts pass it explicitly. Returns true when the
    // system id resolves; false (and a warning) when it doesn't, so
    // gameplay typos surface instead of silently breaking effects.
    sol::table vfx = m_lua.create_named_table("vfx");
    vfx.set_function("spawn", [this](const std::string& system_id,
                                     float x, float y,
                                     sol::optional<int> count) {
        if (!m_vfx || !m_resources) return false;
        const ParticleSystemID id(system_id.c_str());
        if (m_resources->getParticleSystem(id) == nullptr) {
            FP_CORE_WARN("[lua] vfx.spawn: unknown particle system '{}'",
                         system_id);
            return false;
        }
        const int n = count.value_or(1);
        if (n <= 0) return true;
        m_vfx->spawnOnce(id, glm::vec2(x, y), static_cast<uint32_t>(n));
        return true;
    });
}

// ── Per-frame ───────────────────────────────────────────────────────────────

void ScriptSystem::update(entt::registry& reg, float dt) {
    FP_PROFILE_SCOPE("ScriptSystem::update");

    // Phase 1 — discover newly-attached scripts and load them. Loading
    // also fires on_init in the same call, so a script that attaches
    // mid-frame gets initialised on this same tick (before its first
    // on_update).
    auto view = reg.view<const ScriptComponent>();
    for (auto e : view) {
        const auto& sc = view.get<const ScriptComponent>(e);
        if (sc.path.empty()) continue;

        auto it = m_bindings.find(e);
        if (it != m_bindings.end() && it->second.path == sc.path) continue;

        if (loadScriptForEntity(reg, e, sc.path)) {
            // Hot-reload watcher: register the path the first time we see
            // it. The callback is keyed on path, so multiple entities
            // sharing a script file share one watch.
            if (m_watcher && !m_watchedPaths.count(sc.path)) {
                m_watchedPaths[sc.path] = true;
                const std::string watchedPath = sc.path;
                m_watcher->watch(watchedPath,
                    [this, &reg, watchedPath](const std::string&) {
                        reloadScript(reg, watchedPath);
                    });
            }
        }
    }

    // Phase 2 — fire on_update(self, dt) for every entity that has both
    // a binding and a cached on_update handler.
    for (auto& [entity, binding] : m_bindings) {
        if (!binding.on_update) continue;
        if (!reg.valid(entity)) continue;

        ScriptEntity self{ &reg, entity };
        auto result = (*binding.on_update)(self, dt);
        if (!result.valid()) {
            sol::error err = result;
            FP_CORE_ERROR("ScriptSystem: on_update error in '{}': {}",
                          binding.path, err.what());
            // Drop the broken hook so we don't spam the log every frame
            // until the user fixes the script. Hot-reload re-installs it.
            binding.on_update.reset();
        }
    }
}

void ScriptSystem::reloadScript(entt::registry& reg, const std::string& path) {
    FP_CORE_INFO("ScriptSystem: reloading '{}'", path);
    auto view = reg.view<const ScriptComponent>();
    for (auto e : view) {
        const auto& sc = view.get<const ScriptComponent>(e);
        if (sc.path != path) continue;
        loadScriptForEntity(reg, e, path);
    }
}

// ── Load + cache hooks ─────────────────────────────────────────────────────

bool ScriptSystem::loadScriptForEntity(entt::registry& reg, entt::entity entity,
                                       const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        FP_CORE_ERROR("ScriptSystem: cannot open '{}'", path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    auto env = std::make_unique<sol::environment>(
        m_lua, sol::create, m_lua.globals());

    auto loaded = m_lua.load(buffer.str(), path);
    if (!loaded.valid()) {
        sol::error err = loaded;
        FP_CORE_ERROR("ScriptSystem: parse error in '{}': {}", path, err.what());
        return false;
    }
    sol::protected_function fn = loaded;
    sol::set_environment(*env, fn);

    auto result = fn();
    if (!result.valid()) {
        sol::error err = result;
        FP_CORE_ERROR("ScriptSystem: runtime error in '{}': {}", path, err.what());
        return false;
    }

    // Build the binding *now*: cache the on_update reference so the
    // per-frame loop doesn't have to look up "on_update" by name every
    // tick — that lookup is the dominant cost for tiny scripts.
    Binding b;
    b.path = path;
    b.env  = std::move(env);

    // Cache every hook function we know about. Missing hooks stay null
    // and are skipped at dispatch time. Keep this list in sync with the
    // header comment listing the supported hooks.
    auto cache_hook = [&](const char* name) -> std::unique_ptr<sol::protected_function> {
        sol::object obj = (*b.env)[name];
        if (obj.valid() && obj.get_type() == sol::type::function) {
            return std::make_unique<sol::protected_function>(obj);
        }
        return nullptr;
    };
    b.on_update     = cache_hook("on_update");
    b.on_turn_start = cache_hook("on_turn_start");
    b.on_turn_end   = cache_hook("on_turn_end");
    b.on_event      = cache_hook("on_event");

    // on_init runs immediately, only on this load (or a re-load). It
    // doesn't get cached — it's one-shot by definition.
    sol::object init_obj = (*b.env)["on_init"];
    if (init_obj.valid() && init_obj.get_type() == sol::type::function) {
        sol::protected_function init_fn = init_obj;
        ScriptEntity self{ &reg, entity };
        auto init_result = init_fn(self);
        if (!init_result.valid()) {
            sol::error err = init_result;
            FP_CORE_ERROR("ScriptSystem: on_init error in '{}': {}", path, err.what());
        }
    }

    m_bindings[entity] = std::move(b);
    return true;
}

} // namespace engine
