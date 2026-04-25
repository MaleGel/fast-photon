#include "ParticleSystemRegistry.h"
#include "Core/Log/Log.h"

namespace engine {

void ParticleSystemRegistry::add(ParticleSystemID id, ParticleSystem system) {
    m_systems[id] = std::move(system);
    FP_CORE_TRACE("ParticleSystem registered: '{}'", id.c_str());
}

const ParticleSystem* ParticleSystemRegistry::get(ParticleSystemID id) const {
    auto it = m_systems.find(id);
    return it == m_systems.end() ? nullptr : &it->second;
}

void ParticleSystemRegistry::clear() {
    m_systems.clear();
}

} // namespace engine
