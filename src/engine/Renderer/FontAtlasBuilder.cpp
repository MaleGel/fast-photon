#include "FontAtlasBuilder.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <stb_truetype.h>
#include <stb_rect_pack.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace engine {
namespace FontAtlasBuilder {

namespace {

// Hardcoded charset: printable ASCII + Basic Cyrillic block.
std::vector<char32_t> defaultCharset() {
    std::vector<char32_t> cs;
    cs.reserve(95 + 256);
    for (char32_t cp = 0x0020; cp <= 0x007E; ++cp) cs.push_back(cp);
    for (char32_t cp = 0x0400; cp <= 0x04FF; ++cp) cs.push_back(cp);
    return cs;
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const auto size = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return bytes;
}

uint32_t nextPow2(uint32_t n) {
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Packed-rect record we carry between the pack step and the blit step.
struct PackedGlyph {
    char32_t                 codepoint;
    int                      w, h;          // bitmap size in pixels
    int                      xoff, yoff;    // from stbtt (pen-origin → bitmap top-left)
    int                      advance;
    std::vector<uint8_t>     bitmap;        // 1 byte/pixel coverage, size = w*h
    int                      atlasX = 0, atlasY = 0;   // filled by rect_pack
    bool                     packed = false;
};

} // namespace

bool build(const VulkanContext& ctx, const std::string& ttfPath,
           int32_t pixelSize, Font& outFont) {
    // ── 1. Read TTF bytes ───────────────────────────────────────────────────
    const auto ttf = readBinaryFile(ttfPath);
    if (ttf.empty()) {
        FP_CORE_ERROR("FontAtlasBuilder: cannot open '{}'", ttfPath);
        return false;
    }

    stbtt_fontinfo info;
    const int fontOffset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (!stbtt_InitFont(&info, ttf.data(), fontOffset)) {
        FP_CORE_ERROR("FontAtlasBuilder: stbtt_InitFont failed for '{}'", ttfPath);
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(pixelSize));

    int asc = 0, desc = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &lineGap);
    const int ascent     = static_cast<int>(asc  * scale + 0.5f);
    const int descent    = static_cast<int>(desc * scale - 0.5f);   // desc is negative
    const int lineHeight = ascent - descent + static_cast<int>(lineGap * scale + 0.5f);

    // ── 2. Rasterize each glyph (coverage bitmap in CPU memory) ─────────────
    const auto charset = defaultCharset();
    std::vector<PackedGlyph> glyphs;
    glyphs.reserve(charset.size());

    for (char32_t cp : charset) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&info, static_cast<int>(cp), scale, scale,
                                    &x0, &y0, &x1, &y1);
        const int w = x1 - x0;
        const int h = y1 - y0;

        int advW = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, static_cast<int>(cp), &advW, &lsb);
        const int advance = static_cast<int>(advW * scale + 0.5f);

        PackedGlyph g;
        g.codepoint = cp;
        g.w         = std::max(0, w);
        g.h         = std::max(0, h);
        g.xoff      = x0;
        g.yoff      = y0;
        g.advance   = advance;

        if (g.w > 0 && g.h > 0) {
            g.bitmap.resize(static_cast<size_t>(g.w) * g.h);
            stbtt_MakeCodepointBitmap(&info, g.bitmap.data(),
                                      g.w, g.h, g.w, scale, scale,
                                      static_cast<int>(cp));
        }

        glyphs.push_back(std::move(g));
    }

    // ── 3. Pack bitmaps into the smallest POT square atlas that fits ────────
    uint64_t totalArea = 0;
    for (const auto& g : glyphs) totalArea += static_cast<uint64_t>(g.w) * g.h;
    uint32_t side = std::max<uint32_t>(128, nextPow2(static_cast<uint32_t>(
        std::sqrt(static_cast<double>(totalArea)) + 1.0)));

    constexpr uint32_t kMaxSide = 4096;
    bool packed = false;
    while (side <= kMaxSide && !packed) {
        std::vector<stbrp_rect> rects;
        rects.reserve(glyphs.size());
        for (size_t i = 0; i < glyphs.size(); ++i) {
            if (glyphs[i].w > 0 && glyphs[i].h > 0) {
                stbrp_rect r{};
                r.id = static_cast<int>(i);
                r.w  = glyphs[i].w;
                r.h  = glyphs[i].h;
                rects.push_back(r);
            }
        }

        std::vector<stbrp_node> nodes(side);
        stbrp_context rpCtx{};
        stbrp_init_target(&rpCtx, static_cast<int>(side), static_cast<int>(side),
                          nodes.data(), static_cast<int>(nodes.size()));
        stbrp_pack_rects(&rpCtx, rects.data(), static_cast<int>(rects.size()));

        bool allFit = true;
        for (const auto& r : rects) {
            if (!r.was_packed) { allFit = false; break; }
        }
        if (!allFit) { side *= 2; continue; }

        for (const auto& r : rects) {
            auto& g = glyphs[r.id];
            g.atlasX = r.x;
            g.atlasY = r.y;
            g.packed = true;
        }
        packed = true;
    }

    if (!packed) {
        FP_CORE_ERROR("FontAtlasBuilder: glyphs don't fit into {}x{} atlas", kMaxSide, kMaxSide);
        return false;
    }

    // ── 4. Blit coverage bitmaps into one RGBA atlas ────────────────────────
    // Each output pixel = (255, 255, 255, coverage). The text shader then
    // multiplies by the user's color so tinting is trivial.
    std::vector<uint8_t> atlasRgba(static_cast<size_t>(side) * side * 4, 0);
    for (const auto& g : glyphs) {
        if (!g.packed) continue;
        for (int y = 0; y < g.h; ++y) {
            for (int x = 0; x < g.w; ++x) {
                const uint8_t cov = g.bitmap[y * g.w + x];
                const size_t  idx = (static_cast<size_t>(g.atlasY + y) * side +
                                     (g.atlasX + x)) * 4;
                atlasRgba[idx + 0] = 255;
                atlasRgba[idx + 1] = 255;
                atlasRgba[idx + 2] = 255;
                atlasRgba[idx + 3] = cov;
            }
        }
    }

    // ── 5. Upload to GPU ────────────────────────────────────────────────────
    if (!outFont.texture().loadFromMemoryRGBA(ctx, atlasRgba.data(), side, side)) {
        FP_CORE_ERROR("FontAtlasBuilder: texture upload failed");
        return false;
    }

    // ── 6. Fill Font metadata ───────────────────────────────────────────────
    outFont.setMetrics(lineHeight, ascent, pixelSize);
    for (const auto& g : glyphs) {
        Glyph out;
        out.x        = g.atlasX;
        out.y        = g.atlasY;
        out.w        = g.w;
        out.h        = g.h;
        out.advance  = g.advance;
        out.bearingX = g.xoff;
        // yoff is top of bitmap relative to baseline (y grows down in stbtt).
        // bearingY is distance baseline → top of glyph, positive-up.
        out.bearingY = -g.yoff;
        outFont.addGlyph(g.codepoint, out);
    }

    FP_CORE_INFO("Font built: '{}' ({}px, {} glyphs, {}x{} atlas)",
                 ttfPath, pixelSize, glyphs.size(), side, side);
    return true;
}

} // namespace FontAtlasBuilder
} // namespace engine
