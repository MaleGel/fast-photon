#include "ResourceManager.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace engine {

namespace fs = std::filesystem;

static constexpr const char* kAtlasJsonPath = "assets/atlases/atlas.json";
static constexpr const char* kAtlasesDir   = "assets/atlases";
static constexpr const char* kTexturesDir  = "assets/textures";

// JSON arrives as std::string; convert to a StringAtom of the matching category.
static ShaderID   toShaderID  (const std::string& s) { return ShaderID  (s.c_str()); }
static TextureID  toTextureID (const std::string& s) { return TextureID (s.c_str()); }
static MaterialID toMaterialID(const std::string& s) { return MaterialID(s.c_str()); }
static SpriteID   toSpriteID  (const std::string& s) { return SpriteID  (s.c_str()); }

// ── Public ───────────────────────────────────────────────────────────────────

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
    // ── 1. Parse the hand-written manifest ──────────────────────────────────
    std::ifstream file(manifestPath);
    FP_CORE_ASSERT(file.is_open(), "Cannot open asset manifest: {}", manifestPath);
    nlohmann::json doc = nlohmann::json::parse(file);

    // ── 2. Shaders ──────────────────────────────────────────────────────────
    if (doc.contains("shaders")) {
        for (const auto& e : doc.at("shaders")) {
            m_shaders.load(ctx,
                toShaderID(e.at("name").get<std::string>()),
                e.at("path").get<std::string>());
        }
    }

    // ── 3. Textures + sprites — baked atlas vs hand-written manifest ────────
    if (fs::exists(kAtlasJsonPath)) {
        loadBakedAssets(ctx);
    } else {
        loadRawAssets(ctx, doc);
    }

    // ── 4. Materials ────────────────────────────────────────────────────────
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

    FP_CORE_INFO("Asset manifest loaded: {}", manifestPath);
}

// ── Private ──────────────────────────────────────────────────────────────────

void ResourceManager::loadBakedAssets(const VulkanContext& ctx) {
    FP_CORE_INFO("Asset mode: baked (atlas: {})", kAtlasJsonPath);

    std::ifstream file(kAtlasJsonPath);
    FP_CORE_ASSERT(file.is_open(), "Cannot open atlas manifest: {}", kAtlasJsonPath);
    nlohmann::json atlas = nlohmann::json::parse(file);

    // One texture per atlas page, named "atlas_<index>".
    for (const auto& page : atlas.at("pages")) {
        int         index = page.at("index").get<int>();
        std::string image = page.at("image").get<std::string>();
        std::string path  = std::string(kAtlasesDir) + "/" + image;

        // Baked mode contract: any page listed in atlas.json must exist on disk.
        FP_CORE_ASSERT(fs::exists(path),
            "Atlas page '{}' referenced by atlas.json is missing on disk. "
            "Re-run tools/atlas_baker/bake.sh.", path);

        TextureID id(("atlas_" + std::to_string(index)).c_str());
        m_textures.load(ctx, id, path);
    }

    // One sprite per packed rect, referencing its page's texture.
    for (const auto& s : atlas.at("sprites")) {
        Sprite sprite;
        sprite.texture = TextureID(("atlas_" + std::to_string(s.at("page").get<int>())).c_str());
        sprite.x       = s.at("x").get<int>();
        sprite.y       = s.at("y").get<int>();
        sprite.width   = s.at("w").get<int>();
        sprite.height  = s.at("h").get<int>();
        m_sprites.add(SpriteID(s.at("name").get<std::string>().c_str()), std::move(sprite));
    }
}

void ResourceManager::loadRawAssets(const VulkanContext& ctx, const nlohmann::json& doc) {
    FP_CORE_INFO("Asset mode: raw (manifest-driven)");

    // Set of sprite IDs declared in assets.json (used below for two-way validation).
    std::unordered_set<std::string> declared_sprite_ids;

    // ── Textures from manifest ──────────────────────────────────────────────
    if (doc.contains("textures")) {
        for (const auto& e : doc.at("textures")) {
            std::string name = e.at("name").get<std::string>();
            std::string path = e.at("path").get<std::string>();
            FP_CORE_ASSERT(fs::exists(path),
                "Texture '{}' declared in assets.json references missing file '{}'",
                name, path);
            m_textures.load(ctx, toTextureID(name), path);
        }
    }

    // ── Sprites from manifest ───────────────────────────────────────────────
    if (doc.contains("sprites")) {
        for (const auto& e : doc.at("sprites")) {
            Sprite s;
            s.texture = toTextureID(e.at("texture").get<std::string>());
            s.x       = e.value("x", 0);
            s.y       = e.value("y", 0);
            s.width   = e.value("w", 0);
            s.height  = e.value("h", 0);
            std::string sid = e.at("name").get<std::string>();
            declared_sprite_ids.insert(sid);
            m_sprites.add(toSpriteID(sid), std::move(s));
        }
    }

    // ── Two-way validation: every PNG on disk must be declared in manifest ──
    if (fs::is_directory(kTexturesDir)) {
        const fs::path root = kTexturesDir;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".png") continue;

            fs::path rel = fs::relative(entry.path(), root);
            rel.replace_extension();
            std::string expected_id = rel.generic_string();

            FP_CORE_ASSERT(declared_sprite_ids.count(expected_id) > 0,
                "PNG '{}' exists on disk but no sprite '{}' is declared in assets.json. "
                "Run tools/asset_indexer/index.sh to regenerate.",
                entry.path().string(), expected_id);
        }
    }
}

} // namespace engine
