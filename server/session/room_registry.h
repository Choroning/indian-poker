#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "session/room.h"

namespace ip::session {

class RoomRegistry {
public:
    // Returns a pointer to the new Room, or nullptr if the code is already taken.
    Room* create(const std::string& code, int max_players);

    Room* find(const std::string& code);
    const Room* find(const std::string& code) const;

    void erase(const std::string& code) { rooms_.erase(code); }

    const std::unordered_map<std::string, std::unique_ptr<Room>>& all() const { return rooms_; }

private:
    std::unordered_map<std::string, std::unique_ptr<Room>> rooms_;
};

} // namespace ip::session
