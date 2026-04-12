#include "session/room.h"

#include <algorithm>

namespace ip::session {

Player* Room::find_player(const std::string& nickname) {
    for (auto& p : players_) if (p->nickname() == nickname) return p.get();
    return nullptr;
}

const Player* Room::find_player(const std::string& nickname) const {
    for (const auto& p : players_) if (p->nickname() == nickname) return p.get();
    return nullptr;
}

Player* Room::add_player(std::unique_ptr<Player> p) {
    Player* raw = p.get();
    players_.push_back(std::move(p));
    if (host_.empty()) host_ = raw->nickname();
    return raw;
}

bool Room::remove_player(const std::string& nickname) {
    auto it = std::find_if(players_.begin(), players_.end(),
        [&](const auto& p) { return p->nickname() == nickname; });
    if (it == players_.end()) return false;
    bool was_host = (nickname == host_);
    players_.erase(it);
    if (was_host) promote_next_host_if_needed();
    return true;
}

void Room::promote_next_host_if_needed() {
    if (host_.empty() || !find_player(host_)) {
        if (!players_.empty()) host_ = players_.front()->nickname();
        else host_.clear();
    }
}

} // namespace ip::session
