#include "util/origin.h"

namespace ip::util {

OriginAllowlist::OriginAllowlist(std::vector<std::string> allowed)
    : allowed_(allowed.begin(), allowed.end()) {}

bool OriginAllowlist::allows(const std::string& origin) const {
    if (origin.empty()) return false;
    return allowed_.count(origin) > 0;
}

} // namespace ip::util
