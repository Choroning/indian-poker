#include "net/ws_handshake.h"

#include "util/base64.h"
#include "util/sha1.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace ip::net {

namespace {

constexpr const char* kMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string ltrim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char c) { return !std::isspace(c); }));
    return s;
}
std::string rtrim(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
    return s;
}
std::string trim(std::string s) { return ltrim(rtrim(std::move(s))); }

std::string tolower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    return tolower_copy(hay).find(tolower_copy(needle)) != std::string::npos;
}

} // namespace

HandshakeResult parse_handshake(std::string_view input) {
    HandshakeResult r;
    auto end = input.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        r.status = HandshakeStatus::NeedMore;
        return r;
    }
    r.consumed = end + 4;

    // Line-split headers.
    std::string head(input.substr(0, end));
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : head) {
            if (c == '\n') {
                if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                lines.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
    }
    if (lines.empty()) { r.status = HandshakeStatus::Invalid; return r; }

    // Request line must start with "GET "
    if (lines[0].rfind("GET ", 0) != 0) { r.status = HandshakeStatus::Invalid; return r; }

    std::string upgrade, connection, version;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        auto colon = lines[i].find(':');
        if (colon == std::string::npos) continue;
        std::string name = tolower_copy(trim(lines[i].substr(0, colon)));
        std::string value = trim(lines[i].substr(colon + 1));
        if (name == "upgrade") upgrade = value;
        else if (name == "connection") connection = value;
        else if (name == "sec-websocket-key") r.key = value;
        else if (name == "sec-websocket-version") version = value;
        else if (name == "origin") r.origin = value;
    }

    if (!contains_ci(upgrade, "websocket")) { r.status = HandshakeStatus::Invalid; return r; }
    if (!contains_ci(connection, "upgrade")) { r.status = HandshakeStatus::Invalid; return r; }
    if (version != "13") { r.status = HandshakeStatus::Invalid; return r; }
    if (r.key.empty()) { r.status = HandshakeStatus::Invalid; return r; }

    r.status = HandshakeStatus::Complete;
    return r;
}

std::string compute_accept(std::string_view key) {
    std::string concat;
    concat.reserve(key.size() + 36);
    concat.append(key.data(), key.size());
    concat.append(kMagicGuid);
    auto digest = ip::util::sha1(concat);
    return ip::util::base64_encode(digest);
}

std::string build_response(std::string_view key) {
    std::ostringstream os;
    os << "HTTP/1.1 101 Switching Protocols\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Accept: " << compute_accept(key) << "\r\n"
       << "\r\n";
    return os.str();
}

} // namespace ip::net
