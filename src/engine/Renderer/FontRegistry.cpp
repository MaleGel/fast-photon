#include "FontRegistry.h"
#include "FontAtlasBuilder.h"
#include "VulkanContext.h"
#include "Core/Log/Log.h"

namespace engine {

void FontRegistry::shutdown(const VulkanContext& ctx) {
    for (auto& [id, font] : m_fonts) {
        font.destroy(ctx);
        FP_CORE_TRACE("Font destroyed: '{}'", id.c_str());
    }
    m_fonts.clear();
}

bool FontRegistry::load(const VulkanContext& ctx, FontID id,
                        const std::string& ttfPath, int32_t pixelSize) {
    Font font;
    if (!FontAtlasBuilder::build(ctx, ttfPath, pixelSize, font)) {
        return false;
    }
    m_fonts.emplace(id, std::move(font));
    FP_CORE_INFO("Font registered: '{}' ({}px, from {})",
                 id.c_str(), pixelSize, ttfPath);
    return true;
}

const Font* FontRegistry::get(FontID id) const {
    auto it = m_fonts.find(id);
    if (it == m_fonts.end()) {
        FP_CORE_WARN("Font not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
