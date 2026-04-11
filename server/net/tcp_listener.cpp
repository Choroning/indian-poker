#include "net/tcp_listener.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ip::net {

namespace {
void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl F_GETFL failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
    }
}
} // namespace

TCPListener::TCPListener(uint16_t port) : port_(port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));

    int yes = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        ::close(fd_);
        throw std::runtime_error(std::string("setsockopt SO_REUSEADDR: ") + std::strerror(errno));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        throw std::runtime_error(std::string("bind: ") + std::strerror(errno));
    }

    if (::listen(fd_, 64) < 0) {
        ::close(fd_);
        throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
    }

    set_nonblocking(fd_);
}

TCPListener::~TCPListener() {
    if (fd_ >= 0) ::close(fd_);
}

TCPListener::TCPListener(TCPListener&& other) noexcept : fd_(other.fd_), port_(other.port_) {
    other.fd_ = -1;
    other.port_ = 0;
}

TCPListener& TCPListener::operator=(TCPListener&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        port_ = other.port_;
        other.fd_ = -1;
        other.port_ = 0;
    }
    return *this;
}

int TCPListener::accept_one() {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client), &len);
    if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        throw std::runtime_error(std::string("accept: ") + std::strerror(errno));
    }
    set_nonblocking(cfd);
    return cfd;
}

} // namespace ip::net
