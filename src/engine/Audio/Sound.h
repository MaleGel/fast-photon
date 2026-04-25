#pragma once
#include "Renderer/ResourceTypes.h"

namespace engine {

// One loaded sound — owns a miniaudio ma_sound object tied to the owning
// AudioDevice's engine.
//
// We store the ma_sound as a raw void* because ma_sound is a miniaudio
// typedef and can't be forward-declared here. SoundCache allocates and
// frees it via ma_sound_init_from_file / ma_sound_uninit.
struct Sound {
    GroupID group;
    bool    loop   = false;
    void*   handle = nullptr;   // ma_sound*, owned
};

} // namespace engine
