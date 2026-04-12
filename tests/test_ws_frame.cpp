#include <catch2/catch_test_macros.hpp>

#include "net/ws_frame.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ip::net;

// Build a client-to-server text frame: FIN=1, opcode=1, MASK=1,
// payload length fits in 7 bits, masking key = {0x37,0xfa,0x21,0x3d}.
// Payload: "Hello"
static std::vector<uint8_t> hello_frame() {
    std::vector<uint8_t> f = {
        0x81,       // FIN=1, RSV=0, opcode=0x1 (text)
        0x85,       // MASK=1, len=5
        0x37, 0xfa, 0x21, 0x3d, // masking key
        // "Hello" XORed with the key bytes cycled.
        0x7f, 0x9f, 0x4d, 0x51, 0x58,
    };
    return f;
}

TEST_CASE("decode single masked text frame", "[ws_frame]") {
    auto bytes = hello_frame();
    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    REQUIRE(result.status == DecodeStatus::MessageReady);
    REQUIRE(result.message == "Hello");
    REQUIRE(result.consumed == bytes.size());
    REQUIRE(result.opcode == Opcode::Text);
}

TEST_CASE("decode rejects unmasked client frame", "[ws_frame]") {
    // Same as above but MASK bit cleared.
    std::vector<uint8_t> f = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(f.data()), f.size());
    REQUIRE(result.status == DecodeStatus::Error);
}

TEST_CASE("decode partial frame yields NeedMore then completes", "[ws_frame]") {
    auto bytes = hello_frame();
    FrameDecoder dec;
    auto part1 = dec.feed(reinterpret_cast<const char*>(bytes.data()), 3);
    REQUIRE(part1.status == DecodeStatus::NeedMore);
    auto part2 = dec.feed(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
    REQUIRE(part2.status == DecodeStatus::MessageReady);
    REQUIRE(part2.message == "Hello");
}

TEST_CASE("encode single-frame server text", "[ws_frame]") {
    auto bytes = encode_text("Hi");
    // FIN=1, opcode=1, MASK=0, len=2, then payload.
    REQUIRE(bytes.size() == 4);
    REQUIRE(static_cast<uint8_t>(bytes[0]) == 0x81);
    REQUIRE(static_cast<uint8_t>(bytes[1]) == 0x02);
    REQUIRE(bytes[2] == 'H');
    REQUIRE(bytes[3] == 'i');
}

TEST_CASE("decode 16-bit extended length", "[ws_frame]") {
    // 200-byte payload uses 2-byte extended length (opcode 126).
    std::string payload(200, 'a');
    uint8_t key[4] = {0x11, 0x22, 0x33, 0x44};

    std::vector<uint8_t> f;
    f.push_back(0x81);
    f.push_back(0xFE); // MASK=1, len=126
    f.push_back(0x00);
    f.push_back(0xC8); // 200
    f.insert(f.end(), key, key + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        f.push_back(static_cast<uint8_t>(payload[i] ^ key[i % 4]));
    }

    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(f.data()), f.size());
    REQUIRE(result.status == DecodeStatus::MessageReady);
    REQUIRE(result.message.size() == 200);
    REQUIRE(result.message.front() == 'a');
    REQUIRE(result.message.back() == 'a');
}
