#include "session/router.h"

#include "game/indian_poker/indian_poker.h"

#include <memory>
#include <random>

namespace ip::session {

using nlohmann::json;

void Router::send_json(int fd, const json& j) {
    send_(fd, j.dump());
}

void Router::send_error(int fd, const std::string& code, const std::string& message) {
    send_json(fd, {{"type", "error"}, {"code", code}, {"message", message}});
}

static json lobby_update(const Room& r) {
    json players = json::array();
    for (const auto& p : r.players()) players.push_back(p->nickname());
    return {
        {"type", "lobby_update"},
        {"room_code", r.code()},
        {"players", players},
        {"host", r.host_nickname()},
        {"max_players", r.max_players()},
    };
}

void Router::on_text(int fd, const std::string& text) {
    // Clean up closed rooms from previous tick.
    std::vector<std::string> to_erase;
    for (const auto& [code, r] : rooms_.all()) {
        if (r->state() == RoomState::Closed) to_erase.push_back(code);
    }
    for (const auto& code : to_erase) rooms_.erase(code);

    json msg;
    try {
        msg = json::parse(text);
    } catch (const std::exception&) {
        send_error(fd, "MALFORMED_MESSAGE", "JSON parse error");
        return;
    }
    if (!msg.is_object() || !msg.contains("type") || !msg["type"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "missing type");
        return;
    }
    const std::string type = msg["type"];
    if (type == "create_room") handle_create_room(fd, msg);
    else if (type == "join_room") handle_join_room(fd, msg);
    else if (type == "start_game") handle_start_game(fd, msg);
    else if (type == "action" || type == "ready_next_hand") handle_game_action(fd, msg);
    else send_error(fd, "MALFORMED_MESSAGE", "unknown type: " + type);
}

void Router::handle_create_room(int fd, const json& msg) {
    if (!msg.contains("nickname") || !msg["nickname"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "nickname required");
        return;
    }
    const std::string nickname = msg["nickname"];
    if (!validate_nickname(nickname)) {
        send_error(fd, "NICKNAME_INVALID", "nickname failed validation");
        return;
    }
    int max_players = 4;
    if (msg.contains("max_players") && msg["max_players"].is_number_integer()) {
        int v = msg["max_players"].get<int>();
        if (v >= 2 && v <= 4) max_players = v;
    }

    // Generate a unique code.
    std::string code;
    for (int attempt = 0; attempt < 20; ++attempt) {
        code = code_gen_.generate();
        if (!rooms_.find(code)) break;
    }
    Room* room = rooms_.create(code, max_players);
    if (!room) {
        send_error(fd, "INTERNAL_ERROR", "could not allocate room code");
        return;
    }
    auto player = std::make_unique<Player>(nickname, 0);
    player->bind_fd(fd);
    room->add_player(std::move(player));

    fd_owner_[fd] = FdOwner{code, nickname};

    send_json(fd, {
        {"type", "room_created"},
        {"room_code", code},
        {"you", nickname},
        {"host", nickname},
    });
    for (const auto& p : room->players()) {
        if (p->connected()) send_json(p->fd(), lobby_update(*room));
    }
}

void Router::handle_join_room(int fd, const json& msg) {
    if (!msg.contains("room_code") || !msg["room_code"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "room_code required");
        return;
    }
    if (!msg.contains("nickname") || !msg["nickname"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "nickname required");
        return;
    }
    const std::string code = msg["room_code"];
    const std::string nickname = msg["nickname"];

    Room* room = rooms_.find(code);
    if (!room) { send_error(fd, "ROOM_NOT_FOUND", "no room with that code"); return; }

    if (auto* existing = room->find_player(nickname)) {
        if (existing->connected()) {
            send_error(fd, "NICKNAME_TAKEN", "nickname is in use");
            return;
        }
        // Reconnect path.
        existing->bind_fd(fd);
        fd_owner_[fd] = FdOwner{code, nickname};
        grace_.erase(code + ":" + nickname);
        send_json(fd, {
            {"type", "joined_room"},
            {"room_code", code},
            {"you", nickname},
            {"reconnect", true},
        });
        for (const auto& p : room->players()) {
            if (p->connected() && p->fd() != fd) {
                send_json(p->fd(), {
                    {"type", "player_reconnected"},
                    {"nickname", nickname},
                });
            }
        }
        return;
    }

    if (room->state() != RoomState::Open) {
        send_error(fd, "ROOM_IN_PROGRESS", "game already started");
        return;
    }
    if (static_cast<int>(room->players().size()) >= room->max_players()) {
        send_error(fd, "ROOM_FULL", "room is full");
        return;
    }
    if (!validate_nickname(nickname)) {
        send_error(fd, "NICKNAME_INVALID", "nickname failed validation");
        return;
    }

    auto player = std::make_unique<Player>(nickname, static_cast<int>(room->players().size()));
    player->bind_fd(fd);
    room->add_player(std::move(player));
    fd_owner_[fd] = FdOwner{code, nickname};

    send_json(fd, {
        {"type", "joined_room"},
        {"room_code", code},
        {"you", nickname},
        {"reconnect", false},
    });
    for (const auto& p : room->players()) {
        if (p->connected()) send_json(p->fd(), lobby_update(*room));
    }
}

void Router::on_disconnect(int fd, uint64_t now_ms) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) return;
    const std::string code = it->second.room_code;
    const std::string nickname = it->second.nickname;
    fd_owner_.erase(it);

    Room* room = rooms_.find(code);
    if (!room) return;
    Player* player = room->find_player(nickname);
    if (!player) return;

    player->mark_disconnected(now_ms);
    grace_[code + ":" + nickname] = now_ms;

    for (const auto& p : room->players()) {
        if (p->connected()) {
            send_json(p->fd(), {
                {"type", "player_disconnected"},
                {"nickname", nickname},
                {"grace_period_ms", 15000},
            });
        }
    }
}

static ip::game::indian_poker::Dealer make_random_dealer(uint64_t seed) {
    auto rng = std::make_shared<std::mt19937_64>(seed);
    return [rng](int n) {
        std::vector<int> deck = {1,2,3,4,5,6,7,8,9,10};
        std::shuffle(deck.begin(), deck.end(), *rng);
        return std::vector<int>(deck.begin(), deck.begin() + n);
    };
}

void Router::handle_start_game(int fd, const nlohmann::json& /*msg*/) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) { send_error(fd, "ROOM_NOT_FOUND", "not in a room"); return; }
    Room* room = rooms_.find(it->second.room_code);
    if (!room) { send_error(fd, "ROOM_NOT_FOUND", "room gone"); return; }
    if (room->host_nickname() != it->second.nickname) {
        send_error(fd, "NOT_HOST", "only host can start");
        return;
    }
    if (room->players().size() < 2) {
        send_error(fd, "NOT_ENOUGH_PLAYERS", "need at least 2 players");
        return;
    }
    if (room->state() != RoomState::Open) {
        send_error(fd, "ROOM_IN_PROGRESS", "already started");
        return;
    }

    auto game = std::make_unique<ip::game::indian_poker::IndianPoker>(make_random_dealer(42));
    std::vector<std::string> nicks;
    for (const auto& p : room->players()) nicks.push_back(p->nickname());

    auto outs = game->start(nicks);
    room->set_state(RoomState::Locked);
    room->set_game(std::move(game));
    fanout_game_state(*room, outs);
    if (room->game() && room->game()->finished()) {
        room->set_state(RoomState::Closed);
    }
}

void Router::handle_game_action(int fd, const nlohmann::json& msg) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) { send_error(fd, "ROOM_NOT_FOUND", "not in a room"); return; }
    Room* room = rooms_.find(it->second.room_code);
    if (!room || !room->game()) { send_error(fd, "INVALID_ACTION", "no game"); return; }
    auto outs = room->game()->handle_action(it->second.nickname, msg);
    fanout_game_state(*room, outs);
    if (room->game() && room->game()->finished()) {
        room->set_state(RoomState::Closed);
    }
}

void Router::fanout_game_state(Room& room, const ip::game::OutgoingList& outs) {
    for (const auto& o : outs) {
        if (std::holds_alternative<ip::game::Private>(o)) {
            const auto& pv = std::get<ip::game::Private>(o);
            const auto* player = room.find_player(pv.nickname);
            if (player && player->connected()) send_json(player->fd(), pv.payload);
            continue;
        }
        const auto& bc = std::get<ip::game::Broadcast>(o);
        if (bc.payload.value("type", "") == "__game_state_fanout") {
            if (!room.game()) continue;
            for (const auto& p : room.players()) {
                if (!p->connected()) continue;
                send_json(p->fd(), room.game()->snapshot_for(p->nickname()));
            }
            continue;
        }
        for (const auto& p : room.players()) {
            if (p->connected()) send_json(p->fd(), bc.payload);
        }
    }
}

void Router::tick(uint64_t now_ms) {
    constexpr uint64_t kGraceMs = 15000;
    std::vector<std::string> expired;
    for (const auto& [key, at] : grace_) {
        if (now_ms - at >= kGraceMs) expired.push_back(key);
    }
    for (const auto& key : expired) {
        grace_.erase(key);
        auto colon = key.find(':');
        std::string code = key.substr(0, colon);
        std::string nickname = key.substr(colon + 1);
        Room* room = rooms_.find(code);
        if (!room) continue;
        room->remove_player(nickname);
        for (const auto& p : room->players()) {
            if (p->connected()) {
                send_json(p->fd(), {
                    {"type", "player_left"},
                    {"nickname", nickname},
                    {"reason", "timeout"},
                });
                send_json(p->fd(), lobby_update(*room));
            }
        }
        if (room->players().empty()) rooms_.erase(code);
    }
}

} // namespace ip::session
