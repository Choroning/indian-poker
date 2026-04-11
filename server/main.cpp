#include <cstdlib>
#include <iostream>
#include <string>

#include "net/event_loop.h"
#include "net/tcp_listener.h"

int main() {
    const char* port_env = std::getenv("IP_PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::atoi(port_env) : 8080);

    try {
        ip::net::TCPListener listener(port);
        std::cerr << "[ip_server] listening on " << port << "\n";

        ip::net::EventLoop loop(
            std::move(listener),
            [](int fd) { std::cerr << "[ip_server] connect fd=" << fd << "\n"; },
            [&loop](int fd, const std::string& chunk) {
                // Echo back verbatim.
                loop.send_to(fd, chunk);
            },
            [](int fd) { std::cerr << "[ip_server] disconnect fd=" << fd << "\n"; }
        );

        loop.run();
    } catch (const std::exception& e) {
        std::cerr << "[ip_server] fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
