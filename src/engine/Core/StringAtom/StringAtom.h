#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace engine {

// Interned string identifier.
// A StringAtom stores only the 32-bit FNV-1a hash of the original string.
// The full string is kept in a global pool (shared across the program) so it
// can be recovered for logging. Comparison and hashing are therefore O(1).
//
// Use StringAtom as a map key instead of std::string whenever the value is
// a stable identifier (asset name, event name, component tag, etc).
class StringAtom {
public:
    constexpr StringAtom() = default;

    StringAtom(const char* str);
    StringAtom(const std::string& str);

    uint32_t    hash()     const { return m_hash; }
    const char* c_str()    const;
    bool        isValid()  const { return m_hash != 0; }

    bool operator==(StringAtom o) const { return m_hash == o.m_hash; }
    bool operator!=(StringAtom o) const { return m_hash != o.m_hash; }

    // FNV-1a 32-bit. Exposed so tools/tests can compute the same hash.
    static uint32_t computeHash(const char* str, size_t len);

private:
    uint32_t m_hash = 0;
};

} // namespace engine

// Specialization so StringAtom can be used directly in std::unordered_map.
// Since the hash is already stored inside, this is literally a getter — O(1).
namespace std {
    template<> struct hash<engine::StringAtom> {
        size_t operator()(engine::StringAtom a) const noexcept { return a.hash(); }
    };
}
