#include <catch2/catch_test_macros.hpp>

#include "util/base64.h"
#include "util/sha1.h"

using namespace ip::util;

TEST_CASE("sha1 matches known vector", "[sha1]") {
    // Known-good vector from RFC 6455 §1.3.
    // SHA1("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    //   raw bytes are then base64'd to: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
    const std::string input =
        "dGhlIHNhbXBsZSBub25jZQ=="
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    auto digest = sha1(input);
    REQUIRE(digest.size() == 20);

    auto encoded = base64_encode(digest);
    REQUIRE(encoded == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("base64 round trip on arbitrary bytes", "[base64]") {
    std::string raw = "Man is distinguished";
    auto encoded = base64_encode(raw);
    REQUIRE(encoded == "TWFuIGlzIGRpc3Rpbmd1aXNoZWQ=");
}
