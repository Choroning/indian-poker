#include <catch2/catch_test_macros.hpp>

#include "util/random.h"

#include <set>
#include <string>

using namespace ip::util;

TEST_CASE("room code is 6 chars from confusion-free alphabet", "[roomcode]") {
    RoomCodeGen gen(12345);
    for (int i = 0; i < 100; ++i) {
        auto code = gen.generate();
        REQUIRE(code.size() == 6);
        for (char c : code) {
            bool ok = (c >= 'A' && c <= 'Z' && c != 'I' && c != 'O') ||
                      (c >= '2' && c <= '9');
            REQUIRE(ok);
        }
    }
}

TEST_CASE("seeded generator is deterministic", "[roomcode]") {
    RoomCodeGen a(42), b(42);
    for (int i = 0; i < 10; ++i) REQUIRE(a.generate() == b.generate());
}
