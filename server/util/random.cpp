#include "util/random.h"

namespace ip::util {

static constexpr const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 chars

RoomCodeGen::RoomCodeGen(uint64_t seed) : rng_(seed) {}

std::string RoomCodeGen::generate() {
    std::uniform_int_distribution<int> dist(0, 31);
    std::string out(6, ' ');
    for (std::size_t i = 0; i < 6; ++i) out[i] = kAlphabet[dist(rng_)];
    return out;
}

} // namespace ip::util
