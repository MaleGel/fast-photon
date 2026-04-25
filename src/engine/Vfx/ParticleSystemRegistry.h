#pragma once
#include "ParticleSystem.h"
#include "Renderer/ResourceTypes.h"

#include <unordered_map>

namespace engine {

class ParticleSystemRegistry {
public:
    void add(ParticleSystemID id, ParticleSystem system);

    // Returns nullptr if not found.
    const ParticleSystem* get(ParticleSystemID id) const;

    void clear();

    // Iterate every registered system. VfxSystem uses this to allocate
    // a GPU pool per system on init.
    template<typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [id, sys] : m_systems) fn(id, sys);
    }

private:
    std::unordered_map<ParticleSystemID, ParticleSystem> m_systems;
};

} // namespace engine
