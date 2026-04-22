#pragma once
#include "Core/StringAtom/StringAtom.h"

namespace engine {

// Strongly-named aliases for asset identifiers.
// All are StringAtom under the hood (O(1) compare and hash).
// Using aliases rather than wrapper types keeps call sites readable while
// documenting the intended asset category at declaration sites.
using ShaderID   = StringAtom;
using TextureID  = StringAtom;
using MaterialID = StringAtom;
using SpriteID   = StringAtom;

} // namespace engine
