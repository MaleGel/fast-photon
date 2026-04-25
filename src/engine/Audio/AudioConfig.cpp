#include "AudioConfig.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace engine {

AudioConfig loadAudioConfig(const std::string& path) {
    AudioConfig cfg;

    std::ifstream file(path);
    FP_CORE_ASSERT(file.is_open(), "Cannot open audio config: {}", path);

    nlohmann::json doc = nlohmann::json::parse(file);
    if (doc.contains("groups")) {
        for (auto it = doc.at("groups").begin(); it != doc.at("groups").end(); ++it) {
            cfg.groupVolumes.emplace(GroupID(it.key().c_str()),
                                     it.value().get<float>());
        }
    }

    FP_CORE_INFO("AudioConfig loaded from {} ({} group(s))",
                 path, cfg.groupVolumes.size());
    return cfg;
}

} // namespace engine
