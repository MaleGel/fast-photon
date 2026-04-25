#pragma once
#include "ResourceTypes.h"
#include "Texture.h"
#include <string>
#include <unordered_map>

namespace engine {

class VulkanContext;

// Owns all textures loaded during asset registration.
// Textures are stored by value — TextureCache is their sole owner.
class TextureCache {
public:
    void shutdown(const VulkanContext& ctx);

    bool load(const VulkanContext& ctx, TextureID id, const std::string& path);

    // Returns nullptr if not found.
    const Texture* get(TextureID id) const;

private:
    std::unordered_map<TextureID, Texture> m_textures;
};

} // namespace engine
