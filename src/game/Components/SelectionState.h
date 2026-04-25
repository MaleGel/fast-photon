#pragma once
#include <glm/vec2.hpp>
#include <optional>

namespace engine {

// Scene-level singleton held in registry.ctx(): tracks which tile the mouse
// is currently hovering over and which tile (if any) the player has clicked.
//
// Both are grid coordinates (col, row). `hovered` is updated every frame;
// `selected` changes only on the 'tile.select'/'tile.cancel' actions.
struct SelectionState {
    std::optional<glm::ivec2> hovered;
    std::optional<glm::ivec2> selected;
};

} // namespace engine
