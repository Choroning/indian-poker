#include <catch2/catch_test_macros.hpp>

#include "net/ws_handshake.h"

using namespace ip::net;

static constexpr const char* kGoodRequest =
    "GET /game HTTP/1.1\r\n"
    "Host: localhost:8080\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Origin: http://localhost:3000\r\n"
    "\r\n";

TEST_CASE("complete request is parsed and accepted", "[handshake]") {
    HandshakeResult r = parse_handshake(kGoodRequest);
    REQUIRE(r.status == HandshakeStatus::Complete);
    REQUIRE(r.origin == "http://localhost:3000");
    REQUIRE(r.key == "dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(r.consumed == std::string_view(kGoodRequest).size());
}

TEST_CASE("partial request yields NeedMore", "[handshake]") {
    std::string partial = "GET /game HTTP/1.1\r\nHost: localhost:8080\r\n";
    HandshakeResult r = parse_handshake(partial);
    REQUIRE(r.status == HandshakeStatus::NeedMore);
}

TEST_CASE("missing Upgrade header is rejected", "[handshake]") {
    std::string bad =
        "GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HandshakeResult r = parse_handshake(bad);
    REQUIRE(r.status == HandshakeStatus::Invalid);
}

TEST_CASE("accept value matches RFC example", "[handshake]") {
    auto accept = compute_accept("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("build_response contains Sec-WebSocket-Accept", "[handshake]") {
    auto response = build_response("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(response.find("HTTP/1.1 101") != std::string::npos);
    REQUIRE(response.find("Upgrade: websocket") != std::string::npos);
    REQUIRE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    REQUIRE(response.find("\r\n\r\n") != std::string::npos);
}
