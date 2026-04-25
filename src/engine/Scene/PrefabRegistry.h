#pragma once
#include "Prefab.h"
#include <string>
#include <unordered_map>

namespace engine {

// Holds every prefab discovered at asset-load time. Prefab IDs are derived
// from file paths relative to the prefabs root, without the .json extension
// and with forward slashes — e.g. 'units/player/warrior'.
class PrefabRegistry {
public:
    // Recursively scan <rootDir> for *.json files and load each as a prefab.
    void loadFromDirectory(const std::string& rootDir);

    // Returns nullptr if id is not registered.
    const Prefab* get(PrefabID id) const;

    void clear() { m_prefabs.clear(); }

    size_t size() const { return m_prefabs.size(); }

private:
    std::unordered_map<PrefabID, Prefab> m_prefabs;
};

} // namespace engine
