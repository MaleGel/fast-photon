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
using FontID     = StringAtom;
using SoundID    = StringAtom;
using GroupID    = StringAtom;
using AnimationID    = StringAtom;
using AnimationSetID = StringAtom;
// Name of a state inside an AnimationSet ("idle", "walking", ...). Same
// underlying type as the others — a StringAtom — but called out so call
// sites read clearly.
using StateID        = StringAtom;
// Name of a trigger that drives a transition between states ("attack",
// "stop", "land"). Game code fires triggers; the state machine resolves
// them against the current state's transition table.
using TriggerID      = StringAtom;

} // namespace engine
