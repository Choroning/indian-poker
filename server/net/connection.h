#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace ip::net {

// Owns one client TCP socket. Non-blocking reads/writes with a small outgoing
// buffer. Will be extended in later milestones to carry WebSocket framing state.
class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    int fd() const { return fd_; }
    bool closed() const { return fd_ < 0; }

    // Reads up to 4 KiB into the internal read buffer. Returns the number of
    // bytes read (possibly 0 on a clean remote close, in which case the
    // connection is marked closed). Returns -1 on EAGAIN.
    ssize_t read_some();

    // Appends bytes to the outgoing buffer. Does NOT actually send; use flush().
    void enqueue(const char* data, std::size_t len);
    void enqueue(const std::string& s) { enqueue(s.data(), s.size()); }

    // Attempts to write queued data. Leaves anything not yet sent in the buffer.
    // Returns true if the buffer is now empty.
    bool flush();

    // Consumes and returns the bytes currently in the read buffer.
    std::string take_input();

    bool wants_write() const { return !out_.empty(); }

    void close();

private:
    int fd_ = -1;
    std::vector<char> out_;
    std::string in_;
};

} // namespace ip::net
