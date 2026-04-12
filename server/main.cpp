#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

#include "net/event_loop.h"
#include "net/tcp_listener.h"
#include "net/ws_frame.h"
#include "net/ws_handshake.h"
#include "session/router.h"
#include "util/origin.h"

namespace {
struct ConnState {
    std::string pending;
    ip::net::FrameDecoder decoder;
    bool upgraded = false;
};

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

int main() {
    const char* port_env = std::getenv("IP_PORT");
    uint16_t port = static_cast<uint16_t>(port_env ? std::atoi(port_env) : 8080);

    static const ip::util::OriginAllowlist kOrigins({
        "http://localhost:3000",
        "http://localhost:8080",
    });

    try {
        ip::net::TCPListener listener(port);
        std::cerr << "[ip_server] listening on " << port << "\n";

        std::unordered_map<int, ConnState> states;

        std::unique_ptr<ip::net::EventLoop> loop_ptr;
        auto send_text = [&loop_ptr](int fd, const std::string& text) {
            auto bytes = ip::net::encode_text(text);
            loop_ptr->send_to(fd, std::string(bytes.begin(), bytes.end()));
        };
        auto close_fn = [&loop_ptr](int fd) { loop_ptr->close(fd); };

        ip::session::Router router(send_text, close_fn, /*seed=*/now_ms());

        loop_ptr = std::make_unique<ip::net::EventLoop>(
            std::move(listener),
            [&states](int fd) { states[fd] = {}; },
            [&](int fd, const std::string& chunk) {
                router.tick(now_ms());
                auto& s = states[fd];
                if (!s.upgraded) {
                    s.pending.append(chunk);
                    auto r = ip::net::parse_handshake(s.pending);
                    if (r.status == ip::net::HandshakeStatus::NeedMore) return;
                    if (r.status == ip::net::HandshakeStatus::Invalid) {
                        loop_ptr->send_to(fd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                        loop_ptr->close(fd);
                        return;
                    }
                    if (!kOrigins.allows(r.origin)) {
                        loop_ptr->send_to(fd, "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
                        loop_ptr->close(fd);
                        return;
                    }
                    loop_ptr->send_to(fd, ip::net::build_response(r.key));
                    s.upgraded = true;
                    std::string leftover = s.pending.substr(r.consumed);
                    s.pending.clear();
                    if (!leftover.empty()) {
                        auto dec = s.decoder.feed(leftover.data(), leftover.size());
                        if (dec.status == ip::net::DecodeStatus::MessageReady &&
                            dec.opcode == ip::net::Opcode::Text) {
                            router.on_text(fd, dec.message);
                        }
                    }
                    return;
                }
                auto dec = s.decoder.feed(chunk.data(), chunk.size());
                while (dec.status == ip::net::DecodeStatus::MessageReady) {
                    if (dec.opcode == ip::net::Opcode::Text) {
                        router.on_text(fd, dec.message);
                    } else if (dec.opcode == ip::net::Opcode::Ping) {
                        auto pong = ip::net::encode_control(ip::net::Opcode::Pong, dec.message);
                        loop_ptr->send_to(fd, std::string(pong.begin(), pong.end()));
                    } else if (dec.opcode == ip::net::Opcode::Close) {
                        auto cl = ip::net::encode_control(ip::net::Opcode::Close, "");
                        loop_ptr->send_to(fd, std::string(cl.begin(), cl.end()));
                        loop_ptr->close(fd);
                        return;
                    }
                    dec = s.decoder.feed(nullptr, 0);
                }
                if (dec.status == ip::net::DecodeStatus::Error) {
                    std::cerr << "[ip_server] decode error: " << dec.error_reason << "\n";
                    loop_ptr->close(fd);
                }
            },
            [&](int fd) {
                router.on_disconnect(fd, now_ms());
                router.tick(now_ms());
                states.erase(fd);
            }
        );
        loop_ptr->run();
    } catch (const std::exception& e) {
        std::cerr << "[ip_server] fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
