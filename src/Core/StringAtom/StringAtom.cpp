#include "StringAtom.h"
#include "Core/Assert/Assert.h"

#include <cstring>
#include <unordered_map>

namespace engine {

// ── Global pool (Construct On First Use to avoid static init fiasco) ──────────
static std::unordered_map<uint32_t, std::string>& pool() {
    static std::unordered_map<uint32_t, std::string> s_pool;
    return s_pool;
}

// FNV-1a 32-bit: simple, fast, excellent distribution for short identifiers.
uint32_t StringAtom::computeHash(const char* str, size_t len) {
    constexpr uint32_t kOffsetBasis = 2166136261u;
    constexpr uint32_t kPrime       = 16777619u;

    uint32_t h = kOffsetBasis;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(str[i]);
        h *= kPrime;
    }
    // Reserve 0 for "invalid/empty" atoms.
    return h == 0 ? 1 : h;
}

StringAtom::StringAtom(const char* str) {
    if (!str || *str == '\0') {
        m_hash = 0;
        return;
    }

    const size_t len = std::strlen(str);
    m_hash = computeHash(str, len);

    auto& p = pool();
    auto it = p.find(m_hash);
    if (it == p.end()) {
        p.emplace(m_hash, std::string(str, len));
    } else {
#ifdef FP_DEBUG
        // Hash collision detection: same hash must map to same string.
        FP_CORE_ASSERT(it->second == str,
            "StringAtom collision: '{}' vs '{}' (hash {})",
            it->second, str, m_hash);
#endif
    }
}

StringAtom::StringAtom(const std::string& str)
    : StringAtom(str.c_str()) {}

const char* StringAtom::c_str() const {
    if (m_hash == 0) return "";
    auto& p = pool();
    auto it = p.find(m_hash);
    return it != p.end() ? it->second.c_str() : "<unknown>";
}

} // namespace engine
