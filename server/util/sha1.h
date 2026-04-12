#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ip::util {

// Returns the 20-byte SHA-1 digest of `input`.
std::array<uint8_t, 20> sha1(std::string_view input);

// Overload that accepts bytes as a string.
inline std::array<uint8_t, 20> sha1(const std::string& input) {
    return sha1(std::string_view{input});
}

} // namespace ip::util
