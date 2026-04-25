#pragma once
#include "Renderer/ResourceTypes.h"

#include <string>
#include <unordered_map>

// Forward-declare only 'ma_engine' as a struct — miniaudio itself uses it as
// an opaque type in many places. ma_sound and ma_sound_group are typedefs in
// miniaudio, so we can NOT forward-declare them as structs here; we use raw
// ma_engine*/ma_sound_group* (owning) and destroy them explicitly in the .cpp.
struct ma_engine;

namespace engine {

struct Sound;

// Parameters you can vary per play() call. Defaults = "natural" playback.
struct PlayParams {
    float volume = 1.0f;       // 0..∞, multiplied against the sound's group
    float pitch  = 1.0f;       // 1 = original, 2 = octave up, 0.5 = octave down
};

// Owns the miniaudio engine plus a set of volume groups (master/music/sfx/ui).
// Sounds are registered separately through SoundCache — AudioDevice exposes
// the engine pointer that SoundCache uses to construct ma_sound objects.
class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice();

    AudioDevice(const AudioDevice&)            = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void init();
    void shutdown();

    // Create a volume group ('master', 'sfx', 'ui', ...). Group id is used
    // later in sound entries so each sound routes into the right bus.
    void createGroup(GroupID id);
    void setGroupVolume(GroupID id, float volume);

    // Play a previously-registered Sound. 'sound' is borrowed — caller keeps
    // ownership. Restarts playback from the beginning every call.
    void play(Sound& sound, const PlayParams& params = {});

    // Stop this sound if it is currently playing.
    void stop(Sound& sound);

    // Stop every playing sound.
    void stopAll();

    // Used by SoundCache when constructing ma_sound objects.
    // Returns void* because ma_sound_group is a miniaudio typedef that we
    // can't name in this header. SoundCache casts it back to ma_sound_group*.
    ma_engine* engine()              { return m_engine; }
    void*      groupHandle(GroupID id);

private:
    ma_engine*                               m_engine = nullptr;   // owned
    // Values are ma_sound_group*, stored as void* so we don't need the
    // full miniaudio header here. Destroyed in shutdown().
    std::unordered_map<GroupID, void*>       m_groups;
};

} // namespace engine
