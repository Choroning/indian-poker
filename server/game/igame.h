#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "game/outgoing.h"

namespace ip::game {

class IGame {
public:
    virtual ~IGame() = default;

    virtual OutgoingList start(const std::vector<std::string>& nicknames) = 0;
    virtual OutgoingList handle_action(const std::string& nickname,
                                       const nlohmann::json& action) = 0;
    virtual OutgoingList on_player_gone(const std::string& nickname,
                                        const std::string& reason) = 0;
    virtual nlohmann::json snapshot_for(const std::string& nickname) const = 0;
    virtual bool finished() const = 0;
};

} // namespace ip::game
