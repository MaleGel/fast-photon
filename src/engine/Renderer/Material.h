#pragma once
#include "ResourceTypes.h"
#include <glm/glm.hpp>

namespace engine {

// Describes how to render something: which shaders, which texture, base color.
// Pure data — does not own any Vulkan resources. The referenced shaders and
// texture are owned by ShaderCache / TextureCache respectively.
struct Material {
    ShaderID   vertShader;
    ShaderID   fragShader;
    TextureID  texture;                               // invalid = no texture
    glm::vec4  baseColor{ 1.f, 1.f, 1.f, 1.f };
};

} // namespace engine
