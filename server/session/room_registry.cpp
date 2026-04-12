#include "session/room_registry.h"

namespace ip::session {

Room* RoomRegistry::create(const std::string& code, int max_players) {
    if (rooms_.count(code)) return nullptr;
    auto r = std::make_unique<Room>(code, max_players);
    Room* raw = r.get();
    rooms_.emplace(code, std::move(r));
    return raw;
}

Room* RoomRegistry::find(const std::string& code) {
    auto it = rooms_.find(code);
    return it == rooms_.end() ? nullptr : it->second.get();
}

const Room* RoomRegistry::find(const std::string& code) const {
    auto it = rooms_.find(code);
    return it == rooms_.end() ? nullptr : it->second.get();
}

} // namespace ip::session
