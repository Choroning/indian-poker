#pragma once
#include <string>
#include <vector>

namespace ip::game::indian_poker {

enum class PlayerStatus { Active, Folded, Eliminated, DisconnectedGrace };

struct PlayerState {
    std::string nickname;
    int chips = 100;
    int card = 0;              // 0 = not yet dealt
    int bet_this_round = 0;
    PlayerStatus status = PlayerStatus::Active;
    bool connected = true;
    bool ready_next_hand = false;
};

enum class Phase { Lobby, Dealing, Betting, Showdown, HandOver, GameOver };

struct State {
    Phase phase = Phase::Lobby;
    int hand_number = 0;
    std::vector<PlayerState> players;
    int pot = 0;
    int current_bet = 0;
    int min_raise = 5;
    int ante = 5;
    int current_turn_index = -1;
    int starting_player_index = 0;
};

} // namespace ip::game::indian_poker
