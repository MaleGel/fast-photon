#pragma once
#include "ResourceTypes.h"
#include "Sprite.h"
#include <unordered_map>

namespace engine {

class SpriteRegistry {
public:
    void add(SpriteID id, Sprite sprite);

    // Returns nullptr if not found.
    const Sprite* get(SpriteID id) const;

    void clear() { m_sprites.clear(); }

private:
    std::unordered_map<SpriteID, Sprite> m_sprites;
};

} // namespace engine
