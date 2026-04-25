#pragma once
#include "Sound.h"
#include "Renderer/ResourceTypes.h"

#include <string>
#include <unordered_map>

namespace engine {

class AudioDevice;

// Owns every Sound loaded during asset registration. Keyed by SoundID
// ('footstep', 'ui.click', ...).
class SoundCache {
public:
    void shutdown();

    // Load a sound file from disk and bind it to 'group' on 'device'.
    // 'loop' makes the sound repeat on play() (typical for music tracks).
    bool load(AudioDevice& device, SoundID id,
              const std::string& path, GroupID group, bool loop);

    // Returns nullptr if not found.
    Sound* get(SoundID id);

private:
    std::unordered_map<SoundID, Sound> m_sounds;
};

} // namespace engine
