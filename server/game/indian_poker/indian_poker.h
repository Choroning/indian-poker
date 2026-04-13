#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "game/igame.h"
#include "game/indian_poker/state.h"

namespace ip::game::indian_poker {

// Deterministic deal function injected from outside for testability.
using Dealer = std::function<std::vector<int>(int num_players)>;

class IndianPoker : public IGame {
public:
    explicit IndianPoker(Dealer dealer);

    OutgoingList start(const std::vector<std::string>& nicknames) override;
    OutgoingList handle_action(const std::string& nickname,
                               const nlohmann::json& action) override;
    OutgoingList on_player_gone(const std::string& nickname,
                                const std::string& reason) override;
    nlohmann::json snapshot_for(const std::string& nickname) const override;
    bool finished() const override { return state_.phase == Phase::GameOver; }

    const State& state() const { return state_; }

    // Set the "current time" used for turn timers. Router must call this
    // before each start/handle_action/on_player_gone invocation.
    void set_now_ms(uint64_t now_ms) { now_ms_ = now_ms; }

private:
    Dealer dealer_;
    State state_;
    uint64_t now_ms_ = 0;

    void deal_new_hand();
    void advance_turn();
    void maybe_end_betting(OutgoingList& out);
    void do_showdown(OutgoingList& out);
    void enter_hand_over(OutgoingList& out, const std::string& winner_nick, int pot_won);
    void start_next_hand_or_finish(OutgoingList& out);
    void broadcast_state(OutgoingList& out);

    nlohmann::json make_state_json(const std::string& viewer) const;
};

} // namespace ip::game::indian_poker
