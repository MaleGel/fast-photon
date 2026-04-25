#include "SoundCache.h"
#include "AudioDevice.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <miniaudio.h>

namespace engine {

void SoundCache::shutdown() {
    for (auto& [id, sound] : m_sounds) {
        if (sound.handle) {
            auto* s = static_cast<ma_sound*>(sound.handle);
            ma_sound_uninit(s);
            delete s;
            sound.handle = nullptr;
        }
    }
    m_sounds.clear();
}

bool SoundCache::load(AudioDevice& device, SoundID id,
                      const std::string& path, GroupID group, bool loop) {
    ma_engine* engine = device.engine();
    auto*      grp    = static_cast<ma_sound_group*>(device.groupHandle(group));
    if (!engine) {
        FP_CORE_ERROR("SoundCache::load: AudioDevice not initialised");
        return false;
    }
    if (!grp) {
        FP_CORE_ERROR("SoundCache::load: sound '{}' targets unknown group '{}'",
                      id.c_str(), group.c_str());
        return false;
    }

    auto* handle = new ma_sound;
    ma_uint32 flags = 0;
    ma_result r = ma_sound_init_from_file(engine, path.c_str(), flags,
                                          grp, /*fence*/ nullptr, handle);
    if (r != MA_SUCCESS) {
        delete handle;
        FP_CORE_ERROR("SoundCache: failed to load '{}' from {} (error {})",
                      id.c_str(), path, static_cast<int>(r));
        return false;
    }

    ma_sound_set_looping(handle, loop ? MA_TRUE : MA_FALSE);

    Sound sound;
    sound.group  = group;
    sound.loop   = loop;
    sound.handle = handle;

    m_sounds.emplace(id, std::move(sound));
    FP_CORE_INFO("Sound loaded: '{}' <- {} (group='{}', loop={})",
                 id.c_str(), path, group.c_str(), loop);
    return true;
}

Sound* SoundCache::get(SoundID id) {
    auto it = m_sounds.find(id);
    if (it == m_sounds.end()) {
        FP_CORE_WARN("Sound not found: '{}'", id.c_str());
        return nullptr;
    }
    return &it->second;
}

} // namespace engine
