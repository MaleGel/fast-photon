#include "MaterialRegistry.h"
#include "Core/Log/Log.h"

namespace engine {

void MaterialRegistry::add(MaterialID id, Material material) {
    m_materials.emplace(id, std::move(material));
    FP_CORE_INFO("Material registered: '{}'", id.c_str());
}

const Material* MaterialRegistry::get(MaterialID id) const {
    auto it = m_materials.find(id);
    if (it == m_materials.end()) {
        FP_CORE_WARN("Material not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
