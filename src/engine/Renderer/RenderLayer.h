#pragma once
#include <cstdint>

namespace engine {

// Coarse-grained render ordering. Values are spaced so new layers can be
// slotted in-between without renumbering. Smaller = drawn earlier (further
// back visually).
enum class RenderLayer : uint8_t {
    Background   = 0,
    Terrain      = 10,   // grid tiles
    WorldObjects = 20,   // unit sprites, world-space decor
    Overlays     = 30,   // tile highlights, selection, range indicators
    Effects      = 40,   // particles, flashes
    Hud          = 100,  // screen-space text and UI
    Debug        = 200,  // ImGui, developer gizmos
};

} // namespace engine
