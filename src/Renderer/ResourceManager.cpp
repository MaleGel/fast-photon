#include "ResourceManager.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace engine {

// JSON arrives as std::string; convert to a StringAtom of the matching category.
static ShaderID   toShaderID  (const std::string& s) { return ShaderID  (s.c_str()); }
static TextureID  toTextureID (const std::string& s) { return TextureID (s.c_str()); }
static MaterialID toMaterialID(const std::string& s) { return MaterialID(s.c_str()); }
static SpriteID   toSpriteID  (const std::string& s) { return SpriteID  (s.c_str()); }

void ResourceManager::init(const VulkanContext& ctx) {
    (void)ctx;
    FP_CORE_INFO("ResourceManager initialized");
}

void ResourceManager::shutdown(const VulkanContext& ctx) {
    // Reverse order of dependency: sprites/materials reference textures/shaders,
    // so data registries are cleared first, then GPU caches are destroyed.
    m_sprites.clear();
    m_materials.clear();
    m_textures.shutdown(ctx);
    m_shaders.shutdown(ctx);
    FP_CORE_INFO("ResourceManager shutdown");
}

void ResourceManager::loadManifest(const VulkanContext& ctx, const std::string& manifestPath) {
    std::ifstream file(manifestPath);
    FP_CORE_ASSERT(file.is_open(), "Cannot open asset manifest: {}", manifestPath);

    nlohmann::json doc = nlohmann::json::parse(file);

    // ── Shaders ──────────────────────────────────────────────────────────────
    if (doc.contains("shaders")) {
        for (const auto& e : doc.at("shaders")) {
            m_shaders.load(ctx,
                toShaderID(e.at("name").get<std::string>()),
                e.at("path").get<std::string>());
        }
    }

    // ── Textures ─────────────────────────────────────────────────────────────
    if (doc.contains("textures")) {
        for (const auto& e : doc.at("textures")) {
            m_textures.load(ctx,
                toTextureID(e.at("name").get<std::string>()),
                e.at("path").get<std::string>());
        }
    }

    // ── Materials ────────────────────────────────────────────────────────────
    if (doc.contains("materials")) {
        for (const auto& e : doc.at("materials")) {
            Material m;
            m.vertShader = toShaderID (e.value("vert",    std::string{}));
            m.fragShader = toShaderID (e.value("frag",    std::string{}));
            m.texture    = toTextureID(e.value("texture", std::string{}));
            if (e.contains("color")) {
                auto c = e.at("color");
                m.baseColor = { c.at(0).get<float>(), c.at(1).get<float>(),
                                c.at(2).get<float>(), c.at(3).get<float>() };
            }
            m_materials.add(toMaterialID(e.at("name").get<std::string>()), std::move(m));
        }
    }

    // ── Sprites ──────────────────────────────────────────────────────────────
    if (doc.contains("sprites")) {
        for (const auto& e : doc.at("sprites")) {
            Sprite s;
            s.texture = toTextureID(e.at("texture").get<std::string>());
            s.x       = e.value("x", 0);
            s.y       = e.value("y", 0);
            s.width   = e.value("w", 0);
            s.height  = e.value("h", 0);
            m_sprites.add(toSpriteID(e.at("name").get<std::string>()), std::move(s));
        }
    }

    FP_CORE_INFO("Asset manifest loaded: {}", manifestPath);
}

} // namespace engine
