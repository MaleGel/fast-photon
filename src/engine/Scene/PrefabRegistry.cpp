#include "PrefabRegistry.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <filesystem>
#include <fstream>

namespace engine {

namespace fs = std::filesystem;

void PrefabRegistry::loadFromDirectory(const std::string& rootDir) {
    // We expect rootDir to be 'assets/data/factions'. Each immediate
    // subdirectory is a faction; everything inside it that has a
    // 'components' key is a prefab. The faction name becomes the ID
    // prefix, mirroring the manifest baker's namespacing rules.
    const fs::path root(rootDir);
    FP_CORE_INFO("PrefabRegistry: scanning '{}' (absolute: '{}')",
                 rootDir, fs::absolute(root).string());

    if (!fs::is_directory(root)) {
        FP_CORE_WARN("PrefabRegistry: directory '{}' does not exist — no prefabs loaded",
                     rootDir);
        return;
    }

    size_t loaded = 0;
    for (const auto& factionEntry : fs::directory_iterator(root)) {
        if (!factionEntry.is_directory()) continue;
        const std::string faction = factionEntry.path().filename().string();
        const fs::path factionRoot = factionEntry.path();

        for (const auto& entry : fs::recursive_directory_iterator(factionRoot)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            // assets.json files at the faction root are namespaced asset
            // manifests, not prefabs. Skip them explicitly so we don't
            // need to rely on the 'components' presence check alone.
            if (entry.path().filename() == "assets.json") continue;

            std::ifstream file(entry.path());
            FP_CORE_ASSERT(file.is_open(), "PrefabRegistry: cannot open '{}'",
                           entry.path().string());

            nlohmann::json doc = nlohmann::json::parse(file);
            // A prefab is a JSON object with a top-level 'components' key —
            // everything else we silently skip (not every .json is a prefab).
            if (!doc.is_object() || !doc.contains("components")) {
                continue;
            }

            // ID = "<faction>/<rel-path-without-ext>", e.g.
            //      "player/units/warrior".
            fs::path rel = fs::relative(entry.path(), factionRoot);
            rel.replace_extension();
            const std::string idStr = faction + "/" + rel.generic_string();

            Prefab p;
            p.components = doc.at("components");
            m_prefabs.emplace(PrefabID(idStr.c_str()), std::move(p));
            FP_CORE_TRACE("PrefabRegistry: loaded prefab '{}' from {}",
                         idStr, entry.path().string());
            ++loaded;
        }
    }

    FP_CORE_INFO("PrefabRegistry: loaded {} prefab(s) from '{}'", loaded, rootDir);
}

const Prefab* PrefabRegistry::get(PrefabID id) const {
    auto it = m_prefabs.find(id);
    if (it == m_prefabs.end()) {
        FP_CORE_WARN("Prefab not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
