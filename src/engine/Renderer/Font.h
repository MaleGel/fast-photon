#pragma once
#include "Texture.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

class VulkanContext;

// Per-glyph layout data. Positions are pixels inside the atlas texture;
// bearings / advance are pixels in screen space.
//
//   atlas(x, y, w, h)   — where the glyph bitmap lives in the atlas
//   advance             — how far the pen moves after drawing this glyph
//   bearingX            — offset from pen to left edge of the bitmap
//   bearingY            — offset from baseline to top of the bitmap (up-positive)
struct Glyph {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    int32_t advance  = 0;
    int32_t bearingX = 0;
    int32_t bearingY = 0;
};

// A loaded font: owns the GPU atlas texture + per-glyph metrics.
// Destroy the Texture via destroy(ctx) before the owning registry goes away.
class Font {
public:
    Font() = default;
    Font(Font&&) noexcept = default;
    Font& operator=(Font&&) noexcept = default;
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    void destroy(const VulkanContext& ctx) { m_texture.destroy(ctx); m_glyphs.clear(); }

    const Texture&  texture()    const { return m_texture; }
    int32_t         lineHeight() const { return m_lineHeight; }
    int32_t         baseline()   const { return m_baseline; }
    int32_t         pixelSize()  const { return m_pixelSize; }

    // Returns nullptr if the codepoint wasn't baked into this font.
    const Glyph* getGlyph(char32_t cp) const {
        auto it = m_glyphs.find(cp);
        return it != m_glyphs.end() ? &it->second : nullptr;
    }

    // Mutators used by FontAtlasBuilder.
    Texture&  texture()                            { return m_texture; }
    void setMetrics(int32_t lineHeight, int32_t baseline, int32_t pixelSize) {
        m_lineHeight = lineHeight;
        m_baseline   = baseline;
        m_pixelSize  = pixelSize;
    }
    void addGlyph(char32_t cp, const Glyph& g)     { m_glyphs.emplace(cp, g); }

private:
    Texture                                 m_texture;
    std::unordered_map<char32_t, Glyph>     m_glyphs;
    int32_t                                 m_lineHeight = 0;
    int32_t                                 m_baseline   = 0;
    int32_t                                 m_pixelSize  = 0;
};

} // namespace engine
