#include "game/indian_poker/indian_poker.h"

#include <algorithm>
#include <stdexcept>

namespace ip::game::indian_poker {

using nlohmann::json;

IndianPoker::IndianPoker(Dealer dealer) : dealer_(std::move(dealer)) {}

static Private make_error(const std::string& nick, const std::string& code, const std::string& msg) {
    return Private{nick, {{"type", "error"}, {"code", code}, {"message", msg}}};
}

OutgoingList IndianPoker::start(const std::vector<std::string>& nicknames) {
    state_.players.clear();
    for (const auto& nick : nicknames) {
        PlayerState p;
        p.nickname = nick;
        p.chips = 100;
        state_.players.push_back(p);
    }
    state_.phase = Phase::Dealing;
    state_.hand_number = 0;
    state_.starting_player_index = 0;
    deal_new_hand();
    OutgoingList out;
    broadcast_state(out);
    return out;
}

void IndianPoker::deal_new_hand() {
    state_.hand_number += 1;
    state_.pot = 0;
    state_.current_bet = state_.ante;

    int active = 0;
    for (auto& p : state_.players) {
        if (p.status == PlayerStatus::Eliminated) continue;
        p.card = 0;
        p.bet_this_round = 0;
        p.ready_next_hand = false;
        if (p.status == PlayerStatus::Folded) p.status = PlayerStatus::Active;
        if (p.status == PlayerStatus::Active) ++active;
    }

    std::vector<int> cards = dealer_(active);
    std::size_t ci = 0;
    for (auto& p : state_.players) {
        if (p.status != PlayerStatus::Active) continue;
        p.card = cards[ci++];
        p.chips -= state_.ante;
        p.bet_this_round = state_.ante;
        state_.pot += state_.ante;
    }

    state_.current_turn_index = state_.starting_player_index;
    state_.phase = Phase::Betting;
}

void IndianPoker::advance_turn() {
    int n = static_cast<int>(state_.players.size());
    for (int step = 1; step <= n; ++step) {
        int idx = (state_.current_turn_index + step) % n;
        const auto& p = state_.players[idx];
        if (p.status == PlayerStatus::Active && p.chips >= 0) {
            state_.current_turn_index = idx;
            return;
        }
    }
    state_.current_turn_index = -1;
}

void IndianPoker::broadcast_state(OutgoingList& out) {
    json marker = {{"type", "__game_state_fanout"}};
    out.push_back(Broadcast{marker});
}

json IndianPoker::make_state_json(const std::string& viewer) const {
    json players = json::array();
    bool reveal_all = (state_.phase == Phase::Showdown || state_.phase == Phase::HandOver);
    for (const auto& p : state_.players) {
        json card = nullptr;
        if (p.card > 0) {
            if (p.status == PlayerStatus::Folded && !reveal_all) card = nullptr;
            else if (p.nickname == viewer && !reveal_all) card = nullptr;
            else card = p.card;
        }
        players.push_back({
            {"nickname", p.nickname},
            {"chips", p.chips},
            {"card", card},
            {"bet_this_round", p.bet_this_round},
            {"status", [&]() -> std::string {
                switch (p.status) {
                    case PlayerStatus::Active: return "active";
                    case PlayerStatus::Folded: return "folded";
                    case PlayerStatus::Eliminated: return "eliminated";
                    case PlayerStatus::DisconnectedGrace: return "disconnected_grace";
                }
                return "active";
            }()},
            {"connected", p.connected},
        });
    }
    std::string current_turn;
    if (state_.current_turn_index >= 0 &&
        state_.current_turn_index < static_cast<int>(state_.players.size())) {
        current_turn = state_.players[state_.current_turn_index].nickname;
    }
    return {
        {"type", "game_state"},
        {"phase", [&]() -> std::string {
            switch (state_.phase) {
                case Phase::Lobby: return "LOBBY";
                case Phase::Dealing: return "DEALING";
                case Phase::Betting: return "BETTING";
                case Phase::Showdown: return "SHOWDOWN";
                case Phase::HandOver: return "HAND_OVER";
                case Phase::GameOver: return "GAME_OVER";
            }
            return "LOBBY";
        }()},
        {"hand_number", state_.hand_number},
        {"players", players},
        {"pot", state_.pot},
        {"current_turn", current_turn},
        {"current_bet", state_.current_bet},
        {"min_raise", state_.min_raise},
        {"you", viewer},
    };
}

json IndianPoker::snapshot_for(const std::string& nickname) const {
    return make_state_json(nickname);
}

OutgoingList IndianPoker::handle_action(const std::string& nickname,
                                         const nlohmann::json& action) {
    OutgoingList out;

    // ready_next_hand handling.
    if (action.value("type", "") == "ready_next_hand") {
        if (state_.phase != Phase::HandOver) {
            out.push_back(make_error(nickname, "INVALID_ACTION", "not in hand_over"));
            return out;
        }
        for (auto& p : state_.players) {
            if (p.nickname == nickname) p.ready_next_hand = true;
        }
        bool all_ready = true;
        for (const auto& p : state_.players) {
            if (p.status == PlayerStatus::Eliminated) continue;
            if (!p.ready_next_hand) all_ready = false;
        }
        if (all_ready) start_next_hand_or_finish(out);
        else broadcast_state(out);
        return out;
    }

    if (state_.phase != Phase::Betting) {
        out.push_back(make_error(nickname, "INVALID_ACTION", "not in betting phase"));
        return out;
    }
    if (state_.current_turn_index < 0) {
        out.push_back(make_error(nickname, "INVALID_ACTION", "no active turn"));
        return out;
    }
    auto& turn_player = state_.players[state_.current_turn_index];
    if (turn_player.nickname != nickname) {
        out.push_back(make_error(nickname, "NOT_YOUR_TURN", "wait your turn"));
        return out;
    }
    if (!action.contains("action") || !action["action"].is_string()) {
        out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "missing action"));
        return out;
    }
    const std::string kind = action["action"];

    if (kind == "check") {
        if (turn_player.bet_this_round != state_.current_bet) {
            out.push_back(make_error(nickname, "INVALID_ACTION", "must raise or fold; cannot check"));
            return out;
        }
    } else if (kind == "raise") {
        if (!action.contains("amount") || !action["amount"].is_number_integer()) {
            out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "missing raise amount"));
            return out;
        }
        int amount = action["amount"];
        int diff = amount - turn_player.bet_this_round;
        bool allin = diff == turn_player.chips;
        if (!allin && amount < state_.current_bet + state_.min_raise) {
            out.push_back(make_error(nickname, "INVALID_RAISE_AMOUNT", "below minimum raise"));
            return out;
        }
        if (diff <= 0 || diff > turn_player.chips) {
            out.push_back(make_error(nickname, "INVALID_RAISE_AMOUNT", "insufficient chips"));
            return out;
        }
        turn_player.chips -= diff;
        turn_player.bet_this_round = amount;
        state_.pot += diff;
        state_.current_bet = amount;
    } else if (kind == "fold") {
        turn_player.status = PlayerStatus::Folded;
    } else {
        out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "unknown action"));
        return out;
    }

    advance_turn();
    maybe_end_betting(out);
    broadcast_state(out);
    return out;
}

void IndianPoker::maybe_end_betting(OutgoingList& out) {
    int active = 0;
    int all_matched = 0;
    int last_active_idx = -1;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        const auto& p = state_.players[i];
        if (p.status == PlayerStatus::Active) {
            ++active;
            last_active_idx = static_cast<int>(i);
            if (p.bet_this_round == state_.current_bet) ++all_matched;
        }
    }
    if (active == 1) {
        auto& winner = state_.players[last_active_idx];
        int pot_won = state_.pot;
        winner.chips += pot_won;
        state_.pot = 0;
        enter_hand_over(out, winner.nickname, pot_won);
        return;
    }
    if (all_matched == active) {
        do_showdown(out);
    }
}

void IndianPoker::do_showdown(OutgoingList& out) {
    state_.phase = Phase::Showdown;
    int best_card = -1;
    std::vector<int> winners;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        const auto& p = state_.players[i];
        if (p.status != PlayerStatus::Active) continue;
        if (p.card > best_card) {
            best_card = p.card;
            winners.clear();
            winners.push_back(static_cast<int>(i));
        } else if (p.card == best_card) {
            winners.push_back(static_cast<int>(i));
        }
    }
    int pot = state_.pot;
    state_.pot = 0;
    int share = pot / static_cast<int>(winners.size());
    int remainder = pot - share * static_cast<int>(winners.size());
    std::string winner_nick;
    for (std::size_t i = 0; i < winners.size(); ++i) {
        auto& w = state_.players[winners[i]];
        w.chips += share;
        if (i == 0) {
            w.chips += remainder;
            winner_nick = w.nickname;
        }
    }
    enter_hand_over(out, winner_nick, pot);
}

void IndianPoker::enter_hand_over(OutgoingList& out, const std::string& winner_nick, int pot_won) {
    state_.phase = Phase::HandOver;
    std::vector<std::string> eliminated;
    for (auto& p : state_.players) {
        if (p.chips <= 0 && p.status != PlayerStatus::Eliminated) {
            p.status = PlayerStatus::Eliminated;
            eliminated.push_back(p.nickname);
        }
    }
    json revealed = json::object();
    for (const auto& p : state_.players) {
        if (p.card > 0 && p.status != PlayerStatus::Eliminated) {
            revealed[p.nickname] = p.card;
        }
    }
    out.push_back(Broadcast{{
        {"type", "hand_over"},
        {"hand_number", state_.hand_number},
        {"winner", winner_nick},
        {"pot_won", pot_won},
        {"revealed", revealed},
        {"eliminated_after_this_hand", eliminated},
        {"next_hand_starts_at_ms", 5000},
    }});
}

void IndianPoker::start_next_hand_or_finish(OutgoingList& out) {
    int alive = 0;
    int last_idx = -1;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        if (state_.players[i].status != PlayerStatus::Eliminated) {
            ++alive;
            last_idx = static_cast<int>(i);
        }
    }
    if (alive <= 1) {
        state_.phase = Phase::GameOver;
        std::string winner = last_idx >= 0 ? state_.players[last_idx].nickname : std::string{};
        out.push_back(Broadcast{{{"type", "game_over"}, {"winner", winner}}});
        return;
    }
    int n = static_cast<int>(state_.players.size());
    for (int step = 1; step <= n; ++step) {
        int idx = (state_.starting_player_index + step) % n;
        if (state_.players[idx].status != PlayerStatus::Eliminated) {
            state_.starting_player_index = idx;
            break;
        }
    }
    deal_new_hand();
    broadcast_state(out);
}

OutgoingList IndianPoker::on_player_gone(const std::string& nickname,
                                         const std::string& /*reason*/) {
    OutgoingList out;
    // Mark the player as eliminated in game state.
    auto it = std::find_if(state_.players.begin(), state_.players.end(),
        [&](const PlayerState& p) { return p.nickname == nickname; });
    if (it == state_.players.end()) return out;
    if (it->status == PlayerStatus::Eliminated) return out;

    bool was_their_turn = (state_.current_turn_index >= 0 &&
                           state_.current_turn_index < static_cast<int>(state_.players.size()) &&
                           state_.players[state_.current_turn_index].nickname == nickname);
    it->status = PlayerStatus::Eliminated;

    // If in BETTING, advance turn if it was theirs, and check if hand should end.
    if (state_.phase == Phase::Betting) {
        if (was_their_turn) advance_turn();
        maybe_end_betting(out);
    }

    // Check for overall game over.
    int alive = 0;
    int last_idx = -1;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        if (state_.players[i].status != PlayerStatus::Eliminated) {
            ++alive;
            last_idx = static_cast<int>(i);
        }
    }
    if (alive <= 1 && state_.phase != Phase::GameOver) {
        state_.phase = Phase::GameOver;
        std::string winner = last_idx >= 0 ? state_.players[last_idx].nickname : std::string{};
        out.push_back(Broadcast{{{"type", "game_over"}, {"winner", winner}}});
        return out;
    }

    broadcast_state(out);
    return out;
}

} // namespace ip::game::indian_poker
