#include "SpriteRegistry.h"
#include "Core/Log/Log.h"

namespace engine {

void SpriteRegistry::add(SpriteID id, Sprite sprite) {
    FP_CORE_INFO("Sprite registered: '{}' ({}x{} at {},{})",
                 id.c_str(), sprite.width, sprite.height, sprite.x, sprite.y);
    m_sprites.emplace(id, std::move(sprite));
}

const Sprite* SpriteRegistry::get(SpriteID id) const {
    auto it = m_sprites.find(id);
    if (it == m_sprites.end()) {
        FP_CORE_WARN("Sprite not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
