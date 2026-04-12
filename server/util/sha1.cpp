#include "util/sha1.h"

#include <openssl/sha.h>

namespace ip::util {

std::array<uint8_t, 20> sha1(std::string_view input) {
    std::array<uint8_t, 20> out{};
    ::SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), out.data());
    return out;
}

} // namespace ip::util
