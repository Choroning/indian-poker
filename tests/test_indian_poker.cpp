#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "game/indian_poker/indian_poker.h"

using namespace ip::game;
using namespace ip::game::indian_poker;
using nlohmann::json;

// Fixed dealer: returns the given sequence repeatedly.
static Dealer make_fixed_dealer(std::vector<int> cards) {
    return [cards = std::move(cards)](int /*n*/) { return cards; };
}

static IndianPoker make_2p_game(std::vector<int> cards = {5, 9}) {
    IndianPoker g(make_fixed_dealer(std::move(cards)));
    g.start({"alice", "bob"});
    return g;
}

TEST_CASE("start deals cards, deducts ante, enters BETTING", "[ip][setup]") {
    auto g = make_2p_game({5, 9});
    const auto& s = g.state();
    REQUIRE(s.phase == Phase::Betting);
    REQUIRE(s.hand_number == 1);
    REQUIRE(s.players.size() == 2);
    REQUIRE(s.players[0].card == 5);
    REQUIRE(s.players[1].card == 9);
    REQUIRE(s.players[0].chips == 95);
    REQUIRE(s.players[1].chips == 95);
    REQUIRE(s.pot == 10);
    REQUIRE(s.current_bet == 5);
    REQUIRE(s.current_turn_index == 0);
}

TEST_CASE("snapshot hides own card during BETTING", "[ip][snapshot]") {
    auto g = make_2p_game({5, 9});
    auto alice_view = g.snapshot_for("alice");
    const auto& players = alice_view["players"];
    REQUIRE(players[0]["card"].is_null());
    REQUIRE(players[1]["card"] == 9);
}

TEST_CASE("check on matched bet advances turn", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    g.handle_action("alice", {{"type", "action"}, {"action", "check"}});
    REQUIRE(g.state().current_turn_index == 1);
}

TEST_CASE("raise changes current_bet and turn", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    g.handle_action("alice", {{"type", "action"}, {"action", "raise"}, {"amount", 15}});
    const auto& s = g.state();
    REQUIRE(s.current_bet == 15);
    REQUIRE(s.players[0].bet_this_round == 15);
    REQUIRE(s.players[0].chips == 95 - 10);
    REQUIRE(s.pot == 10 + 10);
    REQUIRE(s.current_turn_index == 1);
}

TEST_CASE("raise below min_raise is rejected", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    auto out = g.handle_action("alice", {{"type", "action"}, {"action", "raise"}, {"amount", 8}});
    bool found = false;
    for (const auto& o : out) {
        if (std::holds_alternative<Private>(o)) {
            const auto& pj = std::get<Private>(o).payload;
            if (pj.value("type", "") == "error") {
                REQUIRE(pj["code"] == "INVALID_RAISE_AMOUNT");
                found = true;
            }
        }
    }
    REQUIRE(found);
    REQUIRE(g.state().current_bet == 5);
}

TEST_CASE("fold marks player folded and skips their turn", "[ip][action]") {
    IndianPoker g(make_fixed_dealer({4, 7, 9}));
    g.start({"alice", "bob", "carol"});
    g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
    REQUIRE(g.state().players[0].status == PlayerStatus::Folded);
    REQUIRE(g.state().current_turn_index == 1);
}

TEST_CASE("ready_next_hand from all players advances to a new hand", "[ip][handflow]") {
    IndianPoker g(make_fixed_dealer({5, 9}));
    g.start({"alice", "bob"});
    g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
    REQUIRE(g.state().phase == Phase::HandOver);
    g.handle_action("alice", {{"type", "ready_next_hand"}});
    g.handle_action("bob",   {{"type", "ready_next_hand"}});
    REQUIRE(g.state().phase == Phase::Betting);
    REQUIRE(g.state().hand_number == 2);
}

TEST_CASE("eliminating down to one player triggers GAME_OVER", "[ip][gameover]") {
    IndianPoker g(make_fixed_dealer({5, 9}));
    g.start({"alice", "bob"});
    for (int i = 0; i < 50; ++i) {
        if (g.state().phase == Phase::GameOver) break;
        if (g.state().phase == Phase::Betting) {
            const auto& s = g.state();
            std::string who = s.players[s.current_turn_index].nickname;
            if (who == "alice") {
                g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
            } else {
                g.handle_action(who, {{"type", "action"}, {"action", "check"}});
            }
        }
        if (g.state().phase == Phase::HandOver) {
            g.handle_action("alice", {{"type", "ready_next_hand"}});
            g.handle_action("bob",   {{"type", "ready_next_hand"}});
        }
    }
    REQUIRE(g.state().phase == Phase::GameOver);
}
