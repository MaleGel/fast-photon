#include "AudioDevice.h"
#include "Sound.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <miniaudio.h>

#include <stdexcept>

namespace engine {

AudioDevice::~AudioDevice() {
    // Safety net if shutdown() wasn't called explicitly.
    if (m_engine != nullptr || !m_groups.empty()) {
        shutdown();
    }
}

// ── Public ───────────────────────────────────────────────────────────────────

void AudioDevice::init() {
    FP_CORE_ASSERT(m_engine == nullptr, "AudioDevice already initialised");

    m_engine = new ma_engine;
    ma_engine_config cfg = ma_engine_config_init();
    ma_result r = ma_engine_init(&cfg, m_engine);
    if (r != MA_SUCCESS) {
        delete m_engine;
        m_engine = nullptr;
        throw std::runtime_error("AudioDevice: ma_engine_init failed");
    }

    FP_CORE_INFO("AudioDevice initialized");
}

void AudioDevice::shutdown() {
    // Groups must be uninitialised before the engine they were created in.
    for (auto& [id, raw] : m_groups) {
        auto* grp = static_cast<ma_sound_group*>(raw);
        ma_sound_group_uninit(grp);
        delete grp;
    }
    m_groups.clear();

    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
    FP_CORE_TRACE("AudioDevice destroyed");
}

void AudioDevice::createGroup(GroupID id) {
    FP_CORE_ASSERT(m_engine != nullptr, "AudioDevice::createGroup before init");

    auto* grp = new ma_sound_group;
    ma_result r = ma_sound_group_init(m_engine, 0, /*parent*/ nullptr, grp);
    if (r != MA_SUCCESS) {
        delete grp;
        FP_CORE_ERROR("AudioDevice: failed to create group '{}'", id.c_str());
        return;
    }
    m_groups.emplace(id, grp);
    FP_CORE_TRACE("AudioDevice: created group '{}'", id.c_str());
}

void AudioDevice::setGroupVolume(GroupID id, float volume) {
    auto it = m_groups.find(id);
    if (it == m_groups.end()) {
        FP_CORE_WARN("AudioDevice: unknown group '{}'", id.c_str());
        return;
    }
    ma_sound_group_set_volume(static_cast<ma_sound_group*>(it->second), volume);
}

void* AudioDevice::groupHandle(GroupID id) {
    auto it = m_groups.find(id);
    return it != m_groups.end() ? it->second : nullptr;
}

void AudioDevice::play(Sound& sound, const PlayParams& params) {
    if (!sound.handle) return;
    auto* s = static_cast<ma_sound*>(sound.handle);

    // Restart from the beginning if already playing.
    ma_sound_stop(s);
    ma_sound_seek_to_pcm_frame(s, 0);

    ma_sound_set_volume(s, params.volume);
    ma_sound_set_pitch (s, params.pitch);

    ma_sound_start(s);
}

void AudioDevice::stop(Sound& sound) {
    if (sound.handle) {
        ma_sound_stop(static_cast<ma_sound*>(sound.handle));
    }
}

void AudioDevice::stopAll() {
    if (m_engine) {
        ma_engine_stop(m_engine);
        ma_engine_start(m_engine);
    }
}

} // namespace engine
