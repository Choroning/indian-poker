#pragma once
#include <cstdint>
#include <string>

namespace ip::net {

// Owns a listening TCP socket bound to a port on all interfaces.
// Movable but not copyable. RAII closes the fd on destruction.
class TCPListener {
public:
    // Throws std::runtime_error on any failure (socket/bind/listen/setsockopt).
    explicit TCPListener(uint16_t port);
    ~TCPListener();

    TCPListener(const TCPListener&) = delete;
    TCPListener& operator=(const TCPListener&) = delete;
    TCPListener(TCPListener&& other) noexcept;
    TCPListener& operator=(TCPListener&& other) noexcept;

    int fd() const { return fd_; }
    uint16_t port() const { return port_; }

    // Accept the next pending connection. Returns -1 if none available
    // (EAGAIN/EWOULDBLOCK). Throws on fatal errors.
    int accept_one();

private:
    int fd_ = -1;
    uint16_t port_ = 0;
};

} // namespace ip::net
