#pragma once
#include <cstdint>
#include <entt/entt.hpp>

namespace engine {

enum class TileType : uint8_t {
    Grass  = 0,
    Forest = 1,
    Mountain = 2,
    Water  = 3,
};

struct Tile {
    TileType     type         = TileType::Grass;
    bool         passable     = true;
    uint8_t      movementCost = 1;   // action points to enter
    entt::entity occupant     = entt::null;
};

} // namespace engine
