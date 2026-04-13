#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "game/outgoing.h"
#include "session/room_registry.h"
#include "util/random.h"

namespace ip::session {

class Router {
public:
    using SendFn = std::function<void(int fd, const std::string& text)>;
    using CloseFn = std::function<void(int fd)>;

    Router(SendFn send, CloseFn close, uint64_t seed)
        : send_(std::move(send)), close_(std::move(close)), code_gen_(seed) {}

    void on_text(int fd, const std::string& text);
    void on_disconnect(int fd, uint64_t now_ms);
    void tick(uint64_t now_ms);

    RoomRegistry& rooms() { return rooms_; }

private:
    void handle_create_room(int fd, const nlohmann::json& msg);
    void handle_join_room(int fd, const nlohmann::json& msg);
    void handle_start_game(int fd, const nlohmann::json& msg);
    void handle_game_action(int fd, const nlohmann::json& msg);
    void handle_leave_room(int fd, const nlohmann::json& msg);
    void handle_close_room(int fd, const nlohmann::json& msg);
    void fanout_game_state(Room& room, const ip::game::OutgoingList& outs);

    void send_error(int fd, const std::string& code, const std::string& message);
    void send_json(int fd, const nlohmann::json& j);

    // fd -> (room_code, nickname) so we know where to route / clean up.
    struct FdOwner { std::string room_code; std::string nickname; };
    std::unordered_map<int, FdOwner> fd_owner_;

    SendFn send_;
    CloseFn close_;
    RoomRegistry rooms_;
    ip::util::RoomCodeGen code_gen_;
    // (room_code:nickname) -> disconnected_at_ms
    std::unordered_map<std::string, uint64_t> grace_;
};

} // namespace ip::session
