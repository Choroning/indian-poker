#pragma once
#include <string>
#include <unordered_set>
#include <vector>

namespace ip::util {

class OriginAllowlist {
public:
    OriginAllowlist(std::vector<std::string> allowed);
    bool allows(const std::string& origin) const;

private:
    std::unordered_set<std::string> allowed_;
};

} // namespace ip::util
