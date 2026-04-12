#pragma once
#include <cstdint>
#include <string>

namespace ip::session {

bool validate_nickname(const std::string& s);

enum class ConnState { New, Connected, DisconnectedGrace };

class Player {
public:
    Player(std::string nickname, int seat)
        : nickname_(std::move(nickname)), seat_(seat) {}

    const std::string& nickname() const { return nickname_; }
    int seat() const { return seat_; }

    int chips() const { return chips_; }
    void set_chips(int c) { chips_ = c; }

    ConnState state() const { return state_; }
    bool connected() const { return state_ == ConnState::Connected; }
    int fd() const { return fd_; }

    void bind_fd(int f) {
        fd_ = f;
        state_ = ConnState::Connected;
        disconnected_at_ms_ = 0;
    }

    void mark_disconnected(uint64_t now_ms) {
        fd_ = -1;
        state_ = ConnState::DisconnectedGrace;
        disconnected_at_ms_ = now_ms;
    }

    uint64_t disconnected_at_ms() const { return disconnected_at_ms_; }

private:
    std::string nickname_;
    int seat_;
    int chips_ = 0;
    int fd_ = -1;
    ConnState state_ = ConnState::New;
    uint64_t disconnected_at_ms_ = 0;
};

} // namespace ip::session
