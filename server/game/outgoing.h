#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

namespace ip::game {

struct Broadcast { nlohmann::json payload; };
struct Private   { std::string nickname; nlohmann::json payload; };

using Outgoing = std::variant<Broadcast, Private>;
using OutgoingList = std::vector<Outgoing>;

} // namespace ip::game
