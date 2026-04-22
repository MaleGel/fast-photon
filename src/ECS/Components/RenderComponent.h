#pragma once
#include <glm/glm.hpp>

namespace engine {

struct RenderComponent {
    glm::vec4 color   = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA
    bool      visible = true;

    RenderComponent() = default;
    explicit RenderComponent(glm::vec4 color, bool visible = true)
        : color(color), visible(visible) {}
};

} // namespace engine
