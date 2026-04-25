#pragma once
#include "Renderer/ResourceTypes.h"

#include <string>
#include <unordered_map>

namespace engine {

// Per-group volume multipliers loaded from disk. Group ids become the
// default set of ma_sound_groups on AudioDevice; unlisted groups fall
// back to 1.0.
struct AudioConfig {
    std::unordered_map<GroupID, float> groupVolumes;
};

// Reads an audio config from JSON. Missing 'groups' section => empty map.
AudioConfig loadAudioConfig(const std::string& path);

} // namespace engine
