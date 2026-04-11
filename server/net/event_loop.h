#pragma once
#include <functional>
#include <memory>
#include <unordered_map>

#include "net/connection.h"
#include "net/tcp_listener.h"

namespace ip::net {

// Single-threaded select(2) event loop. Owns the listener and all connections.
// Callers provide callbacks for connect, data, and disconnect events.
class EventLoop {
public:
    using OnConnect = std::function<void(int fd)>;
    using OnData = std::function<void(int fd, const std::string& chunk)>;
    using OnDisconnect = std::function<void(int fd)>;

    EventLoop(TCPListener listener, OnConnect on_connect, OnData on_data, OnDisconnect on_disconnect);

    // Runs until stop() is called.
    void run();
    void stop() { running_ = false; }

    // Queue data to be sent on a connection. Safe to call from within callbacks.
    void send_to(int fd, const std::string& data);

    // Forcibly close a connection. Safe to call from within callbacks.
    void close(int fd);

private:
    void tick();

    TCPListener listener_;
    OnConnect on_connect_;
    OnData on_data_;
    OnDisconnect on_disconnect_;
    std::unordered_map<int, std::unique_ptr<Connection>> conns_;
    bool running_ = false;
};

} // namespace ip::net
