#include "session/player.h"

#include <cctype>

namespace ip::session {

bool validate_nickname(const std::string& s) {
    if (s.empty()) return false;
    // Byte length <= 48 is a cheap upper bound for 16 UTF-8 code points (3 bytes each).
    if (s.size() > 48) return false;
    if (std::isspace(static_cast<unsigned char>(s.front()))) return false;
    if (std::isspace(static_cast<unsigned char>(s.back()))) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    // Approximate code-point count: number of bytes that are not continuation bytes.
    std::size_t cps = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++cps;
    return cps >= 1 && cps <= 16;
}

} // namespace ip::session
