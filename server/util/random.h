#pragma once
#include <cstdint>
#include <random>
#include <string>

namespace ip::util {

// Generates 6-character room codes drawn from a 32-letter confusion-free alphabet
// (A-Z minus I, O; 2-9).
class RoomCodeGen {
public:
    explicit RoomCodeGen(uint64_t seed);
    std::string generate();

private:
    std::mt19937_64 rng_;
};

} // namespace ip::util
