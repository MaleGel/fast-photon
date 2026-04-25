#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine {

struct TransformComponent {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f }; // identity
    glm::vec3 scale    = { 1.0f, 1.0f, 1.0f };

    TransformComponent() = default;
    explicit TransformComponent(glm::vec3 position,
                                glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f },
                                glm::vec3 scale    = { 1.0f, 1.0f, 1.0f })
        : position(position), rotation(rotation), scale(scale) {}
};

} // namespace engine
