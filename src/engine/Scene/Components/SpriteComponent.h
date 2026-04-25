#pragma once
#include "Renderer/ResourceTypes.h"
#include "Renderer/RenderLayer.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

// Attach to an entity to have SpriteRenderer draw a textured quad at the
// entity's TransformComponent position.
//
// Render ordering is (layer, orderInLayer, TransformComponent.position.z) —
// see RenderQueue. Higher z = closer to camera (drawn on top).
struct SpriteComponent {
    SpriteID    sprite;                                 // ResourceManager sprite id
    glm::vec4   tint   { 1.f, 1.f, 1.f, 1.f };         // multiplied against sampled color
    glm::vec2   size   { 1.f, 1.f };                   // world units; 1x1 = one cell
    RenderLayer layer        = RenderLayer::WorldObjects;
    int16_t     orderInLayer = 0;
    bool        visible      = true;
};

} // namespace engine
