#pragma once
#include <string>

namespace engine {

class World;
class EventBus;
class PrefabRegistry;

// Loads a scene description from a JSON file into an (empty) World.
//
// Expected JSON shape:
//   {
//     "grid":     { "width": N, "height": N, "overrides": [...] },
//     "turn":     { "factionOrder": [...], "actionsPerTurn": N },
//     "camera":   { "position": [x, y] },
//     "entities": [
//       // prefab-based (common case):
//       { "prefab": "units/player/warrior",
//         "grid":   { "col": 2, "row": 3 },
//         "overrides": { "Tag": "Hero", ... } },
//
//       // inline-components (for scene-only entities without a prefab):
//       { "components": { "Tag": "Decoration", "Transform": {...}, ... } }
//     ]
//   }
//
// Camera tuning (zoom, pan speed, ...) comes from the separate CameraConfig
// file — the scene only specifies position.
//
// Contract violations (missing required field, unknown enum, unknown
// component, missing prefab) trigger FP_CORE_ASSERT.
class JsonSceneLoader {
public:
    static void load(World& world, EventBus& bus,
                     const PrefabRegistry& prefabs,
                     const std::string& path, float aspectRatio);
};

} // namespace engine
