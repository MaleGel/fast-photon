#pragma once
#include "ResourceTypes.h"
#include <cstdint>

namespace engine {

// A rectangular region of a texture atlas.
// Coordinates are in pixels; converted to UV by the renderer at draw time
// (so sprite metadata stays independent of texture size changes).
struct Sprite {
    TextureID texture;
    int32_t   x      = 0;
    int32_t   y      = 0;
    int32_t   width  = 0;
    int32_t   height = 0;
};

} // namespace engine
