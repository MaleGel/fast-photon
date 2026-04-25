#pragma once
#include "ResourceTypes.h"
#include "ShaderCache.h"
#include "TextureCache.h"
#include "MaterialRegistry.h"
#include "SpriteRegistry.h"
#include "FontRegistry.h"
#include "Audio/SoundCache.h"
#include "Animation/AnimationRegistry.h"
#include "Vfx/ParticleSystemRegistry.h"
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace engine {

class VulkanContext;
class AudioDevice;

// Central coordinator: owns per-type caches/registries and loads them
// from a single JSON manifest. Does not do any loading itself — delegates
// to the appropriate sub-component.
class ResourceManager {
public:
    void init(const VulkanContext& ctx, AudioDevice& audio, class EventBus& bus);
    void shutdown(const VulkanContext& ctx);

    // Load a unified asset manifest (shaders/textures/materials/sprites/fonts/sounds).
    void loadManifest(const VulkanContext& ctx, const std::string& manifestPath);

    // ── Accessors (pass-through to sub-components) ───────────────────────────
    VkShaderModule  getShader  (ShaderID   id) const { return m_shaders.get(id);   }
    const Texture*  getTexture (TextureID  id) const { return m_textures.get(id);  }
    const Material* getMaterial(MaterialID id) const { return m_materials.get(id); }
    const Sprite*   getSprite  (SpriteID   id) const { return m_sprites.get(id);   }
    const Font*     getFont    (FontID     id) const { return m_fonts.get(id);     }
    Sound*          getSound   (SoundID    id)       { return m_sounds.get(id);    }
    const Animation*      getAnimation     (AnimationID      id) const { return m_animations.getAnimation(id); }
    const AnimationSet*   getAnimSet       (AnimationSetID   id) const { return m_animations.getSet(id);       }
    const ParticleSystem* getParticleSystem(ParticleSystemID id) const {
        return m_particleSystems.get(id);
    }

    // ── Direct access (if a system needs to enumerate) ───────────────────────
    const ShaderCache&            shaders()         const { return m_shaders;         }
    const TextureCache&           textures()        const { return m_textures;        }
    const MaterialRegistry&       materials()       const { return m_materials;       }
    const SpriteRegistry&         sprites()         const { return m_sprites;         }
    const FontRegistry&           fonts()           const { return m_fonts;           }
    const SoundCache&             sounds()          const { return m_sounds;          }
    const AnimationRegistry&      animations()      const { return m_animations;      }
    const ParticleSystemRegistry& particleSystems() const { return m_particleSystems; }

private:
    void loadBakedAssets    (const VulkanContext& ctx);
    void loadRawAssets      (const VulkanContext& ctx, const nlohmann::json& doc);
    void loadFonts          (const VulkanContext& ctx, const nlohmann::json& doc);
    void loadSounds         (const nlohmann::json& doc);
    void loadAnimations     (const nlohmann::json& doc);
    void loadParticleSystems(const nlohmann::json& doc);

    ShaderCache            m_shaders;
    TextureCache           m_textures;
    MaterialRegistry       m_materials;
    SpriteRegistry         m_sprites;
    FontRegistry           m_fonts;
    SoundCache             m_sounds;
    AnimationRegistry      m_animations;
    ParticleSystemRegistry m_particleSystems;

    AudioDevice*     m_audio = nullptr;   // not owned
};

} // namespace engine
