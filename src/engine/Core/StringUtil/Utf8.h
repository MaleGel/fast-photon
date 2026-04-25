#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace engine {

// Decode a UTF-8 byte sequence into an array of Unicode codepoints.
// Invalid sequences are replaced with U+FFFD (replacement character) rather
// than aborting, so a bad string shows as '?' rather than crashing the game.
std::vector<char32_t> utf8Decode(std::string_view input);

} // namespace engine
