#include "ResourceManager.h"
#include "VulkanContext.h"
#include "Audio/AudioDevice.h"
#include "Core/Events/EventBus.h"
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

void ResourceManager::init(const VulkanContext& ctx, AudioDevice& audio, EventBus& bus) {
    (void)ctx;
    m_audio = &audio;
    m_shaders.init(bus);
    FP_CORE_INFO("ResourceManager initialized");
}

void ResourceManager::shutdown(const VulkanContext& ctx) {
    // Reverse order of dependency: sprites/materials reference textures/shaders,
    // so data registries are cleared first, then GPU caches are destroyed.
    m_animations.clear();
    m_sprites.clear();
    m_materials.clear();
    m_sounds.shutdown();
    m_fonts.shutdown(ctx);
    m_textures.shutdown(ctx);
    m_shaders.shutdown(ctx);
    m_audio = nullptr;
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

    // ── 4. Fonts ────────────────────────────────────────────────────────────
    loadFonts(ctx, doc);

    // ── 5. Sounds ───────────────────────────────────────────────────────────
    loadSounds(doc);

    // ── 5b. Animations + animation sets ─────────────────────────────────────
    loadAnimations(doc);

    // ── 6. Materials ────────────────────────────────────────────────────────
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

    // Disk-to-manifest validation used to live here, asserting that every
    // PNG under assets/textures/ had a sprite entry. After the move to
    // namespaced manifests (per-faction prefixes), the file name no longer
    // maps 1-to-1 to a sprite id, so the strict check would false-positive.
    // The bake step does the manifest-side cross-references instead.
}

void ResourceManager::loadFonts(const VulkanContext& ctx, const nlohmann::json& doc) {
    if (!doc.contains("fonts")) return;

    for (const auto& e : doc.at("fonts")) {
        const std::string name = e.at("name").get<std::string>();
        const std::string path = e.at("path").get<std::string>();
        const int32_t     size = e.at("size").get<int32_t>();

        FP_CORE_ASSERT(fs::exists(path),
            "Font '{}' declared in assets.json references missing TTF '{}'",
            name, path);

        m_fonts.load(ctx, FontID(name.c_str()), path, size);
    }
}

void ResourceManager::loadAnimations(const nlohmann::json& doc) {
    // Animations are sprite-frame tracks; sets bundle named states pointing
    // at those tracks. Either section may be missing in a manifest that
    // doesn't use animations (the test scene starts that way).
    if (doc.contains("animations")) {
        for (const auto& e : doc.at("animations")) {
            Animation anim;
            for (const auto& f : e.at("frames")) {
                anim.frames.emplace_back(f.get<std::string>().c_str());
            }
            anim.fps = e.value("fps", 8.0f);
            m_animations.addAnimation(
                AnimationID(e.at("name").get<std::string>().c_str()),
                std::move(anim));
        }
    }

    if (doc.contains("animation_sets")) {
        for (const auto& e : doc.at("animation_sets")) {
            AnimationSet set;
            for (auto it = e.at("states").begin(); it != e.at("states").end(); ++it) {
                AnimationState st;
                st.animation = AnimationID(it.value().at("animation").get<std::string>().c_str());
                st.loop      = it.value().value("loop", true);
                if (it.value().contains("next")) {
                    st.next = StateID(it.value().at("next").get<std::string>().c_str());
                }
                if (it.value().contains("transitions")) {
                    for (const auto& t : it.value().at("transitions")) {
                        AnimationTransition tr;
                        tr.trigger = TriggerID(t.at("trigger").get<std::string>().c_str());
                        tr.target  = StateID  (t.at("target" ).get<std::string>().c_str());
                        st.transitions.push_back(tr);
                    }
                }
                set.states.emplace(StateID(it.key().c_str()), std::move(st));
            }
            set.defaultState = StateID(e.at("default").get<std::string>().c_str());
            m_animations.addSet(
                AnimationSetID(e.at("name").get<std::string>().c_str()),
                std::move(set));
        }
    }
}

void ResourceManager::loadSounds(const nlohmann::json& doc) {
    if (!doc.contains("sounds")) {
        FP_CORE_INFO("ResourceManager: no 'sounds' section in manifest");
        return;
    }

    FP_CORE_ASSERT(m_audio != nullptr,
                   "ResourceManager::loadSounds called before AudioDevice was set");

    const auto& arr = doc.at("sounds");
    FP_CORE_INFO("ResourceManager: processing {} sound entry/entries", arr.size());

    for (const auto& e : arr) {
        const std::string  name  = e.at("name").get<std::string>();
        const std::string  path  = e.at("path").get<std::string>();
        const std::string  grp   = e.value("group", std::string{"sfx"});
        const bool         loop  = e.value("loop", false);

        FP_CORE_ASSERT(fs::exists(path),
            "Sound '{}' declared in assets.json references missing file '{}'",
            name, path);

        m_sounds.load(*m_audio, SoundID(name.c_str()), path,
                      GroupID(grp.c_str()), loop);
    }
}

} // namespace engine
