#pragma once
#include "Core/StringAtom/StringAtom.h"
#include <nlohmann/json.hpp>

namespace engine {

using PrefabID = StringAtom;

// A prefab is a partially pre-filled entity template. On disk it's just a
// JSON object with a 'components' map identical in shape to what scene
// entities used to carry inline. The scene loader applies the prefab's
// components first, then lets the scene entity override any of them.
struct Prefab {
    nlohmann::json components;   // the "components" object from the prefab file
};

} // namespace engine
