#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include "net/event_loop.h"
#include "net/tcp_listener.h"
#include "net/ws_handshake.h"

int main() {
    const char* port_env = std::getenv("IP_PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::atoi(port_env) : 8080);

    try {
        ip::net::TCPListener listener(port);
        std::cerr << "[ip_server] listening on " << port << "\n";

        // Per-fd buffered bytes not yet parsed.
        std::unordered_map<int, std::string> pending;

        ip::net::EventLoop loop(
            std::move(listener),
            [](int fd) { std::cerr << "[ip_server] connect fd=" << fd << "\n"; },
            [&loop, &pending](int fd, const std::string& chunk) {
                auto& buf = pending[fd];
                buf.append(chunk);
                // For this milestone, we only care about completing the handshake.
                auto r = ip::net::parse_handshake(buf);
                if (r.status == ip::net::HandshakeStatus::NeedMore) return;
                if (r.status == ip::net::HandshakeStatus::Invalid) {
                    std::cerr << "[ip_server] bad handshake fd=" << fd << "\n";
                    loop.send_to(fd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                    loop.close(fd);
                    pending.erase(fd);
                    return;
                }
                std::string response = ip::net::build_response(r.key);
                loop.send_to(fd, response);
                std::cerr << "[ip_server] handshake ok fd=" << fd << "\n";
                buf.erase(0, r.consumed); // Any trailing bytes would be WebSocket frames.
                // Those frame bytes are dropped for now (implemented in M3).
            },
            [&pending](int fd) {
                std::cerr << "[ip_server] disconnect fd=" << fd << "\n";
                pending.erase(fd);
            }
        );

        loop.run();
    } catch (const std::exception& e) {
        std::cerr << "[ip_server] fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
