#pragma once
#include "ResourceTypes.h"
#include "ShaderCache.h"
#include "TextureCache.h"
#include "MaterialRegistry.h"
#include "SpriteRegistry.h"
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace engine {

class VulkanContext;

// Central coordinator: owns per-type caches/registries and loads them
// from a single JSON manifest. Does not do any loading itself — delegates
// to the appropriate sub-component.
class ResourceManager {
public:
    void init(const VulkanContext& ctx);
    void shutdown(const VulkanContext& ctx);

    // Load a unified asset manifest (shaders, textures, materials, sprites).
    void loadManifest(const VulkanContext& ctx, const std::string& manifestPath);

    // ── Accessors (pass-through to sub-components) ───────────────────────────
    VkShaderModule  getShader  (ShaderID   id) const { return m_shaders.get(id);   }
    const Texture*  getTexture (TextureID  id) const { return m_textures.get(id);  }
    const Material* getMaterial(MaterialID id) const { return m_materials.get(id); }
    const Sprite*   getSprite  (SpriteID   id) const { return m_sprites.get(id);   }

    // ── Direct access (if a system needs to enumerate) ───────────────────────
    const ShaderCache&      shaders()   const { return m_shaders;   }
    const TextureCache&     textures()  const { return m_textures;  }
    const MaterialRegistry& materials() const { return m_materials; }
    const SpriteRegistry&   sprites()   const { return m_sprites;   }

private:
    void loadBakedAssets(const VulkanContext& ctx);
    void loadRawAssets  (const VulkanContext& ctx, const nlohmann::json& doc);

    ShaderCache      m_shaders;
    TextureCache     m_textures;
    MaterialRegistry m_materials;
    SpriteRegistry   m_sprites;
};

} // namespace engine
