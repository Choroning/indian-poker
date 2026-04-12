#include <catch2/catch_test_macros.hpp>

#include "session/router.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ip::session;
using nlohmann::json;

namespace {

struct Harness {
    std::unordered_map<int, std::vector<json>> sent;
    std::vector<int> closed;
    Router router;

    Harness()
      : router(
            [this](int fd, const std::string& text) { sent[fd].push_back(json::parse(text)); },
            [this](int fd) { closed.push_back(fd); },
            42) {}

    json last(int fd, const std::string& type) {
        for (auto it = sent[fd].rbegin(); it != sent[fd].rend(); ++it) {
            if ((*it).value("type", "") == type) return *it;
        }
        return json{};
    }

    void text(int fd, const json& msg) { router.on_text(fd, msg.dump()); }
};

} // namespace

TEST_CASE("create + join yields lobby_update to both", "[join]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    auto created = h.last(1, "room_created");
    REQUIRE(created["room_code"].is_string());
    std::string code = created["room_code"];

    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    REQUIRE(h.last(1, "lobby_update")["players"].size() == 2);
    REQUIRE(h.last(2, "lobby_update")["players"].size() == 2);
}

TEST_CASE("reconnect restores same-nickname slot within grace", "[join][reconnect]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];

    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    // "bob" disconnects at t=1000.
    h.router.on_disconnect(2, /*now_ms=*/1000);

    // Within 15s window (t=1500), new fd joins with same nickname -> reconnect.
    h.text(3, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    auto joined = h.last(3, "joined_room");
    REQUIRE(joined["reconnect"] == true);
}

TEST_CASE("nickname taken rejects new joiner", "[join]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];
    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "alice"}});
    auto err = h.last(2, "error");
    REQUIRE(err["code"] == "NICKNAME_TAKEN");
}

TEST_CASE("grace timeout removes player", "[join][timeout]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];
    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    h.router.on_disconnect(2, /*now_ms=*/1000);

    // Advance the clock past 15s and tick.
    h.router.tick(/*now_ms=*/1000 + 15001);
    auto left = h.last(1, "player_left");
    REQUIRE(left["nickname"] == "bob");
    REQUIRE(left["reason"] == "timeout");
}
