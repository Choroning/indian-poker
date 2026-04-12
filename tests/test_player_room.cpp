#include <catch2/catch_test_macros.hpp>

#include "session/player.h"
#include "session/room.h"
#include "session/room_registry.h"

using namespace ip::session;

TEST_CASE("Player construction", "[player]") {
    Player p("alice", 1);
    REQUIRE(p.nickname() == "alice");
    REQUIRE(p.seat() == 1);
    REQUIRE(p.chips() == 0);
    REQUIRE(p.state() == ConnState::New);
    REQUIRE_FALSE(p.connected());
    REQUIRE(p.fd() == -1);
}

TEST_CASE("Player can be bound to an fd and unbound", "[player]") {
    Player p("alice", 0);
    p.bind_fd(7);
    REQUIRE(p.fd() == 7);
    REQUIRE(p.connected());

    p.mark_disconnected(1000);
    REQUIRE_FALSE(p.connected());
    REQUIRE(p.disconnected_at_ms() == 1000);

    p.bind_fd(9);
    REQUIRE(p.connected());
    REQUIRE(p.fd() == 9);
}

TEST_CASE("Room holds players and is OPEN initially", "[room]") {
    Room r("ABCDEF", 4);
    REQUIRE(r.code() == "ABCDEF");
    REQUIRE(r.max_players() == 4);
    REQUIRE(r.state() == RoomState::Open);
    REQUIRE(r.players().empty());

    r.add_player(std::make_unique<Player>("alice", 0));
    REQUIRE(r.players().size() == 1);
    REQUIRE(r.find_player("alice") != nullptr);
    REQUIRE(r.find_player("bob") == nullptr);
}

TEST_CASE("Room host is the first-added player by default", "[room]") {
    Room r("ABCDEF", 4);
    r.add_player(std::make_unique<Player>("alice", 0));
    r.add_player(std::make_unique<Player>("bob", 1));
    REQUIRE(r.host_nickname() == "alice");
}

TEST_CASE("Room host is promoted when current host leaves in OPEN", "[room]") {
    Room r("ABCDEF", 4);
    r.add_player(std::make_unique<Player>("alice", 0));
    r.add_player(std::make_unique<Player>("bob", 1));
    r.remove_player("alice");
    REQUIRE(r.host_nickname() == "bob");
}

TEST_CASE("RoomRegistry creates and finds rooms", "[registry]") {
    RoomRegistry reg;
    auto* room = reg.create("ABCDEF", 4);
    REQUIRE(room != nullptr);
    REQUIRE(reg.find("ABCDEF") == room);
    REQUIRE(reg.find("ZZZZZZ") == nullptr);
}

TEST_CASE("RoomRegistry rejects duplicate codes", "[registry]") {
    RoomRegistry reg;
    reg.create("ABCDEF", 4);
    REQUIRE(reg.create("ABCDEF", 4) == nullptr);
}
