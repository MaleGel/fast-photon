#pragma once
#include "ResourceTypes.h"
#include "Font.h"

#include <string>
#include <unordered_map>

namespace engine {

class VulkanContext;

// Owns every Font loaded at manifest-load time. Fonts are keyed by FontID
// (StringAtom) — the 'name' field of each assets.json fonts entry.
class FontRegistry {
public:
    void shutdown(const VulkanContext& ctx);

    // Rasterize a TTF at 'pixelSize' and register under 'id'. Returns true
    // on success. The font owns its GPU atlas until shutdown().
    bool load(const VulkanContext& ctx, FontID id,
              const std::string& ttfPath, int32_t pixelSize);

    // Returns nullptr if not found.
    const Font* get(FontID id) const;

private:
    std::unordered_map<FontID, Font> m_fonts;
};

} // namespace engine
