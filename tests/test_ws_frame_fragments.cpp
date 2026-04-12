#include <catch2/catch_test_macros.hpp>

#include "net/ws_frame.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ip::net;

static std::vector<uint8_t> masked_frame(uint8_t opcode, bool fin, const std::string& payload) {
    std::vector<uint8_t> f;
    uint8_t b0 = static_cast<uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    f.push_back(b0);
    std::size_t n = payload.size();
    uint8_t b1;
    if (n < 126) {
        b1 = 0x80 | static_cast<uint8_t>(n);
        f.push_back(b1);
    } else if (n <= 0xFFFF) {
        b1 = 0x80 | 126;
        f.push_back(b1);
        f.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        f.push_back(static_cast<uint8_t>(n & 0xFF));
    } else {
        b1 = 0x80 | 127;
        f.push_back(b1);
        for (int i = 7; i >= 0; --i) f.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
    }
    uint8_t key[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    f.insert(f.end(), key, key + 4);
    for (std::size_t i = 0; i < n; ++i) {
        f.push_back(static_cast<uint8_t>(static_cast<uint8_t>(payload[i]) ^ key[i % 4]));
    }
    return f;
}

TEST_CASE("reassemble two-fragment text message", "[ws_frame][frag]") {
    auto f1 = masked_frame(0x1, false, "Hel");
    auto f2 = masked_frame(0x0, true,  "lo");

    FrameDecoder dec;
    auto r1 = dec.feed(reinterpret_cast<const char*>(f1.data()), f1.size());
    REQUIRE(r1.status == DecodeStatus::NeedMore);

    auto r2 = dec.feed(reinterpret_cast<const char*>(f2.data()), f2.size());
    REQUIRE(r2.status == DecodeStatus::MessageReady);
    REQUIRE(r2.opcode == Opcode::Text);
    REQUIRE(r2.message == "Hello");
}

TEST_CASE("ping frame interleaved between data fragments", "[ws_frame][control]") {
    auto f1 = masked_frame(0x1, false, "AB");
    auto ping = masked_frame(0x9, true, "x");
    auto f2 = masked_frame(0x0, true, "CD");

    FrameDecoder dec;
    auto r1 = dec.feed(reinterpret_cast<const char*>(f1.data()), f1.size());
    REQUIRE(r1.status == DecodeStatus::NeedMore);

    auto rp = dec.feed(reinterpret_cast<const char*>(ping.data()), ping.size());
    REQUIRE(rp.status == DecodeStatus::MessageReady);
    REQUIRE(rp.opcode == Opcode::Ping);
    REQUIRE(rp.message == "x");

    auto r2 = dec.feed(reinterpret_cast<const char*>(f2.data()), f2.size());
    REQUIRE(r2.status == DecodeStatus::MessageReady);
    REQUIRE(r2.opcode == Opcode::Text);
    REQUIRE(r2.message == "ABCD");
}

TEST_CASE("close frame is surfaced", "[ws_frame][control]") {
    auto close = masked_frame(0x8, true, "");
    FrameDecoder dec;
    auto r = dec.feed(reinterpret_cast<const char*>(close.data()), close.size());
    REQUIRE(r.status == DecodeStatus::MessageReady);
    REQUIRE(r.opcode == Opcode::Close);
}

TEST_CASE("encode pong and close", "[ws_frame][encode]") {
    auto pong = encode_control(Opcode::Pong, "hi");
    REQUIRE(pong.size() == 4);
    REQUIRE(static_cast<uint8_t>(pong[0]) == 0x8A);
    REQUIRE(static_cast<uint8_t>(pong[1]) == 0x02);

    auto cl = encode_control(Opcode::Close, "");
    REQUIRE(cl.size() == 2);
    REQUIRE(static_cast<uint8_t>(cl[0]) == 0x88);
    REQUIRE(static_cast<uint8_t>(cl[1]) == 0x00);
}

TEST_CASE("server can emit fragmented text for testing", "[ws_frame][encode]") {
    auto frames = encode_text_fragmented("HelloWorld", 4);
    // Expect 3 frames: "Hell" (FIN=0, opcode=1), "oWor" (FIN=0, opcode=0), "ld" (FIN=1, opcode=0)
    REQUIRE(frames.size() == 3);
    REQUIRE((static_cast<uint8_t>(frames[0][0]) & 0x0F) == 0x1);
    REQUIRE((static_cast<uint8_t>(frames[0][0]) & 0x80) == 0x00);
    REQUIRE((static_cast<uint8_t>(frames[1][0]) & 0x0F) == 0x0);
    REQUIRE((static_cast<uint8_t>(frames[1][0]) & 0x80) == 0x00);
    REQUIRE((static_cast<uint8_t>(frames[2][0]) & 0x0F) == 0x0);
    REQUIRE((static_cast<uint8_t>(frames[2][0]) & 0x80) == 0x80);
}
