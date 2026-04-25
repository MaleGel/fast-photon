#pragma once
#include <cstdint>

namespace engine {

enum class Faction : uint8_t {
    Player  = 0,
    Enemy   = 1,
    Neutral = 2,
};

} // namespace engine
