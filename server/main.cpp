#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include "net/event_loop.h"
#include "net/tcp_listener.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"

struct ConnState {
    std::string pending;                      // pre-handshake raw bytes
    ip::net::FrameDecoder decoder;            // post-handshake frame decoder
    bool upgraded = false;
};

int main() {
    const char* port_env = std::getenv("IP_PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::atoi(port_env) : 8080);

    try {
        ip::net::TCPListener listener(port);
        std::cerr << "[ip_server] listening on " << port << "\n";

        std::unordered_map<int, ConnState> states;

        ip::net::EventLoop loop(
            std::move(listener),
            [&states](int fd) { states[fd] = {}; },
            [&loop, &states](int fd, const std::string& chunk) {
                auto& s = states[fd];
                if (!s.upgraded) {
                    s.pending.append(chunk);
                    auto r = ip::net::parse_handshake(s.pending);
                    if (r.status == ip::net::HandshakeStatus::NeedMore) return;
                    if (r.status == ip::net::HandshakeStatus::Invalid) {
                        loop.send_to(fd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                        loop.close(fd);
                        return;
                    }
                    loop.send_to(fd, ip::net::build_response(r.key));
                    s.upgraded = true;
                    std::string leftover = s.pending.substr(r.consumed);
                    s.pending.clear();
                    if (!leftover.empty()) {
                        auto dec = s.decoder.feed(leftover.data(), leftover.size());
                        if (dec.status == ip::net::DecodeStatus::MessageReady) {
                            auto out = ip::net::encode_text(dec.message);
                            loop.send_to(fd, std::string(out.begin(), out.end()));
                        }
                    }
                    return;
                }
                auto dec = s.decoder.feed(chunk.data(), chunk.size());
                if (dec.status == ip::net::DecodeStatus::MessageReady) {
                    std::cerr << "[ip_server] text fd=" << fd << " msg=" << dec.message << "\n";
                    auto out = ip::net::encode_text(dec.message);
                    loop.send_to(fd, std::string(out.begin(), out.end()));
                } else if (dec.status == ip::net::DecodeStatus::Error) {
                    std::cerr << "[ip_server] decode error: " << dec.error_reason << "\n";
                    loop.close(fd);
                }
            },
            [&states](int fd) { states.erase(fd); }
        );

        loop.run();
    } catch (const std::exception& e) {
        std::cerr << "[ip_server] fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
