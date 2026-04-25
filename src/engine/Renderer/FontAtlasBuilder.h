#pragma once
#include "Font.h"

#include <string>

namespace engine {

class VulkanContext;

// Loads a TTF from disk and builds a fully populated Font: rasterizes every
// glyph from the hardcoded ASCII + Basic Cyrillic charset, packs them into
// the smallest power-of-two atlas that fits, uploads the atlas to a GPU
// texture. Caller owns the resulting Font.
namespace FontAtlasBuilder {

// 'pixelSize' is the target glyph height in pixels (matches 'fontSize' in
// BMFont / ImGui terminology).
bool build(const VulkanContext& ctx,
           const std::string& ttfPath,
           int32_t pixelSize,
           Font& outFont);

} // namespace FontAtlasBuilder

} // namespace engine
