#pragma once
#include "ResourceTypes.h"
#include "Material.h"
#include <unordered_map>

namespace engine {

class MaterialRegistry {
public:
    void add(MaterialID id, Material material);

    // Returns nullptr if not found.
    const Material* get(MaterialID id) const;

    void clear() { m_materials.clear(); }

private:
    std::unordered_map<MaterialID, Material> m_materials;
};

} // namespace engine
