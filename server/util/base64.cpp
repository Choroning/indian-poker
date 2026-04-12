#include "util/base64.h"

#include <openssl/evp.h>
#include <vector>

namespace ip::util {

std::string base64_encode(const uint8_t* data, std::size_t len) {
    // Output length: 4 * ceil(len / 3) plus null terminator.
    std::size_t out_len = 4 * ((len + 2) / 3);
    std::vector<uint8_t> buf(out_len + 1);
    int n = ::EVP_EncodeBlock(buf.data(), data, static_cast<int>(len));
    return std::string(reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(n));
}

} // namespace ip::util
