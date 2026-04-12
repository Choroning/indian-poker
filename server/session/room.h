#pragma once
#include <memory>
#include <string>
#include <vector>

#include "session/player.h"

namespace ip::session {

enum class RoomState { Open, Locked, Closed };

class Room {
public:
    Room(std::string code, int max_players)
        : code_(std::move(code)), max_players_(max_players) {}

    const std::string& code() const { return code_; }
    int max_players() const { return max_players_; }
    RoomState state() const { return state_; }
    void set_state(RoomState s) { state_ = s; }

    const std::vector<std::unique_ptr<Player>>& players() const { return players_; }

    // Returns nullptr if none.
    Player* find_player(const std::string& nickname);
    const Player* find_player(const std::string& nickname) const;

    // Ownership is taken. Returns a raw pointer to the added player.
    Player* add_player(std::unique_ptr<Player> p);

    // Removes by nickname. Returns true if something was removed.
    bool remove_player(const std::string& nickname);

    const std::string& host_nickname() const { return host_; }
    void set_host(const std::string& nickname) { host_ = nickname; }
    void promote_next_host_if_needed();

private:
    std::string code_;
    int max_players_;
    RoomState state_ = RoomState::Open;
    std::vector<std::unique_ptr<Player>> players_;
    std::string host_;
};

} // namespace ip::session
