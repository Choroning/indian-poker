#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace ip::util {

std::string base64_encode(const uint8_t* data, std::size_t len);

inline std::string base64_encode(const std::string& s) {
    return base64_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

template <std::size_t N>
inline std::string base64_encode(const std::array<uint8_t, N>& a) {
    return base64_encode(a.data(), N);
}

} // namespace ip::util
