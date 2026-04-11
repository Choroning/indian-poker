#include "net/connection.h"

#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <sys/socket.h>
#include <utility>

namespace ip::net {

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection() { close(); }

Connection::Connection(Connection&& other) noexcept
    : fd_(other.fd_), out_(std::move(other.out_)), in_(std::move(other.in_)) {
    other.fd_ = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        out_ = std::move(other.out_);
        in_ = std::move(other.in_);
        other.fd_ = -1;
    }
    return *this;
}

ssize_t Connection::read_some() {
    char buf[4096];
    ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n > 0) {
        in_.append(buf, static_cast<std::size_t>(n));
        return n;
    }
    if (n == 0) {
        close();
        return 0;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    close();
    return -1;
}

void Connection::enqueue(const char* data, std::size_t len) {
    out_.insert(out_.end(), data, data + len);
}

bool Connection::flush() {
    while (!out_.empty()) {
        ssize_t n = ::send(fd_, out_.data(), out_.size(), 0);
        if (n > 0) {
            out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;
        close();
        return false;
    }
    return true;
}

std::string Connection::take_input() {
    std::string s = std::move(in_);
    in_.clear();
    return s;
}

void Connection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace ip::net
