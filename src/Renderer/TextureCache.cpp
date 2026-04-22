#include "TextureCache.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"

namespace engine {

void TextureCache::shutdown(const VulkanContext& ctx) {
    for (auto& [id, tex] : m_textures) {
        tex.destroy(ctx);
        FP_CORE_TRACE("Texture destroyed: '{}'", id.c_str());
    }
    m_textures.clear();
}

bool TextureCache::load(const VulkanContext& ctx, TextureID id, const std::string& path) {
    Texture tex;
    if (!tex.loadFromFile(ctx, path)) {
        return false;
    }
    m_textures.emplace(id, std::move(tex));
    return true;
}

const Texture* TextureCache::get(TextureID id) const {
    auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        FP_CORE_WARN("Texture not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
