#include "Utf8.h"

namespace engine {

// Standard UTF-8 → codepoint decoder. Handles 1/2/3/4-byte sequences.
// Reference: RFC 3629. Any malformed input becomes U+FFFD.
std::vector<char32_t> utf8Decode(std::string_view input) {
    constexpr char32_t kReplacement = 0xFFFD;

    std::vector<char32_t> out;
    out.reserve(input.size());  // upper bound (1 byte/codepoint ASCII)

    size_t i = 0;
    while (i < input.size()) {
        unsigned char b0 = static_cast<unsigned char>(input[i]);

        // 1-byte ASCII (0xxxxxxx)
        if ((b0 & 0x80u) == 0) {
            out.push_back(b0);
            i += 1;
            continue;
        }

        // Determine sequence length by leading bits.
        int extra = 0;
        char32_t cp = 0;
        if ((b0 & 0xE0u) == 0xC0u) { extra = 1; cp = b0 & 0x1Fu; }   // 110xxxxx
        else if ((b0 & 0xF0u) == 0xE0u) { extra = 2; cp = b0 & 0x0Fu; }   // 1110xxxx
        else if ((b0 & 0xF8u) == 0xF0u) { extra = 3; cp = b0 & 0x07u; }   // 11110xxx
        else {
            out.push_back(kReplacement);   // lone continuation or 5+ byte marker
            i += 1;
            continue;
        }

        if (i + extra >= input.size()) {
            out.push_back(kReplacement);   // truncated at string end
            break;
        }

        bool valid = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char bk = static_cast<unsigned char>(input[i + k]);
            if ((bk & 0xC0u) != 0x80u) { valid = false; break; }   // not 10xxxxxx
            cp = (cp << 6) | (bk & 0x3Fu);
        }

        if (valid) {
            out.push_back(cp);
            i += 1 + extra;
        } else {
            out.push_back(kReplacement);
            i += 1;   // resync one byte at a time
        }
    }

    return out;
}

} // namespace engine
