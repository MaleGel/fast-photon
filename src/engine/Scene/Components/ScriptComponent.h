#pragma once
#include <sol/sol.hpp>

#include <string>

namespace engine {

// Attached to an entity to give it a Lua-driven behaviour.
//
// 'path' is repo-relative (e.g. "assets/data/factions/player/units/warrior.lua").
// ScriptSystem owns the actual environment table and looks it up by entity;
// this component just records which file the entity is wired to.
//
// Why we don't store the environment here:
//   - sol::environment holds a Lua reference; copying / moving it across
//     ECS storage would risk dangling refs after compaction.
//   - Hot-reload swaps the environment in-place inside ScriptSystem's
//     own map, leaving the component untouched.
struct ScriptComponent {
    std::string path;
};

} // namespace engine
