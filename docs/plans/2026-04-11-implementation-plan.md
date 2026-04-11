# Indian Poker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a real-time multiplayer Indian Poker web game from scratch, with a C++ server speaking hand-written WebSocket over raw TCP sockets and a vanilla JS client, deployed via Vercel + Cloudflare Tunnel.

**Architecture:** Three strictly separated server layers — Network/Protocol (raw TCP + `select(2)` + WebSocket frame parser), Session (Player/Room/RoomRegistry), and Game Logic (pluggable `IGame` interface with `IndianPoker` as the first implementation). State synchronization uses full-state broadcast with per-player redaction. The client is a stateless view of `game_state` messages.

**Tech Stack:**
- **Server:** C++17, CMake, POSIX sockets, `select(2)`, OpenSSL (SHA-1 + base64 only), `nlohmann/json` (header-only), Catch2 v3 (tests)
- **Client:** Vanilla HTML/CSS/JS (no framework, no build step)
- **Deployment:** Vercel (frontend), Cloudflare Tunnel via `cloudflared` (backend)

**Reference spec:** See [`PROJECT.md`](../../PROJECT.md) for the complete design. This plan implements that spec milestone by milestone. Section references below (`§N.N`) refer to PROJECT.md.

---

## Global Conventions

- **Build dir:** `server/build/` (gitignored). Always build out-of-source.
- **Tests:** Catch2 v3 fetched via CMake `FetchContent`. Test executable: `server/build/tests/ip_tests`.
- **Test naming:** `test_<module>.cpp` under `tests/`. One file per server module.
- **Commit style:** Short English imperative subject, optional body, Co-Authored-By footer (this repo is 2026+, so footer is required).
- **TDD discipline:** For every non-trivial module, write the failing test first, verify it fails, implement the minimum to pass, verify it passes, commit.
- **Log destination:** `stderr` during development (no log framework; just `std::cerr`).
- **Port:** `8080` for the C++ server in development. Configurable via `IP_PORT` environment variable.

---

## File Structure Overview

The plan creates files in the order below. Each milestone adds a focused set of files rather than scattering changes across the tree.

```
indian-poker/
├── server/
│   ├── CMakeLists.txt                            # Phase 0
│   ├── cmake/
│   │   └── FetchDeps.cmake                       # Phase 0
│   ├── main.cpp                                  # Phase 1
│   ├── net/
│   │   ├── event_loop.{h,cpp}                    # Phase 1
│   │   ├── tcp_listener.{h,cpp}                  # Phase 1
│   │   ├── connection.{h,cpp}                    # Phase 1, extended in 2-4
│   │   ├── ws_handshake.{h,cpp}                  # Phase 2
│   │   └── ws_frame.{h,cpp}                      # Phase 3, extended in 4
│   ├── session/
│   │   ├── player.{h,cpp}                        # Phase 5
│   │   ├── room.{h,cpp}                          # Phase 5
│   │   ├── room_registry.{h,cpp}                 # Phase 5
│   │   └── router.{h,cpp}                        # Phase 5, extended in 6-10
│   ├── game/
│   │   ├── igame.h                               # Phase 8
│   │   ├── outgoing.h                            # Phase 8
│   │   └── indian_poker/
│   │       ├── indian_poker.{h,cpp}              # Phase 8
│   │       └── state.h                           # Phase 8
│   └── util/
│       ├── sha1.{h,cpp}                          # Phase 2
│       ├── base64.{h,cpp}                        # Phase 2
│       ├── json.h                                # Phase 5 (nlohmann wrapper alias)
│       ├── random.{h,cpp}                        # Phase 6, 8
│       └── origin.{h,cpp}                        # Phase 12
├── tests/
│   ├── CMakeLists.txt                            # Phase 0
│   ├── test_main.cpp                             # Phase 0
│   ├── test_event_loop.cpp                       # Phase 1
│   ├── test_ws_handshake.cpp                     # Phase 2
│   ├── test_sha1_base64.cpp                      # Phase 2
│   ├── test_ws_frame.cpp                         # Phase 3
│   ├── test_ws_frame_fragments.cpp               # Phase 4
│   ├── test_player_room.cpp                      # Phase 5
│   ├── test_room_code.cpp                        # Phase 6
│   ├── test_join_flow.cpp                        # Phase 6, 7
│   └── test_indian_poker.cpp                     # Phase 8
├── client/                                       # Phase 11
│   ├── index.html
│   ├── css/style.css
│   └── js/
│       ├── main.js
│       ├── ws_client.js
│       └── ui.js
├── deploy/                                       # Phase 13
│   ├── cloudflared-config.example.yml
│   └── vercel.json
└── docs/
    └── plans/
        └── 2026-04-11-implementation-plan.md     # this file
```

---

# Phase 0 — Project Scaffolding

**Outcome:** A runnable test executable that contains zero real code but proves the toolchain (CMake + Catch2 + OpenSSL + nlohmann/json) is wired up correctly.

### Task 0.1: Create server directory skeleton

**Files:**
- Create: `server/`, `server/net/`, `server/session/`, `server/game/`, `server/util/`, `server/cmake/`, `tests/`

- [ ] **Step 1: Make directories**

```bash
mkdir -p server/net server/session server/game/indian_poker server/util server/cmake tests
```

- [ ] **Step 2: Add placeholder files so git tracks the empty dirs**

```bash
touch server/net/.gitkeep server/session/.gitkeep server/game/indian_poker/.gitkeep server/util/.gitkeep tests/.gitkeep
```

- [ ] **Step 3: Commit**

```bash
git add server tests
git commit -m "$(cat <<'EOF'
Scaffold server and tests directory tree

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 0.2: Add dependency-fetching CMake helper

**Files:**
- Create: `server/cmake/FetchDeps.cmake`

- [ ] **Step 1: Write `server/cmake/FetchDeps.cmake`**

```cmake
# Fetches header-only dependencies via FetchContent.
# System dependency (OpenSSL) is located via find_package in the main CMakeLists.

include(FetchContent)

# nlohmann/json — single-header JSON library.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Catch2 v3 — test framework.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)
```

- [ ] **Step 2: Commit**

```bash
git add server/cmake/FetchDeps.cmake
git commit -m "$(cat <<'EOF'
Add FetchDeps.cmake for nlohmann/json and Catch2

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 0.3: Write top-level `server/CMakeLists.txt`

**Files:**
- Create: `server/CMakeLists.txt`

- [ ] **Step 1: Write `server/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(indian_poker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# System dependency.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Fetched dependencies.
include(cmake/FetchDeps.cmake)

# Library of all non-main sources, used by both the server binary and tests.
add_library(ip_core STATIC
    # files added per milestone
)
target_include_directories(ip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ip_core PUBLIC OpenSSL::Crypto nlohmann_json::nlohmann_json)

# Main server executable.
add_executable(ip_server main.cpp)
target_link_libraries(ip_server PRIVATE ip_core)

# Tests.
enable_testing()
add_subdirectory(../tests ${CMAKE_BINARY_DIR}/tests)
```

- [ ] **Step 2: Commit**

```bash
git add server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add top-level CMakeLists for server

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 0.4: Write the tests `CMakeLists.txt` and first smoke test

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_main.cpp`

- [ ] **Step 1: Write `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    # files added per milestone
)
target_link_libraries(ip_tests PRIVATE ip_core Catch2::Catch2WithMain)
target_include_directories(ip_tests PRIVATE ${CMAKE_SOURCE_DIR})

include(Catch)
catch_discover_tests(ip_tests)
```

- [ ] **Step 2: Write `tests/test_main.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("toolchain smoke test", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 3: Create a stub source file so `ip_core` is non-empty**

Create `server/util/version.cpp`:

```cpp
namespace ip {
const char* version() { return "0.1.0-dev"; }
}
```

- [ ] **Step 4: Add `util/version.cpp` to `ip_core` sources**

Edit `server/CMakeLists.txt`, changing the `add_library(ip_core STATIC ...)` block to:

```cmake
add_library(ip_core STATIC
    util/version.cpp
)
```

- [ ] **Step 5: Create a stub `server/main.cpp`**

```cpp
#include <iostream>
int main() {
    std::cerr << "ip_server stub — milestone M1 will replace this.\n";
    return 0;
}
```

- [ ] **Step 6: Configure and build**

```bash
cd server
cmake -B build -S .
cmake --build build -j
```

Expected: build succeeds, produces `server/build/ip_server` and `server/build/tests/ip_tests`.

- [ ] **Step 7: Run the smoke test**

```bash
cd server/build && ctest --output-on-failure
```

Expected output contains: `Passed` and `1 test from 1 test case`.

- [ ] **Step 8: Commit**

```bash
cd ../..
git add server/CMakeLists.txt server/main.cpp server/util/version.cpp tests/CMakeLists.txt tests/test_main.cpp
git commit -m "$(cat <<'EOF'
Wire up CMake, Catch2, and first smoke test

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

# Phase 1 — Milestone M1: TCP Echo Server with `select(2)`

**Outcome:** A single-threaded, multi-connection echo server. `telnet localhost 8080` in two windows works; anything typed in one window is echoed back. Internally this introduces `EventLoop`, `TCPListener`, and a minimal `Connection`.

### Task 1.1: `TCPListener` — bind and listen

**Files:**
- Create: `server/net/tcp_listener.h`
- Create: `server/net/tcp_listener.cpp`

- [ ] **Step 1: Write `server/net/tcp_listener.h`**

```cpp
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
```

- [ ] **Step 2: Write `server/net/tcp_listener.cpp`**

```cpp
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
```

- [ ] **Step 3: Add `net/tcp_listener.cpp` to `ip_core`**

Edit `server/CMakeLists.txt` sources list:

```cmake
add_library(ip_core STATIC
    util/version.cpp
    net/tcp_listener.cpp
)
```

- [ ] **Step 4: Build to confirm it compiles**

```bash
cmake --build server/build -j
```

Expected: clean build with no warnings.

- [ ] **Step 5: Commit**

```bash
git add server/net/tcp_listener.h server/net/tcp_listener.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add TCPListener wrapping socket/bind/listen

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 1.2: `Connection` — minimal fd owner

**Files:**
- Create: `server/net/connection.h`
- Create: `server/net/connection.cpp`

- [ ] **Step 1: Write `server/net/connection.h`**

```cpp
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
```

- [ ] **Step 2: Write `server/net/connection.cpp`**

```cpp
#include "net/connection.h"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
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
            out_.erase(out_.begin(), out_.begin() + n);
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
```

- [ ] **Step 3: Add to `ip_core` sources and build**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    net/tcp_listener.cpp
    net/connection.cpp
)
```

```bash
cmake --build server/build -j
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add server/net/connection.h server/net/connection.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add Connection wrapping a non-blocking client socket

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 1.3: `EventLoop` — `select(2)` driver

**Files:**
- Create: `server/net/event_loop.h`
- Create: `server/net/event_loop.cpp`

- [ ] **Step 1: Write `server/net/event_loop.h`**

```cpp
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
```

- [ ] **Step 2: Write `server/net/event_loop.cpp`**

```cpp
#include "net/event_loop.h"

#include <sys/select.h>
#include <sys/time.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace ip::net {

EventLoop::EventLoop(TCPListener listener, OnConnect on_connect, OnData on_data, OnDisconnect on_disconnect)
    : listener_(std::move(listener)),
      on_connect_(std::move(on_connect)),
      on_data_(std::move(on_data)),
      on_disconnect_(std::move(on_disconnect)) {}

void EventLoop::run() {
    running_ = true;
    while (running_) tick();
}

void EventLoop::tick() {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    int maxfd = listener_.fd();
    FD_SET(listener_.fd(), &rfds);

    for (auto& [fd, c] : conns_) {
        FD_SET(fd, &rfds);
        if (c->wants_write()) FD_SET(fd, &wfds);
        if (fd > maxfd) maxfd = fd;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100 ms tick

    int n = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
    if (n < 0) {
        if (errno == EINTR) return;
        throw std::runtime_error(std::string("select: ") + std::strerror(errno));
    }

    if (FD_ISSET(listener_.fd(), &rfds)) {
        while (true) {
            int cfd = listener_.accept_one();
            if (cfd < 0) break;
            conns_.emplace(cfd, std::make_unique<Connection>(cfd));
            if (on_connect_) on_connect_(cfd);
        }
    }

    std::vector<int> to_erase;
    for (auto& [fd, c] : conns_) {
        if (FD_ISSET(fd, &wfds)) c->flush();

        if (FD_ISSET(fd, &rfds)) {
            ssize_t r = c->read_some();
            if (r == 0) {
                if (on_disconnect_) on_disconnect_(fd);
                to_erase.push_back(fd);
                continue;
            }
            if (r > 0) {
                auto chunk = c->take_input();
                if (on_data_) on_data_(fd, chunk);
            }
        }

        if (c->closed()) {
            if (on_disconnect_) on_disconnect_(fd);
            to_erase.push_back(fd);
        }
    }

    for (int fd : to_erase) conns_.erase(fd);
}

void EventLoop::send_to(int fd, const std::string& data) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    it->second->enqueue(data);
    it->second->flush();
}

void EventLoop::close(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    it->second->close();
}

} // namespace ip::net
```

- [ ] **Step 3: Add to sources and build**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
)
```

```bash
cmake --build server/build -j
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add server/net/event_loop.h server/net/event_loop.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add select-based EventLoop managing listener and connections

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 1.4: Turn `main.cpp` into an echo server

**Files:**
- Modify: `server/main.cpp`

- [ ] **Step 1: Rewrite `server/main.cpp`**

```cpp
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
```

- [ ] **Step 2: Build**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Manual smoke test**

Terminal 1:
```bash
./server/build/ip_server
```

Terminal 2:
```bash
telnet localhost 8080
```
Type `hello<Enter>` — should see `hello` echoed. `Ctrl+]` then `quit` to exit. Server keeps running.

Terminal 3: open another `telnet localhost 8080` in parallel. Type `world` — both clients remain independent. Kill the server with `Ctrl+C` when done.

Expected: every line you type is echoed; multiple connections work simultaneously.

- [ ] **Step 4: Commit**

```bash
git add server/main.cpp
git commit -m "$(cat <<'EOF'
Wire main.cpp into an echo server (milestone M1)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M1.**

---

# Phase 2 — Milestone M2: HTTP Upgrade Handshake

**Outcome:** Browser `new WebSocket("ws://localhost:8080/")` successfully completes the handshake and stays connected (no HTTP 400, no immediate disconnect). Internally this introduces an HTTP request parser, SHA-1/base64 wrappers, and a per-connection state machine distinguishing "pre-handshake" from "post-handshake" bytes. Frame parsing is still stubbed — any subsequent bytes are simply dropped.

### Task 2.1: SHA-1 and base64 wrappers — TDD

**Files:**
- Create: `server/util/sha1.h`
- Create: `server/util/sha1.cpp`
- Create: `server/util/base64.h`
- Create: `server/util/base64.cpp`
- Create: `tests/test_sha1_base64.cpp`

- [ ] **Step 1: Write the failing test `tests/test_sha1_base64.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "util/base64.h"
#include "util/sha1.h"

using namespace ip::util;

TEST_CASE("sha1 matches known vector", "[sha1]") {
    // Known-good vector from RFC 6455 §1.3.
    // SHA1("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    //   raw bytes are then base64'd to: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
    const std::string input =
        "dGhlIHNhbXBsZSBub25jZQ=="
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    auto digest = sha1(input);
    REQUIRE(digest.size() == 20);

    auto encoded = base64_encode(digest);
    REQUIRE(encoded == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("base64 round trip on arbitrary bytes", "[base64]") {
    std::string raw = "Man is distinguished";
    auto encoded = base64_encode(raw);
    REQUIRE(encoded == "TWFuIGlzIGRpc3Rpbmd1aXNoZWQ=");
}
```

- [ ] **Step 2: Add the test file to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
)
```

- [ ] **Step 3: Run the test and verify it fails to link**

```bash
cmake --build server/build -j 2>&1 | tail -20
```

Expected: linker errors referencing `ip::util::sha1` and `ip::util::base64_encode`.

- [ ] **Step 4: Write `server/util/sha1.h`**

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ip::util {

// Returns the 20-byte SHA-1 digest of `input`.
std::array<uint8_t, 20> sha1(std::string_view input);

// Overload that accepts bytes as a string.
inline std::array<uint8_t, 20> sha1(const std::string& input) {
    return sha1(std::string_view{input});
}

} // namespace ip::util
```

- [ ] **Step 5: Write `server/util/sha1.cpp`**

```cpp
#include "util/sha1.h"

#include <openssl/sha.h>

namespace ip::util {

std::array<uint8_t, 20> sha1(std::string_view input) {
    std::array<uint8_t, 20> out{};
    ::SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), out.data());
    return out;
}

} // namespace ip::util
```

- [ ] **Step 6: Write `server/util/base64.h`**

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace ip::util {

std::string base64_encode(const uint8_t* data, std::size_t len);

inline std::string base64_encode(const std::string& s) {
    return base64_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

template <std::size_t N>
inline std::string base64_encode(const std::array<uint8_t, N>& a) {
    return base64_encode(a.data(), N);
}

} // namespace ip::util
```

- [ ] **Step 7: Write `server/util/base64.cpp`**

```cpp
#include "util/base64.h"

#include <openssl/evp.h>
#include <vector>

namespace ip::util {

std::string base64_encode(const uint8_t* data, std::size_t len) {
    // Output length: 4 * ceil(len / 3) plus null terminator.
    std::size_t out_len = 4 * ((len + 2) / 3);
    std::vector<uint8_t> buf(out_len + 1);
    int n = ::EVP_EncodeBlock(buf.data(), data, static_cast<int>(len));
    return std::string(reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(n));
}

} // namespace ip::util
```

- [ ] **Step 8: Add sources to `ip_core`**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
)
```

- [ ] **Step 9: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: 3 tests passing (smoke + sha1 + base64).

- [ ] **Step 10: Commit**

```bash
cd ../..
git add server/util/sha1.h server/util/sha1.cpp server/util/base64.h server/util/base64.cpp tests/test_sha1_base64.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add SHA-1 and base64 wrappers backed by OpenSSL

Verified against the RFC 6455 §1.3 handshake example.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2.2: `WebSocketHandshake` — parse request, build response — TDD

**Files:**
- Create: `server/net/ws_handshake.h`
- Create: `server/net/ws_handshake.cpp`
- Create: `tests/test_ws_handshake.cpp`

- [ ] **Step 1: Write the failing test `tests/test_ws_handshake.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "net/ws_handshake.h"

using namespace ip::net;

static constexpr const char* kGoodRequest =
    "GET /game HTTP/1.1\r\n"
    "Host: localhost:8080\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Origin: http://localhost:3000\r\n"
    "\r\n";

TEST_CASE("complete request is parsed and accepted", "[handshake]") {
    HandshakeResult r = parse_handshake(kGoodRequest);
    REQUIRE(r.status == HandshakeStatus::Complete);
    REQUIRE(r.origin == "http://localhost:3000");
    REQUIRE(r.key == "dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(r.consumed == std::string_view(kGoodRequest).size());
}

TEST_CASE("partial request yields NeedMore", "[handshake]") {
    std::string partial = "GET /game HTTP/1.1\r\nHost: localhost:8080\r\n";
    HandshakeResult r = parse_handshake(partial);
    REQUIRE(r.status == HandshakeStatus::NeedMore);
}

TEST_CASE("missing Upgrade header is rejected", "[handshake]") {
    std::string bad =
        "GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    HandshakeResult r = parse_handshake(bad);
    REQUIRE(r.status == HandshakeStatus::Invalid);
}

TEST_CASE("accept value matches RFC example", "[handshake]") {
    auto accept = compute_accept("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("build_response contains Sec-WebSocket-Accept", "[handshake]") {
    auto response = build_response("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(response.find("HTTP/1.1 101") != std::string::npos);
    REQUIRE(response.find("Upgrade: websocket") != std::string::npos);
    REQUIRE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    REQUIRE(response.find("\r\n\r\n") != std::string::npos);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
)
```

- [ ] **Step 3: Run and verify the new tests fail to link**

```bash
cmake --build server/build -j 2>&1 | tail -20
```

Expected: undefined references to `parse_handshake`, `compute_accept`, `build_response`.

- [ ] **Step 4: Write `server/net/ws_handshake.h`**

```cpp
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace ip::net {

enum class HandshakeStatus {
    NeedMore, // Request headers not yet complete (no \r\n\r\n seen).
    Complete, // Valid Upgrade request fully parsed.
    Invalid,  // Syntactically malformed or missing required headers.
};

struct HandshakeResult {
    HandshakeStatus status = HandshakeStatus::NeedMore;
    std::string key;     // Sec-WebSocket-Key (base64-encoded).
    std::string origin;  // Origin header (may be empty).
    std::size_t consumed = 0; // Bytes of the input that belonged to the request.
};

// Parses an in-flight HTTP Upgrade request. Idempotent; the caller may accumulate
// bytes and call repeatedly until the status becomes Complete or Invalid.
HandshakeResult parse_handshake(std::string_view input);

// Computes base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) per RFC 6455.
std::string compute_accept(std::string_view key);

// Builds a complete HTTP 101 response including the Sec-WebSocket-Accept header.
std::string build_response(std::string_view key);

} // namespace ip::net
```

- [ ] **Step 5: Write `server/net/ws_handshake.cpp`**

```cpp
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
```

- [ ] **Step 6: Add to `ip_core`**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
    net/ws_handshake.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: 8 tests pass.

- [ ] **Step 8: Commit**

```bash
cd ../..
git add server/net/ws_handshake.h server/net/ws_handshake.cpp tests/test_ws_handshake.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add WebSocket handshake parser and response builder

Parses an HTTP Upgrade request incrementally, validates required headers,
and builds the 101 Switching Protocols response with Sec-WebSocket-Accept.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2.3: Per-connection handshake state in `Connection`

**Files:**
- Modify: `server/net/connection.h`
- Modify: `server/net/connection.cpp`

- [ ] **Step 1: Extend `Connection` with a protocol phase**

Add to the top of `server/net/connection.h`, inside the class body after `bool closed() const`:

```cpp
    enum class Phase { Http, WebSocket };
    Phase phase() const { return phase_; }
    void set_phase(Phase p) { phase_ = p; }
```

Add to the private section:

```cpp
    Phase phase_ = Phase::Http;
```

- [ ] **Step 2: Build to confirm it still compiles**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Commit**

```bash
git add server/net/connection.h
git commit -m "$(cat <<'EOF'
Tag Connection with an Http/WebSocket phase

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2.4: Wire the handshake into `main.cpp`

**Files:**
- Modify: `server/main.cpp`

- [ ] **Step 1: Replace `main.cpp` with a handshake-performing echo server**

```cpp
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
```

- [ ] **Step 2: Build**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Manual browser test**

In one terminal:
```bash
./server/build/ip_server
```

Open a browser devtools console and run:
```js
const ws = new WebSocket("ws://localhost:8080/");
ws.onopen = () => console.log("OPEN");
ws.onclose = (e) => console.log("CLOSE", e.code, e.reason);
ws.onerror = (e) => console.log("ERROR");
```

Expected: the console prints `OPEN` and the connection stays open (no `CLOSE` immediately after). The server's stderr shows `handshake ok fd=...`.

- [ ] **Step 4: Commit**

```bash
git add server/main.cpp
git commit -m "$(cat <<'EOF'
Perform WebSocket handshake in the main event loop (milestone M2)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M2.**

---

# Phase 3 — Milestone M3: WebSocket Text Frames (No Fragmentation)

**Outcome:** The server decodes incoming single-frame text messages (with required client-side masking) and encodes outgoing single-frame text messages. An echo test from a browser round-trips short strings.

### Task 3.1: `WebSocketFrame` — decoder for single, masked, non-fragmented text frames — TDD

**Files:**
- Create: `server/net/ws_frame.h`
- Create: `server/net/ws_frame.cpp`
- Create: `tests/test_ws_frame.cpp`

- [ ] **Step 1: Write the failing decoder test `tests/test_ws_frame.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "net/ws_frame.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ip::net;

// Build a client-to-server text frame: FIN=1, opcode=1, MASK=1,
// payload length fits in 7 bits, masking key = {0x37,0xfa,0x21,0x3d}.
// Payload: "Hello"
static std::vector<uint8_t> hello_frame() {
    std::vector<uint8_t> f = {
        0x81,       // FIN=1, RSV=0, opcode=0x1 (text)
        0x85,       // MASK=1, len=5
        0x37, 0xfa, 0x21, 0x3d, // masking key
        // "Hello" XORed with the key bytes cycled.
        0x7f, 0x9f, 0x4d, 0x51, 0x58,
    };
    return f;
}

TEST_CASE("decode single masked text frame", "[ws_frame]") {
    auto bytes = hello_frame();
    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    REQUIRE(result.status == DecodeStatus::MessageReady);
    REQUIRE(result.message == "Hello");
    REQUIRE(result.consumed == bytes.size());
    REQUIRE(result.opcode == Opcode::Text);
}

TEST_CASE("decode rejects unmasked client frame", "[ws_frame]") {
    // Same as above but MASK bit cleared.
    std::vector<uint8_t> f = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(f.data()), f.size());
    REQUIRE(result.status == DecodeStatus::Error);
}

TEST_CASE("decode partial frame yields NeedMore then completes", "[ws_frame]") {
    auto bytes = hello_frame();
    FrameDecoder dec;
    auto part1 = dec.feed(reinterpret_cast<const char*>(bytes.data()), 3);
    REQUIRE(part1.status == DecodeStatus::NeedMore);
    auto part2 = dec.feed(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
    REQUIRE(part2.status == DecodeStatus::MessageReady);
    REQUIRE(part2.message == "Hello");
}

TEST_CASE("encode single-frame server text", "[ws_frame]") {
    auto bytes = encode_text("Hi");
    // FIN=1, opcode=1, MASK=0, len=2, then payload.
    REQUIRE(bytes.size() == 4);
    REQUIRE(static_cast<uint8_t>(bytes[0]) == 0x81);
    REQUIRE(static_cast<uint8_t>(bytes[1]) == 0x02);
    REQUIRE(bytes[2] == 'H');
    REQUIRE(bytes[3] == 'i');
}

TEST_CASE("decode 16-bit extended length", "[ws_frame]") {
    // 200-byte payload uses 2-byte extended length (opcode 126).
    std::string payload(200, 'a');
    uint8_t key[4] = {0x11, 0x22, 0x33, 0x44};

    std::vector<uint8_t> f;
    f.push_back(0x81);
    f.push_back(0xFE); // MASK=1, len=126
    f.push_back(0x00);
    f.push_back(0xC8); // 200
    f.insert(f.end(), key, key + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        f.push_back(static_cast<uint8_t>(payload[i] ^ key[i % 4]));
    }

    FrameDecoder dec;
    auto result = dec.feed(reinterpret_cast<const char*>(f.data()), f.size());
    REQUIRE(result.status == DecodeStatus::MessageReady);
    REQUIRE(result.message.size() == 200);
    REQUIRE(result.message.front() == 'a');
    REQUIRE(result.message.back() == 'a');
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
    test_ws_frame.cpp
)
```

- [ ] **Step 3: Run and verify the new tests fail to link**

```bash
cmake --build server/build -j 2>&1 | tail -20
```

Expected: undefined references to `FrameDecoder`, `encode_text`, etc.

- [ ] **Step 4: Write `server/net/ws_frame.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ip::net {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class DecodeStatus {
    NeedMore,     // not enough bytes yet
    MessageReady, // one complete logical message decoded
    Error,        // protocol error (unmasked client frame, bad opcode, etc.)
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    Opcode opcode = Opcode::Text;
    std::string message;
    std::size_t consumed = 0;
    std::string error_reason;
};

// Decodes a stream of WebSocket frames into messages. For milestone M3 only
// non-fragmented frames are supported; any continuation frame or FIN=0 returns
// an error. Milestone M4 extends this class to reassemble fragments.
class FrameDecoder {
public:
    DecodeResult feed(const char* data, std::size_t len);

private:
    std::vector<uint8_t> buf_;
};

// Encodes `payload` as a single unmasked server text frame.
std::vector<char> encode_text(const std::string& payload);

} // namespace ip::net
```

- [ ] **Step 5: Write `server/net/ws_frame.cpp`**

```cpp
#include "net/ws_frame.h"

#include <algorithm>
#include <cstring>

namespace ip::net {

namespace {

// Attempts to decode exactly one frame from `buf`. If not enough bytes, returns
// NeedMore. Fills `consumed`, `opcode`, and the unmasked payload into `out`.
DecodeStatus decode_one(const std::vector<uint8_t>& buf, DecodeResult& out) {
    if (buf.size() < 2) return DecodeStatus::NeedMore;

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t rsv = (b0 & 0x70);
    uint8_t opcode = b0 & 0x0F;
    bool mask = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if (rsv != 0) {
        out.error_reason = "reserved bits set";
        return DecodeStatus::Error;
    }
    if (!mask) {
        out.error_reason = "client frame not masked";
        return DecodeStatus::Error;
    }
    if (!fin) {
        // M3 does not support fragmentation yet; M4 will.
        out.error_reason = "fragmentation not supported in M3";
        return DecodeStatus::Error;
    }
    if (opcode != static_cast<uint8_t>(Opcode::Text)) {
        out.error_reason = "unsupported opcode in M3";
        return DecodeStatus::Error;
    }

    std::size_t header = 2;
    if (payload_len == 126) {
        if (buf.size() < header + 2) return DecodeStatus::NeedMore;
        payload_len = (uint64_t(buf[header]) << 8) | uint64_t(buf[header + 1]);
        header += 2;
    } else if (payload_len == 127) {
        if (buf.size() < header + 8) return DecodeStatus::NeedMore;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | uint64_t(buf[header + i]);
        }
        header += 8;
    }

    if (buf.size() < header + 4) return DecodeStatus::NeedMore;
    uint8_t key[4] = {buf[header], buf[header + 1], buf[header + 2], buf[header + 3]};
    header += 4;

    if (buf.size() < header + payload_len) return DecodeStatus::NeedMore;

    out.opcode = Opcode::Text;
    out.message.assign(payload_len, '\0');
    for (uint64_t i = 0; i < payload_len; ++i) {
        out.message[i] = static_cast<char>(buf[header + i] ^ key[i % 4]);
    }
    out.consumed = header + payload_len;
    return DecodeStatus::MessageReady;
}

} // namespace

DecodeResult FrameDecoder::feed(const char* data, std::size_t len) {
    buf_.insert(buf_.end(),
                reinterpret_cast<const uint8_t*>(data),
                reinterpret_cast<const uint8_t*>(data) + len);
    DecodeResult result;
    auto status = decode_one(buf_, result);
    result.status = status;
    if (status == DecodeStatus::MessageReady) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(result.consumed));
    }
    return result;
}

std::vector<char> encode_text(const std::string& payload) {
    std::vector<char> out;
    out.push_back(static_cast<char>(0x81)); // FIN=1, opcode=1
    std::size_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
        }
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

} // namespace ip::net
```

- [ ] **Step 6: Add to `ip_core`**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
    net/ws_handshake.cpp
    net/ws_frame.cpp
)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```bash
cd ../..
git add server/net/ws_frame.h server/net/ws_frame.cpp tests/test_ws_frame.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add WebSocket text frame decoder and encoder

M3 scope: single non-fragmented masked text frames client->server,
unmasked text frames server->client. Fragmentation and control frames
come in M4.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3.2: Echo text frames from `main.cpp`

**Files:**
- Modify: `server/main.cpp`

- [ ] **Step 1: Replace `main.cpp` with a frame-echoing server**

```cpp
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
```

- [ ] **Step 2: Build**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Manual browser test**

Start the server, open devtools console, run:
```js
const ws = new WebSocket("ws://localhost:8080/");
ws.onopen = () => ws.send("Hello");
ws.onmessage = (e) => console.log("RECV", e.data);
```

Expected: console prints `RECV Hello`. Server stderr prints `text fd=... msg=Hello`.

- [ ] **Step 4: Commit**

```bash
git add server/main.cpp
git commit -m "$(cat <<'EOF'
Echo WebSocket text frames end-to-end (milestone M3)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M3.**

---

# Phase 4 — Milestone M4: Fragmentation and Control Frames

**Outcome:** The server correctly reassembles fragmented data messages and handles interleaved control frames (ping/pong/close). It can also emit fragmented server messages to exercise the client-side reassembler.

### Task 4.1: Extend `FrameDecoder` to reassemble fragments and expose control frames — TDD

**Files:**
- Modify: `server/net/ws_frame.h`
- Modify: `server/net/ws_frame.cpp`
- Create: `tests/test_ws_frame_fragments.cpp`

- [ ] **Step 1: Write the failing test `tests/test_ws_frame_fragments.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "net/ws_frame.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace ip::net;

static std::vector<uint8_t> masked_frame(uint8_t opcode, bool fin, const std::string& payload) {
    std::vector<uint8_t> f;
    uint8_t b0 = static_cast<uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    f.push_back(b0);
    std::size_t n = payload.size();
    uint8_t b1;
    if (n < 126) {
        b1 = 0x80 | static_cast<uint8_t>(n);
        f.push_back(b1);
    } else if (n <= 0xFFFF) {
        b1 = 0x80 | 126;
        f.push_back(b1);
        f.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        f.push_back(static_cast<uint8_t>(n & 0xFF));
    } else {
        b1 = 0x80 | 127;
        f.push_back(b1);
        for (int i = 7; i >= 0; --i) f.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
    }
    uint8_t key[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    f.insert(f.end(), key, key + 4);
    for (std::size_t i = 0; i < n; ++i) {
        f.push_back(static_cast<uint8_t>(static_cast<uint8_t>(payload[i]) ^ key[i % 4]));
    }
    return f;
}

TEST_CASE("reassemble two-fragment text message", "[ws_frame][frag]") {
    auto f1 = masked_frame(0x1, false, "Hel");
    auto f2 = masked_frame(0x0, true,  "lo");

    FrameDecoder dec;
    auto r1 = dec.feed(reinterpret_cast<const char*>(f1.data()), f1.size());
    REQUIRE(r1.status == DecodeStatus::NeedMore);

    auto r2 = dec.feed(reinterpret_cast<const char*>(f2.data()), f2.size());
    REQUIRE(r2.status == DecodeStatus::MessageReady);
    REQUIRE(r2.opcode == Opcode::Text);
    REQUIRE(r2.message == "Hello");
}

TEST_CASE("ping frame interleaved between data fragments", "[ws_frame][control]") {
    auto f1 = masked_frame(0x1, false, "AB");
    auto ping = masked_frame(0x9, true, "x");
    auto f2 = masked_frame(0x0, true, "CD");

    FrameDecoder dec;
    auto r1 = dec.feed(reinterpret_cast<const char*>(f1.data()), f1.size());
    REQUIRE(r1.status == DecodeStatus::NeedMore);

    auto rp = dec.feed(reinterpret_cast<const char*>(ping.data()), ping.size());
    REQUIRE(rp.status == DecodeStatus::MessageReady);
    REQUIRE(rp.opcode == Opcode::Ping);
    REQUIRE(rp.message == "x");

    auto r2 = dec.feed(reinterpret_cast<const char*>(f2.data()), f2.size());
    REQUIRE(r2.status == DecodeStatus::MessageReady);
    REQUIRE(r2.opcode == Opcode::Text);
    REQUIRE(r2.message == "ABCD");
}

TEST_CASE("close frame is surfaced", "[ws_frame][control]") {
    auto close = masked_frame(0x8, true, "");
    FrameDecoder dec;
    auto r = dec.feed(reinterpret_cast<const char*>(close.data()), close.size());
    REQUIRE(r.status == DecodeStatus::MessageReady);
    REQUIRE(r.opcode == Opcode::Close);
}

TEST_CASE("encode pong and close", "[ws_frame][encode]") {
    auto pong = encode_control(Opcode::Pong, "hi");
    REQUIRE(pong.size() == 4);
    REQUIRE(static_cast<uint8_t>(pong[0]) == 0x8A);
    REQUIRE(static_cast<uint8_t>(pong[1]) == 0x02);

    auto cl = encode_control(Opcode::Close, "");
    REQUIRE(cl.size() == 2);
    REQUIRE(static_cast<uint8_t>(cl[0]) == 0x88);
    REQUIRE(static_cast<uint8_t>(cl[1]) == 0x00);
}

TEST_CASE("server can emit fragmented text for testing", "[ws_frame][encode]") {
    auto frames = encode_text_fragmented("HelloWorld", 4);
    // Expect 3 frames: "Hell" (FIN=0, opcode=1), "oWor" (FIN=0, opcode=0), "ld" (FIN=1, opcode=0)
    REQUIRE(frames.size() == 3);
    REQUIRE((static_cast<uint8_t>(frames[0][0]) & 0x0F) == 0x1);
    REQUIRE((static_cast<uint8_t>(frames[0][0]) & 0x80) == 0x00);
    REQUIRE((static_cast<uint8_t>(frames[1][0]) & 0x0F) == 0x0);
    REQUIRE((static_cast<uint8_t>(frames[1][0]) & 0x80) == 0x00);
    REQUIRE((static_cast<uint8_t>(frames[2][0]) & 0x0F) == 0x0);
    REQUIRE((static_cast<uint8_t>(frames[2][0]) & 0x80) == 0x80);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
    test_ws_frame.cpp
    test_ws_frame_fragments.cpp
)
```

- [ ] **Step 3: Run and verify new tests fail**

```bash
cmake --build server/build -j 2>&1 | tail -20
```

Expected: build breaks or tests fail (old decoder rejects fragmentation and control opcodes).

- [ ] **Step 4: Rewrite `server/net/ws_frame.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ip::net {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class DecodeStatus { NeedMore, MessageReady, Error };

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    Opcode opcode = Opcode::Text;
    std::string message;
    std::size_t consumed = 0;
    std::string error_reason;
};

// Reassembles fragmented data messages and exposes control frames separately.
// Control frames (ping/pong/close) are never fragmented per RFC; they may appear
// in between fragments of a data message.
class FrameDecoder {
public:
    DecodeResult feed(const char* data, std::size_t len);

private:
    std::vector<uint8_t> buf_;
    std::string pending_msg_;   // bytes of an in-progress fragmented message
    Opcode pending_opcode_ = Opcode::Text;
    bool in_fragment_ = false;
};

// Encode helpers: single frame, unmasked (server->client).
std::vector<char> encode_text(const std::string& payload);

// Encodes a control frame with the given opcode (Close, Ping, or Pong).
std::vector<char> encode_control(Opcode opcode, const std::string& payload);

// Splits `payload` into N server frames of up to `chunk_size` bytes each.
// The first frame carries opcode=Text with FIN=0, middle frames carry
// Continuation with FIN=0, final frame carries Continuation with FIN=1.
// Single-chunk payloads return a single frame with FIN=1, opcode=Text.
std::vector<std::vector<char>> encode_text_fragmented(const std::string& payload, std::size_t chunk_size);

} // namespace ip::net
```

- [ ] **Step 5: Rewrite `server/net/ws_frame.cpp`**

```cpp
#include "net/ws_frame.h"

#include <cstring>
#include <stdexcept>

namespace ip::net {

namespace {

struct RawFrame {
    bool fin;
    Opcode opcode;
    std::string payload;
    std::size_t consumed;
};

enum class RawStatus { NeedMore, Ready, Error };

RawStatus parse_one_frame(const std::vector<uint8_t>& buf, RawFrame& out, std::string& err) {
    if (buf.size() < 2) return RawStatus::NeedMore;

    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t rsv = (b0 & 0x70);
    uint8_t opcode = b0 & 0x0F;
    bool mask = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;

    if (rsv != 0) { err = "reserved bits set"; return RawStatus::Error; }
    if (!mask) { err = "client frame not masked"; return RawStatus::Error; }

    std::size_t header = 2;
    if (payload_len == 126) {
        if (buf.size() < header + 2) return RawStatus::NeedMore;
        payload_len = (uint64_t(buf[header]) << 8) | uint64_t(buf[header + 1]);
        header += 2;
    } else if (payload_len == 127) {
        if (buf.size() < header + 8) return RawStatus::NeedMore;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | uint64_t(buf[header + i]);
        header += 8;
    }

    // Control frames must be <= 125 bytes and FIN=1 per RFC.
    bool is_control = (opcode & 0x08) != 0;
    if (is_control) {
        if (payload_len > 125) { err = "oversized control frame"; return RawStatus::Error; }
        if (!fin) { err = "fragmented control frame"; return RawStatus::Error; }
    }

    if (buf.size() < header + 4) return RawStatus::NeedMore;
    uint8_t key[4] = {buf[header], buf[header + 1], buf[header + 2], buf[header + 3]};
    header += 4;

    if (buf.size() < header + payload_len) return RawStatus::NeedMore;

    out.fin = fin;
    out.opcode = static_cast<Opcode>(opcode);
    out.payload.assign(payload_len, '\0');
    for (uint64_t i = 0; i < payload_len; ++i) {
        out.payload[i] = static_cast<char>(buf[header + i] ^ key[i % 4]);
    }
    out.consumed = header + payload_len;
    return RawStatus::Ready;
}

} // namespace

DecodeResult FrameDecoder::feed(const char* data, std::size_t len) {
    buf_.insert(buf_.end(),
                reinterpret_cast<const uint8_t*>(data),
                reinterpret_cast<const uint8_t*>(data) + len);

    DecodeResult result;
    RawFrame frame;
    std::string err;
    auto status = parse_one_frame(buf_, frame, err);
    if (status == RawStatus::NeedMore) {
        result.status = DecodeStatus::NeedMore;
        return result;
    }
    if (status == RawStatus::Error) {
        result.status = DecodeStatus::Error;
        result.error_reason = err;
        return result;
    }

    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(frame.consumed));

    // Control frames stand alone.
    bool is_control = (static_cast<uint8_t>(frame.opcode) & 0x08) != 0;
    if (is_control) {
        result.status = DecodeStatus::MessageReady;
        result.opcode = frame.opcode;
        result.message = std::move(frame.payload);
        result.consumed = frame.consumed;
        return result;
    }

    // Data frame.
    if (frame.opcode == Opcode::Continuation) {
        if (!in_fragment_) {
            result.status = DecodeStatus::Error;
            result.error_reason = "continuation frame with no start";
            return result;
        }
        pending_msg_.append(frame.payload);
        if (frame.fin) {
            result.status = DecodeStatus::MessageReady;
            result.opcode = pending_opcode_;
            result.message = std::move(pending_msg_);
            pending_msg_.clear();
            in_fragment_ = false;
            return result;
        }
        result.status = DecodeStatus::NeedMore;
        return result;
    }

    // Start of a data message (opcode Text or Binary).
    if (in_fragment_) {
        result.status = DecodeStatus::Error;
        result.error_reason = "new data frame during fragmented message";
        return result;
    }
    if (frame.fin) {
        result.status = DecodeStatus::MessageReady;
        result.opcode = frame.opcode;
        result.message = std::move(frame.payload);
        return result;
    }
    in_fragment_ = true;
    pending_opcode_ = frame.opcode;
    pending_msg_ = std::move(frame.payload);
    result.status = DecodeStatus::NeedMore;
    return result;
}

static void write_header(std::vector<char>& out, uint8_t b0, std::size_t n) {
    out.push_back(static_cast<char>(b0));
    if (n < 126) {
        out.push_back(static_cast<char>(n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
    }
}

std::vector<char> encode_text(const std::string& payload) {
    std::vector<char> out;
    write_header(out, 0x81, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<char> encode_control(Opcode opcode, const std::string& payload) {
    if (payload.size() > 125) throw std::runtime_error("control payload > 125");
    std::vector<char> out;
    uint8_t b0 = 0x80 | static_cast<uint8_t>(opcode);
    out.push_back(static_cast<char>(b0));
    out.push_back(static_cast<char>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::vector<char>> encode_text_fragmented(const std::string& payload, std::size_t chunk_size) {
    if (chunk_size == 0) throw std::runtime_error("chunk_size must be > 0");
    std::vector<std::vector<char>> out;
    if (payload.size() <= chunk_size) {
        out.push_back(encode_text(payload));
        return out;
    }
    std::size_t offset = 0;
    bool first = true;
    while (offset < payload.size()) {
        std::size_t remaining = payload.size() - offset;
        std::size_t take = remaining < chunk_size ? remaining : chunk_size;
        bool last = (offset + take == payload.size());
        uint8_t b0 = static_cast<uint8_t>((last ? 0x80 : 0x00) |
                                          (first ? 0x1 : 0x0));
        std::vector<char> frame;
        write_header(frame, b0, take);
        frame.insert(frame.end(), payload.begin() + offset, payload.begin() + offset + take);
        out.push_back(std::move(frame));
        offset += take;
        first = false;
    }
    return out;
}

} // namespace ip::net
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: all M3 tests still pass AND all new fragment/control tests pass.

- [ ] **Step 7: Commit**

```bash
cd ../..
git add server/net/ws_frame.h server/net/ws_frame.cpp tests/test_ws_frame_fragments.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Support fragmented messages and control frames in FrameDecoder

Data messages can now span multiple frames with interleaved ping/pong/close
control frames, matching RFC 6455 §5.4. Server-side helpers added to encode
fragmented text for testing client reassembly.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4.2: Handle control frames in `main.cpp`

**Files:**
- Modify: `server/main.cpp`

- [ ] **Step 1: Extend the data-handling callback to reply to pings and honor closes**

Replace the frame-handling block in `main.cpp` with:

```cpp
                while (true) {
                    auto dec = s.decoder.feed(nullptr, 0);
                    // feed(nullptr, 0) lets the decoder check whether another
                    // full message is already buffered without adding new bytes.
                    if (dec.status == ip::net::DecodeStatus::MessageReady) {
                        if (dec.opcode == ip::net::Opcode::Text) {
                            auto out = ip::net::encode_text(dec.message);
                            loop.send_to(fd, std::string(out.begin(), out.end()));
                        } else if (dec.opcode == ip::net::Opcode::Ping) {
                            auto pong = ip::net::encode_control(ip::net::Opcode::Pong, dec.message);
                            loop.send_to(fd, std::string(pong.begin(), pong.end()));
                        } else if (dec.opcode == ip::net::Opcode::Close) {
                            auto cl = ip::net::encode_control(ip::net::Opcode::Close, "");
                            loop.send_to(fd, std::string(cl.begin(), cl.end()));
                            loop.close(fd);
                            return;
                        }
                        continue;
                    }
                    break;
                }
```

The actual chunk-driven call is still `s.decoder.feed(chunk.data(), chunk.size())` — the `while`/`feed(nullptr, 0)` loop above drains any additional fully-buffered messages after the first. (The feed function happily accepts a zero-length append.)

- [ ] **Step 2: Verify the whole `main.cpp` compiles**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Manual browser test**

In devtools:
```js
const ws = new WebSocket("ws://localhost:8080/");
ws.onopen = () => ws.send("A".repeat(1000));
ws.onmessage = (e) => console.log("len=", e.data.length);
```

Expected: `len= 1000`. Leave the socket open for 30+ seconds — the browser's idle ping should be answered by our pong and the socket should not close on its own.

- [ ] **Step 4: Commit**

```bash
git add server/main.cpp
git commit -m "$(cat <<'EOF'
Handle ping/pong/close control frames in main (milestone M4)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M4.**

---

# Phase 5 — Milestone M5: Session Layer Scaffolding

**Outcome:** `Player`, `Room`, and `RoomRegistry` classes exist with unit tests. No game logic yet; these are pure data structures and lookup helpers.

### Task 5.1: `Player` class — TDD

**Files:**
- Create: `server/session/player.h`
- Create: `server/session/player.cpp`
- Create: `tests/test_player_room.cpp`

- [ ] **Step 1: Write the failing test `tests/test_player_room.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "session/player.h"
#include "session/room.h"
#include "session/room_registry.h"

using namespace ip::session;

TEST_CASE("Player construction", "[player]") {
    Player p("alice", 1);
    REQUIRE(p.nickname() == "alice");
    REQUIRE(p.seat() == 1);
    REQUIRE(p.chips() == 0);
    REQUIRE(p.connected());
    REQUIRE(p.fd() == -1);
}

TEST_CASE("Player can be bound to an fd and unbound", "[player]") {
    Player p("alice", 0);
    p.bind_fd(7);
    REQUIRE(p.fd() == 7);
    REQUIRE(p.connected());

    p.mark_disconnected(1000);
    REQUIRE_FALSE(p.connected());
    REQUIRE(p.disconnected_at_ms() == 1000);

    p.bind_fd(9);
    REQUIRE(p.connected());
    REQUIRE(p.fd() == 9);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
    test_ws_frame.cpp
    test_ws_frame_fragments.cpp
    test_player_room.cpp
)
```

- [ ] **Step 3: Verify the build fails**

```bash
cmake --build server/build -j 2>&1 | tail -10
```

Expected: includes cannot find `session/player.h`.

- [ ] **Step 4: Write `server/session/player.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace ip::session {

class Player {
public:
    Player(std::string nickname, int seat)
        : nickname_(std::move(nickname)), seat_(seat) {}

    const std::string& nickname() const { return nickname_; }
    int seat() const { return seat_; }

    int chips() const { return chips_; }
    void set_chips(int c) { chips_ = c; }

    bool connected() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void bind_fd(int f) { fd_ = f; disconnected_at_ms_ = 0; }

    void mark_disconnected(uint64_t now_ms) {
        fd_ = -1;
        disconnected_at_ms_ = now_ms;
    }
    uint64_t disconnected_at_ms() const { return disconnected_at_ms_; }

private:
    std::string nickname_;
    int seat_;
    int chips_ = 0;
    int fd_ = 0;          // will be set via bind_fd
    uint64_t disconnected_at_ms_ = 0;
};

} // namespace ip::session
```

- [ ] **Step 5: Write `server/session/player.cpp`**

```cpp
// Intentionally empty — Player is header-only.
#include "session/player.h"
```

(Keeping a .cpp file means the source list can stay uniform; CMake will just produce no output for it.)

- [ ] **Step 6: Add to `ip_core`**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
    net/ws_handshake.cpp
    net/ws_frame.cpp
    session/player.cpp
)
```

- [ ] **Step 7: Fix the Player default-constructed fd**

The test expects a fresh Player to have `fd() == -1` and `connected() == true`. That's inconsistent: a `-1` fd should be "not connected". Change the test and the class to make the initial state "no fd bound yet, but also not counted as disconnected". Simplest fix: introduce a `ConnectionState` enum.

Replace `server/session/player.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace ip::session {

enum class ConnState { New, Connected, DisconnectedGrace };

class Player {
public:
    Player(std::string nickname, int seat)
        : nickname_(std::move(nickname)), seat_(seat) {}

    const std::string& nickname() const { return nickname_; }
    int seat() const { return seat_; }

    int chips() const { return chips_; }
    void set_chips(int c) { chips_ = c; }

    ConnState state() const { return state_; }
    bool connected() const { return state_ == ConnState::Connected; }
    int fd() const { return fd_; }

    void bind_fd(int f) {
        fd_ = f;
        state_ = ConnState::Connected;
        disconnected_at_ms_ = 0;
    }

    void mark_disconnected(uint64_t now_ms) {
        fd_ = -1;
        state_ = ConnState::DisconnectedGrace;
        disconnected_at_ms_ = now_ms;
    }

    uint64_t disconnected_at_ms() const { return disconnected_at_ms_; }

private:
    std::string nickname_;
    int seat_;
    int chips_ = 0;
    int fd_ = -1;
    ConnState state_ = ConnState::New;
    uint64_t disconnected_at_ms_ = 0;
};

} // namespace ip::session
```

Update the test so the expectation matches reality:

```cpp
TEST_CASE("Player construction", "[player]") {
    Player p("alice", 1);
    REQUIRE(p.nickname() == "alice");
    REQUIRE(p.seat() == 1);
    REQUIRE(p.chips() == 0);
    REQUIRE(p.state() == ConnState::New);
    REQUIRE_FALSE(p.connected());
    REQUIRE(p.fd() == -1);
}
```

- [ ] **Step 8: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 9: Commit**

```bash
cd ../..
git add server/session/player.h server/session/player.cpp tests/test_player_room.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add Player model with New/Connected/DisconnectedGrace states

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5.2: `Room` class

**Files:**
- Create: `server/session/room.h`
- Create: `server/session/room.cpp`

- [ ] **Step 1: Append to `tests/test_player_room.cpp`**

```cpp
TEST_CASE("Room holds players and is OPEN initially", "[room]") {
    Room r("ABCDEF", 4);
    REQUIRE(r.code() == "ABCDEF");
    REQUIRE(r.max_players() == 4);
    REQUIRE(r.state() == RoomState::Open);
    REQUIRE(r.players().empty());

    r.add_player(std::make_unique<Player>("alice", 0));
    REQUIRE(r.players().size() == 1);
    REQUIRE(r.find_player("alice") != nullptr);
    REQUIRE(r.find_player("bob") == nullptr);
}

TEST_CASE("Room host is the first-added player by default", "[room]") {
    Room r("ABCDEF", 4);
    r.add_player(std::make_unique<Player>("alice", 0));
    r.add_player(std::make_unique<Player>("bob", 1));
    REQUIRE(r.host_nickname() == "alice");
}

TEST_CASE("Room host is promoted when current host leaves in OPEN", "[room]") {
    Room r("ABCDEF", 4);
    r.add_player(std::make_unique<Player>("alice", 0));
    r.add_player(std::make_unique<Player>("bob", 1));
    r.remove_player("alice");
    REQUIRE(r.host_nickname() == "bob");
}
```

- [ ] **Step 2: Write `server/session/room.h`**

```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "session/player.h"

namespace ip::session {

enum class RoomState { Open, Locked, Closed };

class Room {
public:
    Room(std::string code, int max_players)
        : code_(std::move(code)), max_players_(max_players) {}

    const std::string& code() const { return code_; }
    int max_players() const { return max_players_; }
    RoomState state() const { return state_; }
    void set_state(RoomState s) { state_ = s; }

    const std::vector<std::unique_ptr<Player>>& players() const { return players_; }

    // Returns nullptr if none.
    Player* find_player(const std::string& nickname);
    const Player* find_player(const std::string& nickname) const;

    // Ownership is taken. Returns a raw pointer to the added player.
    Player* add_player(std::unique_ptr<Player> p);

    // Removes by nickname. Returns true if something was removed.
    bool remove_player(const std::string& nickname);

    const std::string& host_nickname() const { return host_; }
    void set_host(const std::string& nickname) { host_ = nickname; }
    void promote_next_host_if_needed();

private:
    std::string code_;
    int max_players_;
    RoomState state_ = RoomState::Open;
    std::vector<std::unique_ptr<Player>> players_;
    std::string host_;
};

} // namespace ip::session
```

- [ ] **Step 3: Write `server/session/room.cpp`**

```cpp
#include "session/room.h"

#include <algorithm>

namespace ip::session {

Player* Room::find_player(const std::string& nickname) {
    for (auto& p : players_) if (p->nickname() == nickname) return p.get();
    return nullptr;
}

const Player* Room::find_player(const std::string& nickname) const {
    for (const auto& p : players_) if (p->nickname() == nickname) return p.get();
    return nullptr;
}

Player* Room::add_player(std::unique_ptr<Player> p) {
    Player* raw = p.get();
    players_.push_back(std::move(p));
    if (host_.empty()) host_ = raw->nickname();
    return raw;
}

bool Room::remove_player(const std::string& nickname) {
    auto it = std::find_if(players_.begin(), players_.end(),
        [&](const auto& p) { return p->nickname() == nickname; });
    if (it == players_.end()) return false;
    bool was_host = (nickname == host_);
    players_.erase(it);
    if (was_host) promote_next_host_if_needed();
    return true;
}

void Room::promote_next_host_if_needed() {
    if (host_.empty() || !find_player(host_)) {
        if (!players_.empty()) host_ = players_.front()->nickname();
        else host_.clear();
    }
}

} // namespace ip::session
```

- [ ] **Step 4: Add to `ip_core`**

```cmake
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
    net/ws_handshake.cpp
    net/ws_frame.cpp
    session/player.cpp
    session/room.cpp
)
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build server/build -j
cd server/build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
cd ../..
git add server/session/room.h server/session/room.cpp tests/test_player_room.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add Room with host promotion and player lookup

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5.3: `RoomRegistry`

**Files:**
- Create: `server/session/room_registry.h`
- Create: `server/session/room_registry.cpp`

- [ ] **Step 1: Append to `tests/test_player_room.cpp`**

```cpp
TEST_CASE("RoomRegistry creates and finds rooms", "[registry]") {
    RoomRegistry reg;
    auto* room = reg.create("ABCDEF", 4);
    REQUIRE(room != nullptr);
    REQUIRE(reg.find("ABCDEF") == room);
    REQUIRE(reg.find("ZZZZZZ") == nullptr);
}

TEST_CASE("RoomRegistry rejects duplicate codes", "[registry]") {
    RoomRegistry reg;
    reg.create("ABCDEF", 4);
    REQUIRE(reg.create("ABCDEF", 4) == nullptr);
}
```

- [ ] **Step 2: Write `server/session/room_registry.h`**

```cpp
#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "session/room.h"

namespace ip::session {

class RoomRegistry {
public:
    // Returns a pointer to the new Room, or nullptr if the code is already taken.
    Room* create(const std::string& code, int max_players);

    Room* find(const std::string& code);
    const Room* find(const std::string& code) const;

    void erase(const std::string& code) { rooms_.erase(code); }

    const std::unordered_map<std::string, std::unique_ptr<Room>>& all() const { return rooms_; }

private:
    std::unordered_map<std::string, std::unique_ptr<Room>> rooms_;
};

} // namespace ip::session
```

- [ ] **Step 3: Write `server/session/room_registry.cpp`**

```cpp
#include "session/room_registry.h"

namespace ip::session {

Room* RoomRegistry::create(const std::string& code, int max_players) {
    if (rooms_.count(code)) return nullptr;
    auto r = std::make_unique<Room>(code, max_players);
    Room* raw = r.get();
    rooms_.emplace(code, std::move(r));
    return raw;
}

Room* RoomRegistry::find(const std::string& code) {
    auto it = rooms_.find(code);
    return it == rooms_.end() ? nullptr : it->second.get();
}

const Room* RoomRegistry::find(const std::string& code) const {
    auto it = rooms_.find(code);
    return it == rooms_.end() ? nullptr : it->second.get();
}

} // namespace ip::session
```

- [ ] **Step 4: Add to `ip_core` and build**

```cmake
    session/player.cpp
    session/room.cpp
    session/room_registry.cpp
```

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add server/session/room_registry.h server/session/room_registry.cpp tests/test_player_room.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add RoomRegistry keyed by room code

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M5.**

---

# Phase 6 — Milestone M6: Lobby Flow

**Outcome:** Two browser tabs can `create_room`, `join_room`, see each other in the lobby, and `start_game` transitions the room to LOCKED. No actual game is played yet.

### Task 6.1: Room code generator — TDD

**Files:**
- Create: `server/util/random.h`
- Create: `server/util/random.cpp`
- Create: `tests/test_room_code.cpp`

- [ ] **Step 1: Write `tests/test_room_code.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "util/random.h"

#include <set>
#include <string>

using namespace ip::util;

TEST_CASE("room code is 6 chars from confusion-free alphabet", "[roomcode]") {
    RoomCodeGen gen(12345);
    for (int i = 0; i < 100; ++i) {
        auto code = gen.generate();
        REQUIRE(code.size() == 6);
        for (char c : code) {
            bool ok = (c >= 'A' && c <= 'Z' && c != 'I' && c != 'O') ||
                      (c >= '2' && c <= '9');
            REQUIRE(ok);
        }
    }
}

TEST_CASE("seeded generator is deterministic", "[roomcode]") {
    RoomCodeGen a(42), b(42);
    for (int i = 0; i < 10; ++i) REQUIRE(a.generate() == b.generate());
}
```

- [ ] **Step 2: Add test file to `tests/CMakeLists.txt`**

```cmake
add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
    test_ws_frame.cpp
    test_ws_frame_fragments.cpp
    test_player_room.cpp
    test_room_code.cpp
)
```

- [ ] **Step 3: Write `server/util/random.h`**

```cpp
#pragma once
#include <cstdint>
#include <random>
#include <string>

namespace ip::util {

// Generates 6-character room codes drawn from a 32-letter confusion-free alphabet
// (A-Z minus I, O; 2-9).
class RoomCodeGen {
public:
    explicit RoomCodeGen(uint64_t seed);
    std::string generate();

private:
    std::mt19937_64 rng_;
};

} // namespace ip::util
```

- [ ] **Step 4: Write `server/util/random.cpp`**

```cpp
#include "util/random.h"

namespace ip::util {

static constexpr const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 chars

RoomCodeGen::RoomCodeGen(uint64_t seed) : rng_(seed) {}

std::string RoomCodeGen::generate() {
    std::uniform_int_distribution<int> dist(0, 31);
    std::string out(6, ' ');
    for (int i = 0; i < 6; ++i) out[i] = kAlphabet[dist(rng_)];
    return out;
}

} // namespace ip::util
```

- [ ] **Step 5: Add to `ip_core`, build, test**

```cmake
    util/random.cpp
```

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

- [ ] **Step 6: Commit**

```bash
git add server/util/random.h server/util/random.cpp tests/test_room_code.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add RoomCodeGen with 32-char confusion-free alphabet

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6.2: Nickname validator — TDD

**Files:**
- Modify: `server/session/player.h` (add free function)
- Modify: `server/session/player.cpp`
- Modify: `tests/test_player_room.cpp`

- [ ] **Step 1: Append to `tests/test_player_room.cpp`**

```cpp
TEST_CASE("nickname validator", "[nickname]") {
    REQUIRE(validate_nickname("alice"));
    REQUIRE(validate_nickname("밥"));
    REQUIRE(validate_nickname("user_1"));
    REQUIRE_FALSE(validate_nickname(""));
    REQUIRE_FALSE(validate_nickname(std::string(17, 'x'))); // > 16
    REQUIRE_FALSE(validate_nickname(" alice"));  // leading space
    REQUIRE_FALSE(validate_nickname("alice "));  // trailing space
    REQUIRE_FALSE(validate_nickname("al\tice")); // control
}
```

- [ ] **Step 2: Add declaration to `session/player.h`** (top of namespace):

```cpp
bool validate_nickname(const std::string& s);
```

- [ ] **Step 3: Replace `session/player.cpp`**

```cpp
#include "session/player.h"

#include <cctype>

namespace ip::session {

bool validate_nickname(const std::string& s) {
    if (s.empty()) return false;
    // Count code points loosely; we accept any byte except control chars and
    // reject surrounding whitespace. Max length: 16 Unicode code points.
    // Byte length <= 48 is a cheap upper bound for 16 UTF-8 code points (3 bytes each).
    if (s.size() > 48) return false;
    if (std::isspace(static_cast<unsigned char>(s.front()))) return false;
    if (std::isspace(static_cast<unsigned char>(s.back()))) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    // Approximate code-point count: number of bytes that are not continuation bytes.
    std::size_t cps = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++cps;
    return cps >= 1 && cps <= 16;
}

} // namespace ip::session
```

- [ ] **Step 4: Build and run tests**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

- [ ] **Step 5: Commit**

```bash
git add server/session/player.h server/session/player.cpp tests/test_player_room.cpp
git commit -m "$(cat <<'EOF'
Add nickname validator (length, whitespace, control chars)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6.3: `Router` — JSON message dispatch

**Files:**
- Create: `server/session/router.h`
- Create: `server/session/router.cpp`

The Router owns the `RoomRegistry` and the `RoomCodeGen`, and exposes two things to the network layer:
- `on_text(int fd, std::string_view message)` — called when a WebSocket text message arrives
- `on_disconnect(int fd, uint64_t now_ms)` — called when a connection is closed

It needs a `send` callback for outgoing messages. For now implement only `create_room`, `join_room` (new-join path only), and `lobby_update` broadcast.

- [ ] **Step 1: Write `server/session/router.h`**

```cpp
#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "session/room_registry.h"
#include "util/random.h"

namespace ip::session {

class Router {
public:
    using SendFn = std::function<void(int fd, const std::string& text)>;
    using CloseFn = std::function<void(int fd)>;

    Router(SendFn send, CloseFn close, uint64_t seed)
        : send_(std::move(send)), close_(std::move(close)), code_gen_(seed) {}

    void on_text(int fd, const std::string& text);
    void on_disconnect(int fd, uint64_t now_ms);

    RoomRegistry& rooms() { return rooms_; }

private:
    void handle_create_room(int fd, const nlohmann::json& msg);
    void handle_join_room(int fd, const nlohmann::json& msg);

    void send_error(int fd, const std::string& code, const std::string& message);
    void send_json(int fd, const nlohmann::json& j);

    // fd -> (room_code, nickname) so we know where to route / clean up.
    struct FdOwner { std::string room_code; std::string nickname; };
    std::unordered_map<int, FdOwner> fd_owner_;

    SendFn send_;
    CloseFn close_;
    RoomRegistry rooms_;
    ip::util::RoomCodeGen code_gen_;
};

} // namespace ip::session
```

- [ ] **Step 2: Write `server/session/router.cpp`**

```cpp
#include "session/router.h"

#include <iostream>

namespace ip::session {

using nlohmann::json;

void Router::send_json(int fd, const json& j) {
    send_(fd, j.dump());
}

void Router::send_error(int fd, const std::string& code, const std::string& message) {
    send_json(fd, {{"type", "error"}, {"code", code}, {"message", message}});
}

static json player_entry(const Player& p) {
    return {
        {"nickname", p.nickname()},
        {"chips", p.chips()},
        {"card", nullptr},
        {"bet_this_round", 0},
        {"status", "active"},
        {"connected", p.connected()},
    };
}

static json lobby_update(const Room& r) {
    json players = json::array();
    for (const auto& p : r.players()) players.push_back(p->nickname());
    return {
        {"type", "lobby_update"},
        {"room_code", r.code()},
        {"players", players},
        {"host", r.host_nickname()},
        {"max_players", r.max_players()},
    };
}

void Router::on_text(int fd, const std::string& text) {
    json msg;
    try {
        msg = json::parse(text);
    } catch (const std::exception&) {
        send_error(fd, "MALFORMED_MESSAGE", "JSON parse error");
        return;
    }
    if (!msg.is_object() || !msg.contains("type") || !msg["type"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "missing type");
        return;
    }
    const std::string type = msg["type"];
    if (type == "create_room") handle_create_room(fd, msg);
    else if (type == "join_room") handle_join_room(fd, msg);
    else send_error(fd, "MALFORMED_MESSAGE", "unknown type: " + type);
}

void Router::handle_create_room(int fd, const json& msg) {
    if (!msg.contains("nickname") || !msg["nickname"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "nickname required");
        return;
    }
    const std::string nickname = msg["nickname"];
    if (!validate_nickname(nickname)) {
        send_error(fd, "NICKNAME_INVALID", "nickname failed validation");
        return;
    }
    int max_players = 4;
    if (msg.contains("max_players") && msg["max_players"].is_number_integer()) {
        int v = msg["max_players"].get<int>();
        if (v >= 2 && v <= 4) max_players = v;
    }

    // Generate a unique code.
    std::string code;
    for (int attempt = 0; attempt < 20; ++attempt) {
        code = code_gen_.generate();
        if (!rooms_.find(code)) break;
    }
    Room* room = rooms_.create(code, max_players);
    if (!room) {
        send_error(fd, "INTERNAL_ERROR", "could not allocate room code");
        return;
    }
    auto player = std::make_unique<Player>(nickname, 0);
    player->bind_fd(fd);
    room->add_player(std::move(player));

    fd_owner_[fd] = FdOwner{code, nickname};

    send_json(fd, {
        {"type", "room_created"},
        {"room_code", code},
        {"you", nickname},
        {"host", nickname},
    });
    // Broadcast lobby_update to everyone in the room (currently just the creator).
    for (const auto& p : room->players()) {
        if (p->connected()) send_json(p->fd(), lobby_update(*room));
    }
}

void Router::handle_join_room(int fd, const json& msg) {
    if (!msg.contains("room_code") || !msg["room_code"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "room_code required");
        return;
    }
    if (!msg.contains("nickname") || !msg["nickname"].is_string()) {
        send_error(fd, "MALFORMED_MESSAGE", "nickname required");
        return;
    }
    const std::string code = msg["room_code"];
    const std::string nickname = msg["nickname"];

    Room* room = rooms_.find(code);
    if (!room) { send_error(fd, "ROOM_NOT_FOUND", "no room with that code"); return; }

    // Check nickname taken by connected player.
    if (auto* existing = room->find_player(nickname)) {
        if (existing->connected()) {
            send_error(fd, "NICKNAME_TAKEN", "nickname is in use");
            return;
        }
        // Reconnect path added in M7; for M6 just reject until then.
        send_error(fd, "NICKNAME_TAKEN", "nickname is in use");
        return;
    }

    if (room->state() != RoomState::Open) {
        send_error(fd, "ROOM_IN_PROGRESS", "game already started");
        return;
    }
    if (static_cast<int>(room->players().size()) >= room->max_players()) {
        send_error(fd, "ROOM_FULL", "room is full");
        return;
    }
    if (!validate_nickname(nickname)) {
        send_error(fd, "NICKNAME_INVALID", "nickname failed validation");
        return;
    }

    auto player = std::make_unique<Player>(nickname, static_cast<int>(room->players().size()));
    player->bind_fd(fd);
    room->add_player(std::move(player));
    fd_owner_[fd] = FdOwner{code, nickname};

    send_json(fd, {
        {"type", "joined_room"},
        {"room_code", code},
        {"you", nickname},
        {"reconnect", false},
    });
    for (const auto& p : room->players()) {
        if (p->connected()) send_json(p->fd(), lobby_update(*room));
    }
}

void Router::on_disconnect(int fd, uint64_t /*now_ms*/) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) return;
    const std::string code = it->second.room_code;
    const std::string nickname = it->second.nickname;
    fd_owner_.erase(it);

    Room* room = rooms_.find(code);
    if (!room) return;

    // For M6 (no grace period yet), treat any disconnect as a leave.
    room->remove_player(nickname);
    if (room->players().empty()) {
        rooms_.erase(code);
        return;
    }
    for (const auto& p : room->players()) {
        if (p->connected()) send_json(p->fd(), lobby_update(*room));
    }
}

} // namespace ip::session
```

- [ ] **Step 3: Add to `ip_core`**

```cmake
    session/router.cpp
```

- [ ] **Step 4: Wire `Router` into `main.cpp`**

Replace `server/main.cpp` with:

```cpp
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
```

- [ ] **Step 5: Build and manual-test in browser devtools**

```bash
cmake --build server/build -j
./server/build/ip_server &
```

Tab 1:
```js
const ws = new WebSocket("ws://localhost:8080/");
ws.onopen = () => ws.send(JSON.stringify({type:"create_room", nickname:"alice"}));
ws.onmessage = (e) => console.log(JSON.parse(e.data));
```

Expected: `room_created {room_code: "XXXXXX", you: "alice", host: "alice"}` then `lobby_update`.

Tab 2 (use the actual code printed above):
```js
const ws = new WebSocket("ws://localhost:8080/");
ws.onopen = () => ws.send(JSON.stringify({type:"join_room", room_code:"XXXXXX", nickname:"bob"}));
ws.onmessage = (e) => console.log(JSON.parse(e.data));
```

Expected: `joined_room` and both tabs receive a `lobby_update` with 2 players.

- [ ] **Step 6: Commit**

```bash
git add server/session/router.h server/session/router.cpp server/main.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Implement create_room / join_room / lobby_update via Router

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M6.**

---

# Phase 7 — Milestone M7: Disconnect Detection and 15-Second Reconnect Window

**Outcome:** Closing a browser tab triggers `player_disconnected` in the other tab; opening a new tab within 15 seconds with the same nickname restores the seat and triggers `player_reconnected`. After 15 seconds without reconnect, the player is removed.

### Task 7.1: Reconnect path in `handle_join_room` and grace-period tracking — TDD

**Files:**
- Modify: `server/session/router.h`
- Modify: `server/session/router.cpp`
- Create: `tests/test_join_flow.cpp`

- [ ] **Step 1: Write `tests/test_join_flow.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "session/router.h"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ip::session;
using nlohmann::json;

namespace {

struct Harness {
    std::unordered_map<int, std::vector<json>> sent;
    std::vector<int> closed;
    Router router;

    Harness()
      : router(
            [this](int fd, const std::string& text) { sent[fd].push_back(json::parse(text)); },
            [this](int fd) { closed.push_back(fd); },
            42) {}

    json last(int fd, const std::string& type) {
        for (auto it = sent[fd].rbegin(); it != sent[fd].rend(); ++it) {
            if ((*it).value("type", "") == type) return *it;
        }
        return json{};
    }

    void text(int fd, const json& msg) { router.on_text(fd, msg.dump()); }
};

} // namespace

TEST_CASE("create + join yields lobby_update to both", "[join]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    auto created = h.last(1, "room_created");
    REQUIRE(created["room_code"].is_string());
    std::string code = created["room_code"];

    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    REQUIRE(h.last(1, "lobby_update")["players"].size() == 2);
    REQUIRE(h.last(2, "lobby_update")["players"].size() == 2);
}

TEST_CASE("reconnect restores same-nickname slot within grace", "[join][reconnect]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];

    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    // "bob" disconnects at t=1000.
    h.router.on_disconnect(2, /*now_ms=*/1000);

    // Within 15s window (t=1500), new fd joins with same nickname -> reconnect.
    h.text(3, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    auto joined = h.last(3, "joined_room");
    REQUIRE(joined["reconnect"] == true);
}

TEST_CASE("nickname taken rejects new joiner", "[join]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];
    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "alice"}});
    auto err = h.last(2, "error");
    REQUIRE(err["code"] == "NICKNAME_TAKEN");
}

TEST_CASE("grace timeout removes player", "[join][timeout]") {
    Harness h;
    h.text(1, {{"type", "create_room"}, {"nickname", "alice"}});
    std::string code = h.last(1, "room_created")["room_code"];
    h.text(2, {{"type", "join_room"}, {"room_code", code}, {"nickname", "bob"}});
    h.router.on_disconnect(2, /*now_ms=*/1000);

    // Advance the clock past 15s and tick.
    h.router.tick(/*now_ms=*/1000 + 15001);
    auto left = h.last(1, "player_left");
    REQUIRE(left["nickname"] == "bob");
    REQUIRE(left["reason"] == "timeout");
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

```cmake
    test_join_flow.cpp
```

- [ ] **Step 3: Extend `Router` with `tick()` and grace handling**

In `server/session/router.h`, add inside the public section:

```cpp
    void tick(uint64_t now_ms);
```

Add a private member:

```cpp
    // (room_code, nickname) -> disconnected_at_ms
    std::unordered_map<std::string, uint64_t> grace_;
```

- [ ] **Step 4: Update `server/session/router.cpp`** — add the reconnect branch and `tick`:

Replace the nickname-taken block in `handle_join_room` with:

```cpp
    if (auto* existing = room->find_player(nickname)) {
        if (existing->connected()) {
            send_error(fd, "NICKNAME_TAKEN", "nickname is in use");
            return;
        }
        // Reconnect path.
        existing->bind_fd(fd);
        fd_owner_[fd] = FdOwner{code, nickname};
        grace_.erase(code + ":" + nickname);
        send_json(fd, {
            {"type", "joined_room"},
            {"room_code", code},
            {"you", nickname},
            {"reconnect", true},
        });
        for (const auto& p : room->players()) {
            if (p->connected() && p->fd() != fd) {
                send_json(p->fd(), {
                    {"type", "player_reconnected"},
                    {"nickname", nickname},
                });
            }
        }
        return;
    }
```

Replace `on_disconnect` with:

```cpp
void Router::on_disconnect(int fd, uint64_t now_ms) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) return;
    const std::string code = it->second.room_code;
    const std::string nickname = it->second.nickname;
    fd_owner_.erase(it);

    Room* room = rooms_.find(code);
    if (!room) return;
    Player* player = room->find_player(nickname);
    if (!player) return;

    player->mark_disconnected(now_ms);
    grace_[code + ":" + nickname] = now_ms;

    for (const auto& p : room->players()) {
        if (p->connected()) {
            send_json(p->fd(), {
                {"type", "player_disconnected"},
                {"nickname", nickname},
                {"grace_period_ms", 15000},
            });
        }
    }
}

void Router::tick(uint64_t now_ms) {
    constexpr uint64_t kGraceMs = 15000;
    std::vector<std::string> expired;
    for (const auto& [key, at] : grace_) {
        if (now_ms - at >= kGraceMs) expired.push_back(key);
    }
    for (const auto& key : expired) {
        grace_.erase(key);
        auto colon = key.find(':');
        std::string code = key.substr(0, colon);
        std::string nickname = key.substr(colon + 1);
        Room* room = rooms_.find(code);
        if (!room) continue;
        room->remove_player(nickname);
        for (const auto& p : room->players()) {
            if (p->connected()) {
                send_json(p->fd(), {
                    {"type", "player_left"},
                    {"nickname", nickname},
                    {"reason", "timeout"},
                });
                send_json(p->fd(), {
                    {"type", "lobby_update"},
                    {"room_code", room->code()},
                    {"players", [&] {
                        json a = json::array();
                        for (const auto& pp : room->players()) a.push_back(pp->nickname());
                        return a;
                    }()},
                    {"host", room->host_nickname()},
                    {"max_players", room->max_players()},
                });
            }
        }
        if (room->players().empty()) rooms_.erase(code);
    }
}
```

- [ ] **Step 5: Call `router.tick(now_ms())` each iteration in `main.cpp`**

At the start of the `EventLoop`'s data callback (or by adding a separate timer tick), add a call at the top:

```cpp
            [&](int fd, const std::string& chunk) {
                router.tick(now_ms());
                // ... existing body ...
            }
```

Also call it in the disconnect callback:

```cpp
            [&](int fd) {
                router.on_disconnect(fd, now_ms());
                router.tick(now_ms());
                states.erase(fd);
            }
```

(An idle server with no events won't auto-tick; that's acceptable for this milestone because timeouts only matter when events are flowing. A proper scheduled tick is listed in §13 future work.)

- [ ] **Step 6: Build and run tests**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

Expected: all tests pass.

- [ ] **Step 7: Manual browser test**

Tab 1 creates a room, Tab 2 joins. Close Tab 2 — Tab 1 should log `player_disconnected`. Within 15 s, reopen Tab 2 and re-join with the same nickname — should get `reconnect: true` and Tab 1 should log `player_reconnected`. Wait 16 s instead — Tab 1 should log `player_left` with `reason: "timeout"`.

- [ ] **Step 8: Commit**

```bash
git add server/session/router.h server/session/router.cpp server/main.cpp tests/test_join_flow.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add 15s reconnect window with grace-period tracking (milestone M7)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M7.**

---

# Phase 8 — Milestone M8: Indian Poker Game Logic (Unit-Tested, No Sockets)

**Outcome:** A `game/indian_poker` module that can drive an entire hand (deal → bet → showdown → winner) in isolation, with deterministic tests.

### Task 8.1: `IGame` interface and `Outgoing` record

**Files:**
- Create: `server/game/igame.h`
- Create: `server/game/outgoing.h`

- [ ] **Step 1: Write `server/game/outgoing.h`**

```cpp
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

namespace ip::game {

// Describes a message the game wants to send out. The session layer translates
// these into actual socket writes. "Broadcast" means "send this payload to
// every currently-connected player, computing per-player views on their side".
struct Broadcast { nlohmann::json payload; };
struct Private   { std::string nickname; nlohmann::json payload; };

using Outgoing = std::variant<Broadcast, Private>;
using OutgoingList = std::vector<Outgoing>;

} // namespace ip::game
```

- [ ] **Step 2: Write `server/game/igame.h`**

```cpp
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "game/outgoing.h"

namespace ip::game {

// Abstract base class: every pluggable game implements this.
class IGame {
public:
    virtual ~IGame() = default;

    // Called once when the game starts. `nicknames` are seated in order.
    virtual OutgoingList start(const std::vector<std::string>& nicknames) = 0;

    // Called on each client action. Unknown actions return an error Outgoing.
    virtual OutgoingList handle_action(const std::string& nickname,
                                       const nlohmann::json& action) = 0;

    // Called when a player disconnects mid-game (after the grace window expires
    // or when they leave voluntarily).
    virtual OutgoingList on_player_gone(const std::string& nickname,
                                        const std::string& reason) = 0;

    // Produces the per-player view of the current state. Used for reconnection
    // snapshots and whenever a broadcast is fanned out.
    virtual nlohmann::json snapshot_for(const std::string& nickname) const = 0;

    // True once the game has reached GAME_OVER.
    virtual bool finished() const = 0;
};

} // namespace ip::game
```

- [ ] **Step 3: Commit (no build needed yet — both are headers with no consumers)**

```bash
git add server/game/igame.h server/game/outgoing.h
git commit -m "$(cat <<'EOF'
Define IGame interface and Outgoing message types

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8.2: `IndianPoker` state and LOBBY → DEALING → BETTING setup — TDD

**Files:**
- Create: `server/game/indian_poker/state.h`
- Create: `server/game/indian_poker/indian_poker.h`
- Create: `server/game/indian_poker/indian_poker.cpp`
- Create: `tests/test_indian_poker.cpp`

- [ ] **Step 1: Write `server/game/indian_poker/state.h`**

```cpp
#pragma once
#include <string>
#include <vector>

namespace ip::game::indian_poker {

enum class PlayerStatus { Active, Folded, Eliminated, DisconnectedGrace };

struct PlayerState {
    std::string nickname;
    int chips = 100;
    int card = 0;              // 0 = not yet dealt
    int bet_this_round = 0;
    PlayerStatus status = PlayerStatus::Active;
    bool connected = true;
    bool ready_next_hand = false;
};

enum class Phase { Lobby, Dealing, Betting, Showdown, HandOver, GameOver };

struct State {
    Phase phase = Phase::Lobby;
    int hand_number = 0;
    std::vector<PlayerState> players;
    int pot = 0;
    int current_bet = 0;
    int min_raise = 5;
    int ante = 5;
    int current_turn_index = -1;
    int starting_player_index = 0;
};

} // namespace ip::game::indian_poker
```

- [ ] **Step 2: Write `server/game/indian_poker/indian_poker.h`**

```cpp
#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "game/igame.h"
#include "game/indian_poker/state.h"

namespace ip::game::indian_poker {

// Deterministic deal function injected from outside for testability.
// Returns one card per player (values 1-10).
using Dealer = std::function<std::vector<int>(int num_players)>;

class IndianPoker : public IGame {
public:
    explicit IndianPoker(Dealer dealer);

    OutgoingList start(const std::vector<std::string>& nicknames) override;
    OutgoingList handle_action(const std::string& nickname,
                               const nlohmann::json& action) override;
    OutgoingList on_player_gone(const std::string& nickname,
                                const std::string& reason) override;
    nlohmann::json snapshot_for(const std::string& nickname) const override;
    bool finished() const override { return state_.phase == Phase::GameOver; }

    const State& state() const { return state_; }

private:
    Dealer dealer_;
    State state_;

    // Helpers.
    void deal_new_hand();
    void advance_turn();
    void maybe_end_betting(OutgoingList& out);
    void do_showdown(OutgoingList& out);
    void enter_hand_over(OutgoingList& out, const std::string& winner_nick, int pot_won);
    void start_next_hand_or_finish(OutgoingList& out);
    void broadcast_state(OutgoingList& out);

    nlohmann::json make_state_json(const std::string& viewer) const;
};

} // namespace ip::game::indian_poker
```

- [ ] **Step 3: Write `tests/test_indian_poker.cpp`** — LOBBY/DEAL test only for this task

```cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "game/indian_poker/indian_poker.h"

using namespace ip::game;
using namespace ip::game::indian_poker;
using nlohmann::json;

// Fixed dealer: returns the given sequence repeatedly.
static Dealer make_fixed_dealer(std::vector<int> cards) {
    return [cards = std::move(cards)](int /*n*/) { return cards; };
}

static IndianPoker make_2p_game(std::vector<int> cards = {5, 9}) {
    IndianPoker g(make_fixed_dealer(std::move(cards)));
    g.start({"alice", "bob"});
    return g;
}

TEST_CASE("start deals cards, deducts ante, enters BETTING", "[ip][setup]") {
    auto g = make_2p_game({5, 9});
    const auto& s = g.state();
    REQUIRE(s.phase == Phase::Betting);
    REQUIRE(s.hand_number == 1);
    REQUIRE(s.players.size() == 2);
    REQUIRE(s.players[0].card == 5);
    REQUIRE(s.players[1].card == 9);
    REQUIRE(s.players[0].chips == 95);  // 100 - ante(5)
    REQUIRE(s.players[1].chips == 95);
    REQUIRE(s.pot == 10);
    REQUIRE(s.current_bet == 5);
    REQUIRE(s.current_turn_index == 0);
}

TEST_CASE("snapshot hides own card during BETTING", "[ip][snapshot]") {
    auto g = make_2p_game({5, 9});
    auto alice_view = g.snapshot_for("alice");
    const auto& players = alice_view["players"];
    // alice is index 0 -> her own card nulled.
    REQUIRE(players[0]["card"].is_null());
    // bob's card visible as 9.
    REQUIRE(players[1]["card"] == 9);
}
```

- [ ] **Step 4: Write initial `server/game/indian_poker/indian_poker.cpp`**

```cpp
#include "game/indian_poker/indian_poker.h"

#include <algorithm>
#include <stdexcept>

namespace ip::game::indian_poker {

using nlohmann::json;

IndianPoker::IndianPoker(Dealer dealer) : dealer_(std::move(dealer)) {}

OutgoingList IndianPoker::start(const std::vector<std::string>& nicknames) {
    state_.players.clear();
    int seat = 0;
    for (const auto& nick : nicknames) {
        PlayerState p;
        p.nickname = nick;
        p.chips = 100;
        state_.players.push_back(p);
        (void)seat;
    }
    state_.phase = Phase::Dealing;
    state_.hand_number = 0;
    state_.starting_player_index = 0;
    deal_new_hand();
    OutgoingList out;
    broadcast_state(out);
    return out;
}

void IndianPoker::deal_new_hand() {
    state_.hand_number += 1;
    state_.pot = 0;
    state_.current_bet = state_.ante;

    // Reset per-hand player state.
    int active = 0;
    for (auto& p : state_.players) {
        if (p.status == PlayerStatus::Eliminated) continue;
        p.card = 0;
        p.bet_this_round = 0;
        p.ready_next_hand = false;
        if (p.status == PlayerStatus::Folded) p.status = PlayerStatus::Active;
        if (p.status == PlayerStatus::Active) ++active;
    }

    std::vector<int> cards = dealer_(active);
    std::size_t ci = 0;
    for (auto& p : state_.players) {
        if (p.status != PlayerStatus::Active) continue;
        p.card = cards[ci++];
        // Deduct ante.
        p.chips -= state_.ante;
        p.bet_this_round = state_.ante;
        state_.pot += state_.ante;
    }

    state_.current_turn_index = state_.starting_player_index;
    state_.phase = Phase::Betting;
}

void IndianPoker::advance_turn() {
    int n = static_cast<int>(state_.players.size());
    for (int step = 1; step <= n; ++step) {
        int idx = (state_.current_turn_index + step) % n;
        const auto& p = state_.players[idx];
        if (p.status == PlayerStatus::Active && p.chips >= 0) {
            state_.current_turn_index = idx;
            return;
        }
    }
    state_.current_turn_index = -1;
}

void IndianPoker::broadcast_state(OutgoingList& out) {
    // The session layer will materialize one message per connected player using
    // snapshot_for(); here we emit a "fan-out request" as a single Broadcast
    // with an empty payload to mean "re-snapshot and send to everyone".
    json marker = {{"type", "__game_state_fanout"}};
    out.push_back(Broadcast{marker});
}

json IndianPoker::make_state_json(const std::string& viewer) const {
    json players = json::array();
    bool reveal_all = (state_.phase == Phase::Showdown || state_.phase == Phase::HandOver);
    for (const auto& p : state_.players) {
        json card = nullptr;
        if (p.card > 0) {
            if (p.status == PlayerStatus::Folded && !reveal_all) card = nullptr;
            else if (p.nickname == viewer && !reveal_all) card = nullptr;
            else card = p.card;
        }
        players.push_back({
            {"nickname", p.nickname},
            {"chips", p.chips},
            {"card", card},
            {"bet_this_round", p.bet_this_round},
            {"status", [&]() -> std::string {
                switch (p.status) {
                    case PlayerStatus::Active: return "active";
                    case PlayerStatus::Folded: return "folded";
                    case PlayerStatus::Eliminated: return "eliminated";
                    case PlayerStatus::DisconnectedGrace: return "disconnected_grace";
                }
                return "active";
            }()},
            {"connected", p.connected},
        });
    }
    std::string current_turn;
    if (state_.current_turn_index >= 0 &&
        state_.current_turn_index < static_cast<int>(state_.players.size())) {
        current_turn = state_.players[state_.current_turn_index].nickname;
    }
    json j = {
        {"type", "game_state"},
        {"phase", [&]() -> std::string {
            switch (state_.phase) {
                case Phase::Lobby: return "LOBBY";
                case Phase::Dealing: return "DEALING";
                case Phase::Betting: return "BETTING";
                case Phase::Showdown: return "SHOWDOWN";
                case Phase::HandOver: return "HAND_OVER";
                case Phase::GameOver: return "GAME_OVER";
            }
            return "LOBBY";
        }()},
        {"hand_number", state_.hand_number},
        {"players", players},
        {"pot", state_.pot},
        {"current_turn", current_turn},
        {"current_bet", state_.current_bet},
        {"min_raise", state_.min_raise},
        {"you", viewer},
    };
    return j;
}

json IndianPoker::snapshot_for(const std::string& nickname) const {
    return make_state_json(nickname);
}

OutgoingList IndianPoker::handle_action(const std::string& /*nickname*/,
                                         const nlohmann::json& /*action*/) {
    return {}; // Implemented in later tasks.
}

OutgoingList IndianPoker::on_player_gone(const std::string& /*nickname*/,
                                         const std::string& /*reason*/) {
    return {}; // Implemented in later tasks.
}

} // namespace ip::game::indian_poker
```

- [ ] **Step 5: Add to `ip_core` and `tests/CMakeLists.txt`**

```cmake
    game/indian_poker/indian_poker.cpp
```

```cmake
    test_indian_poker.cpp
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

Expected: both setup tests pass.

- [ ] **Step 7: Commit**

```bash
git add server/game/indian_poker tests/test_indian_poker.cpp server/CMakeLists.txt tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
IndianPoker: initial state, deal, redacted snapshot

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8.3: Check / Raise / Fold actions — TDD

**Files:**
- Modify: `server/game/indian_poker/indian_poker.cpp`
- Modify: `tests/test_indian_poker.cpp`

- [ ] **Step 1: Append tests to `tests/test_indian_poker.cpp`**

```cpp
TEST_CASE("check on matched bet advances turn", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    // Both players already have bet_this_round == ante(5) == current_bet.
    // alice checks -> turn goes to bob.
    g.handle_action("alice", {{"type", "action"}, {"action", "check"}});
    REQUIRE(g.state().current_turn_index == 1);
}

TEST_CASE("raise changes current_bet and turn", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    g.handle_action("alice", {{"type", "action"}, {"action", "raise"}, {"amount", 15}});
    const auto& s = g.state();
    REQUIRE(s.current_bet == 15);
    REQUIRE(s.players[0].bet_this_round == 15);
    REQUIRE(s.players[0].chips == 95 - 10); // paid 10 more
    REQUIRE(s.pot == 10 + 10);
    REQUIRE(s.current_turn_index == 1);
}

TEST_CASE("raise below min_raise is rejected", "[ip][action]") {
    auto g = make_2p_game({5, 9});
    auto out = g.handle_action("alice", {{"type", "action"}, {"action", "raise"}, {"amount", 8}});
    // Find an error Private message.
    bool found = false;
    for (const auto& o : out) {
        if (std::holds_alternative<Private>(o)) {
            const auto& pj = std::get<Private>(o).payload;
            if (pj.value("type", "") == "error") {
                REQUIRE(pj["code"] == "INVALID_RAISE_AMOUNT");
                found = true;
            }
        }
    }
    REQUIRE(found);
    // State should be unchanged.
    REQUIRE(g.state().current_bet == 5);
}

TEST_CASE("fold marks player folded and skips their turn", "[ip][action]") {
    IndianPoker g(make_fixed_dealer({4, 7, 9}));
    g.start({"alice", "bob", "carol"});
    g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
    REQUIRE(g.state().players[0].status == PlayerStatus::Folded);
    REQUIRE(g.state().current_turn_index == 1);
}
```

- [ ] **Step 2: Replace `IndianPoker::handle_action` in the .cpp**

```cpp
static Private make_error(const std::string& nick, const std::string& code, const std::string& msg) {
    return Private{nick, {{"type", "error"}, {"code", code}, {"message", msg}}};
}

OutgoingList IndianPoker::handle_action(const std::string& nickname,
                                         const nlohmann::json& action) {
    OutgoingList out;
    if (state_.phase != Phase::Betting) {
        out.push_back(make_error(nickname, "INVALID_ACTION", "not in betting phase"));
        return out;
    }
    if (state_.current_turn_index < 0) {
        out.push_back(make_error(nickname, "INVALID_ACTION", "no active turn"));
        return out;
    }
    auto& turn_player = state_.players[state_.current_turn_index];
    if (turn_player.nickname != nickname) {
        out.push_back(make_error(nickname, "NOT_YOUR_TURN", "wait your turn"));
        return out;
    }
    if (!action.contains("action") || !action["action"].is_string()) {
        out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "missing action"));
        return out;
    }
    const std::string kind = action["action"];

    if (kind == "check") {
        if (turn_player.bet_this_round != state_.current_bet) {
            out.push_back(make_error(nickname, "INVALID_ACTION", "must raise or fold; cannot check"));
            return out;
        }
    } else if (kind == "raise") {
        if (!action.contains("amount") || !action["amount"].is_number_integer()) {
            out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "missing raise amount"));
            return out;
        }
        int amount = action["amount"];
        int diff = amount - turn_player.bet_this_round;
        bool allin = diff == turn_player.chips;
        if (!allin && amount < state_.current_bet + state_.min_raise) {
            out.push_back(make_error(nickname, "INVALID_RAISE_AMOUNT", "below minimum raise"));
            return out;
        }
        if (diff <= 0 || diff > turn_player.chips) {
            out.push_back(make_error(nickname, "INVALID_RAISE_AMOUNT", "insufficient chips"));
            return out;
        }
        turn_player.chips -= diff;
        turn_player.bet_this_round = amount;
        state_.pot += diff;
        state_.current_bet = amount;
    } else if (kind == "fold") {
        turn_player.status = PlayerStatus::Folded;
    } else {
        out.push_back(make_error(nickname, "MALFORMED_MESSAGE", "unknown action"));
        return out;
    }

    advance_turn();
    maybe_end_betting(out);
    broadcast_state(out);
    return out;
}

void IndianPoker::maybe_end_betting(OutgoingList& out) {
    int active = 0;
    int all_matched = 0;
    int last_active_idx = -1;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        const auto& p = state_.players[i];
        if (p.status == PlayerStatus::Active) {
            ++active;
            last_active_idx = static_cast<int>(i);
            if (p.bet_this_round == state_.current_bet) ++all_matched;
        }
    }
    if (active == 1) {
        // Fast path: sole survivor wins pot.
        auto& winner = state_.players[last_active_idx];
        int pot_won = state_.pot;
        winner.chips += pot_won;
        state_.pot = 0;
        enter_hand_over(out, winner.nickname, pot_won);
        return;
    }
    if (all_matched == active) {
        do_showdown(out);
    }
}

void IndianPoker::do_showdown(OutgoingList& out) {
    state_.phase = Phase::Showdown;
    int best_card = -1;
    std::vector<int> winners;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        const auto& p = state_.players[i];
        if (p.status != PlayerStatus::Active) continue;
        if (p.card > best_card) {
            best_card = p.card;
            winners.clear();
            winners.push_back(static_cast<int>(i));
        } else if (p.card == best_card) {
            winners.push_back(static_cast<int>(i));
        }
    }
    int pot = state_.pot;
    state_.pot = 0;
    int share = pot / static_cast<int>(winners.size());
    int remainder = pot - share * static_cast<int>(winners.size());
    std::string winner_nick;
    for (std::size_t i = 0; i < winners.size(); ++i) {
        auto& w = state_.players[winners[i]];
        w.chips += share;
        if (i == 0) {
            w.chips += remainder;
            winner_nick = w.nickname;
        }
    }
    enter_hand_over(out, winner_nick, pot);
}

void IndianPoker::enter_hand_over(OutgoingList& out, const std::string& winner_nick, int pot_won) {
    state_.phase = Phase::HandOver;
    // Eliminate anyone with 0 chips.
    std::vector<std::string> eliminated;
    for (auto& p : state_.players) {
        if (p.chips <= 0 && p.status != PlayerStatus::Eliminated) {
            p.status = PlayerStatus::Eliminated;
            eliminated.push_back(p.nickname);
        }
    }
    nlohmann::json revealed = nlohmann::json::object();
    for (const auto& p : state_.players) {
        if (p.card > 0 && p.status != PlayerStatus::Eliminated) {
            revealed[p.nickname] = p.card;
        }
    }
    out.push_back(Broadcast{{
        {"type", "hand_over"},
        {"hand_number", state_.hand_number},
        {"winner", winner_nick},
        {"pot_won", pot_won},
        {"revealed", revealed},
        {"eliminated_after_this_hand", eliminated},
        {"next_hand_starts_at_ms", 5000},
    }});
}

} // namespace ip::game::indian_poker
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

Expected: all action tests pass.

- [ ] **Step 4: Commit**

```bash
git add server/game/indian_poker/indian_poker.cpp tests/test_indian_poker.cpp
git commit -m "$(cat <<'EOF'
IndianPoker: check/raise/fold actions with betting round end

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8.4: Next hand and game over — TDD

**Files:**
- Modify: `server/game/indian_poker/indian_poker.cpp`
- Modify: `tests/test_indian_poker.cpp`

- [ ] **Step 1: Append tests**

```cpp
TEST_CASE("ready_next_hand from all players advances to a new hand", "[ip][handflow]") {
    IndianPoker g(make_fixed_dealer({5, 9}));
    g.start({"alice", "bob"});
    // Force a quick hand: alice folds -> bob wins.
    g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
    REQUIRE(g.state().phase == Phase::HandOver);
    g.handle_action("alice", {{"type", "ready_next_hand"}});
    g.handle_action("bob",   {{"type", "ready_next_hand"}});
    REQUIRE(g.state().phase == Phase::Betting);
    REQUIRE(g.state().hand_number == 2);
}

TEST_CASE("eliminating down to one player triggers GAME_OVER", "[ip][gameover]") {
    // alice has only 5 chips. bob can take them in one hand.
    IndianPoker g(make_fixed_dealer({5, 9}));
    g.start({"alice", "bob"});
    // Alice starts with 100. Force her to 5 via a scripted scenario:
    // for this test, just directly manipulate via consecutive folds until
    // we can't anymore. Simpler: assert behavior in a smaller-scale way with
    // a helper that shortcuts chips.
    // We add a testing hook: game_over triggers when only 1 chip-holder remains.
    // Simulate by folding many hands; alice loses 5 chips (ante) per hand.
    // 100 / 5 = 20 hands. We'll loop until game over.
    for (int i = 0; i < 25; ++i) {
        if (g.state().phase == Phase::GameOver) break;
        if (g.state().phase == Phase::Betting) {
            g.handle_action("alice", {{"type", "action"}, {"action", "fold"}});
        }
        if (g.state().phase == Phase::HandOver) {
            g.handle_action("alice", {{"type", "ready_next_hand"}});
            g.handle_action("bob",   {{"type", "ready_next_hand"}});
        }
    }
    REQUIRE(g.state().phase == Phase::GameOver);
}
```

- [ ] **Step 2: Extend `handle_action` to accept `ready_next_hand`**

Add before the "not in betting" check in `handle_action`:

```cpp
    if (action.value("type", "") == "ready_next_hand") {
        if (state_.phase != Phase::HandOver) {
            out.push_back(make_error(nickname, "INVALID_ACTION", "not in hand_over"));
            return out;
        }
        for (auto& p : state_.players) {
            if (p.nickname == nickname) p.ready_next_hand = true;
        }
        bool all_ready = true;
        int remaining_with_chips = 0;
        for (const auto& p : state_.players) {
            if (p.status == PlayerStatus::Eliminated) continue;
            if (!p.ready_next_hand) all_ready = false;
            if (p.chips > 0) ++remaining_with_chips;
        }
        if (all_ready) start_next_hand_or_finish(out);
        else broadcast_state(out);
        (void)remaining_with_chips;
        return out;
    }
```

Add `start_next_hand_or_finish`:

```cpp
void IndianPoker::start_next_hand_or_finish(OutgoingList& out) {
    int alive = 0;
    int last_idx = -1;
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        if (state_.players[i].status != PlayerStatus::Eliminated) {
            ++alive;
            last_idx = static_cast<int>(i);
        }
    }
    if (alive <= 1) {
        state_.phase = Phase::GameOver;
        std::string winner = last_idx >= 0 ? state_.players[last_idx].nickname : std::string{};
        out.push_back(Broadcast{{{"type", "game_over"}, {"winner", winner}}});
        return;
    }
    // Advance starting player by one non-eliminated seat.
    int n = static_cast<int>(state_.players.size());
    for (int step = 1; step <= n; ++step) {
        int idx = (state_.starting_player_index + step) % n;
        if (state_.players[idx].status != PlayerStatus::Eliminated) {
            state_.starting_player_index = idx;
            break;
        }
    }
    deal_new_hand();
    broadcast_state(out);
}
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add server/game/indian_poker/indian_poker.cpp tests/test_indian_poker.cpp
git commit -m "$(cat <<'EOF'
IndianPoker: ready_next_hand, next-hand advance, GAME_OVER

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M8.**

---

# Phase 9 — Milestone M9: Wire Game Logic Into Session Layer

**Outcome:** Two browser tabs can play a full hand. `start_game`, `action`, and per-player `game_state` broadcasts work end-to-end.

### Task 9.1: Add `IGame` to `Room`, handle `start_game`

**Files:**
- Modify: `server/session/room.h`
- Modify: `server/session/router.cpp`

- [ ] **Step 1: Add a game slot to `Room`**

In `server/session/room.h`, include `#include <memory>` and `#include "game/igame.h"` at the top, then add to the class:

```cpp
    ip::game::IGame* game() { return game_.get(); }
    void set_game(std::unique_ptr<ip::game::IGame> g) { game_ = std::move(g); }
```

Private member:

```cpp
    std::unique_ptr<ip::game::IGame> game_;
```

- [ ] **Step 2: Build to confirm `Room` still compiles**

```bash
cmake --build server/build -j
```

- [ ] **Step 3: Handle `start_game` in Router**

In `server/session/router.cpp`, add the handler and register it in `on_text`. At the top, add:

```cpp
#include "game/indian_poker/indian_poker.h"
#include <random>
```

Register the type in `on_text`:

```cpp
    else if (type == "start_game") handle_start_game(fd, msg);
```

Add declaration in `router.h`:

```cpp
    void handle_start_game(int fd, const nlohmann::json& msg);
    void handle_game_action(int fd, const nlohmann::json& msg);
    void fanout_game_state(Room& room, const ip::game::OutgoingList& outs);
```

Implement in `router.cpp`:

```cpp
static ip::game::indian_poker::Dealer make_random_dealer(uint64_t seed) {
    auto rng = std::make_shared<std::mt19937_64>(seed);
    return [rng](int n) {
        std::vector<int> cards;
        std::vector<int> deck = {1,2,3,4,5,6,7,8,9,10};
        std::shuffle(deck.begin(), deck.end(), *rng);
        cards.assign(deck.begin(), deck.begin() + n);
        return cards;
    };
}

void Router::handle_start_game(int fd, const nlohmann::json& /*msg*/) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) { send_error(fd, "ROOM_NOT_FOUND", "not in a room"); return; }
    Room* room = rooms_.find(it->second.room_code);
    if (!room) { send_error(fd, "ROOM_NOT_FOUND", "room gone"); return; }
    if (room->host_nickname() != it->second.nickname) {
        send_error(fd, "NOT_HOST", "only host can start");
        return;
    }
    if (room->players().size() < 2) {
        send_error(fd, "NOT_ENOUGH_PLAYERS", "need at least 2 players");
        return;
    }
    if (room->state() != RoomState::Open) {
        send_error(fd, "ROOM_IN_PROGRESS", "already started");
        return;
    }

    auto game = std::make_unique<ip::game::indian_poker::IndianPoker>(
        make_random_dealer(42 /*replace with time-based seed in production*/));
    std::vector<std::string> nicks;
    for (const auto& p : room->players()) nicks.push_back(p->nickname());

    auto outs = game->start(nicks);
    room->set_state(RoomState::Locked);
    room->set_game(std::move(game));
    fanout_game_state(*room, outs);
}

void Router::fanout_game_state(Room& room, const ip::game::OutgoingList& outs) {
    for (const auto& o : outs) {
        if (std::holds_alternative<ip::game::Private>(o)) {
            const auto& pv = std::get<ip::game::Private>(o);
            const auto* player = room.find_player(pv.nickname);
            if (player && player->connected()) send_json(player->fd(), pv.payload);
            continue;
        }
        const auto& bc = std::get<ip::game::Broadcast>(o);
        // Special marker: re-snapshot for every player.
        if (bc.payload.value("type", "") == "__game_state_fanout") {
            if (!room.game()) continue;
            for (const auto& p : room.players()) {
                if (!p->connected()) continue;
                send_json(p->fd(), room.game()->snapshot_for(p->nickname()));
            }
            continue;
        }
        // Plain broadcast: same payload to everyone connected.
        for (const auto& p : room.players()) {
            if (p->connected()) send_json(p->fd(), bc.payload);
        }
    }
}
```

- [ ] **Step 4: Build and confirm manually**

```bash
cmake --build server/build -j
./server/build/ip_server &
```

Browser: create a room with alice, join with bob, alice sends `start_game`:

```js
ws.send(JSON.stringify({type:"start_game"}));
```

Expected: both tabs receive a `game_state` with phase `BETTING`, each player's own `card` field `null`, opponent's card visible as an integer.

- [ ] **Step 5: Commit**

```bash
git add server/session/room.h server/session/router.h server/session/router.cpp
git commit -m "$(cat <<'EOF'
Wire IndianPoker into Room via start_game handler

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9.2: Route `action` and `ready_next_hand` to the game

**Files:**
- Modify: `server/session/router.cpp`

- [ ] **Step 1: Register action handlers in `on_text`**

```cpp
    else if (type == "action" || type == "ready_next_hand") handle_game_action(fd, msg);
```

- [ ] **Step 2: Implement `handle_game_action`**

```cpp
void Router::handle_game_action(int fd, const nlohmann::json& msg) {
    auto it = fd_owner_.find(fd);
    if (it == fd_owner_.end()) { send_error(fd, "ROOM_NOT_FOUND", "not in a room"); return; }
    Room* room = rooms_.find(it->second.room_code);
    if (!room || !room->game()) { send_error(fd, "INVALID_ACTION", "no game"); return; }
    auto outs = room->game()->handle_action(it->second.nickname, msg);
    fanout_game_state(*room, outs);
}
```

- [ ] **Step 3: Build and play a full hand in the browser**

Both tabs should now be able to send `{type:"action", action:"check"}`. Play a hand: alice checks, bob checks, receive `hand_over`, both send `ready_next_hand`, next hand starts.

- [ ] **Step 4: Commit**

```bash
git add server/session/router.cpp
git commit -m "$(cat <<'EOF'
Route action / ready_next_hand to IGame (milestone M9)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M9.**

---

# Phase 10 — Milestone M10: Full Game Flow End to End

**Outcome:** A complete session (multiple hands, eliminations, `game_over`) plays cleanly. Room cleanup on game end.

### Task 10.1: Clean up room on GAME_OVER

**Files:**
- Modify: `server/session/router.cpp`

- [ ] **Step 1: After `fanout_game_state`, check whether the game is finished**

In the `handle_game_action` (and `handle_start_game`) body, after the `fanout_game_state` call, add:

```cpp
    if (room->game() && room->game()->finished()) {
        // Keep the room around for a tick so clients can render; mark Closed.
        room->set_state(RoomState::Closed);
    }
```

And in the top of `on_text`, at the start, schedule cleanup of any closed rooms:

```cpp
    // Clean up closed rooms from previous tick.
    std::vector<std::string> to_erase;
    for (const auto& [code, r] : rooms_.all()) {
        if (r->state() == RoomState::Closed) to_erase.push_back(code);
    }
    for (const auto& code : to_erase) rooms_.erase(code);
```

- [ ] **Step 2: Build and play two full hands**

Manually verify GAME_OVER is broadcast and the server memory is clean (restart not required between games).

- [ ] **Step 3: Commit**

```bash
git add server/session/router.cpp
git commit -m "$(cat <<'EOF'
Clean up CLOSED rooms after GAME_OVER (milestone M10)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M10.**

---

# Phase 11 — Milestone M11: Client UI

**Outcome:** A browser-playable UI: landing page with create/join form, lobby screen, game screen with action buttons, hand-over screen, and game-over screen.

### Task 11.1: Landing page + WebSocket client

**Files:**
- Create: `client/index.html`
- Create: `client/css/style.css`
- Create: `client/js/ws_client.js`
- Create: `client/js/ui.js`
- Create: `client/js/main.js`

- [ ] **Step 1: Write `client/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Indian Poker</title>
    <link rel="stylesheet" href="css/style.css">
</head>
<body>
    <main id="app">
        <section id="landing" class="screen">
            <h1>Indian Poker</h1>
            <form id="form-create">
                <h2>Create a room</h2>
                <input name="nickname" placeholder="nickname" required maxlength="16">
                <select name="max_players">
                    <option value="2">2 players</option>
                    <option value="3">3 players</option>
                    <option value="4" selected>4 players</option>
                </select>
                <button type="submit">Create</button>
            </form>
            <form id="form-join">
                <h2>Join a room</h2>
                <input name="room_code" placeholder="ABCDEF" required maxlength="6" pattern="[A-Z2-9]{6}" autocapitalize="characters">
                <input name="nickname" placeholder="nickname" required maxlength="16">
                <button type="submit">Join</button>
            </form>
        </section>
        <section id="lobby" class="screen hidden">
            <h2>Room <span id="lobby-code"></span></h2>
            <ul id="lobby-players"></ul>
            <button id="btn-start" hidden>Start game</button>
        </section>
        <section id="game" class="screen hidden">
            <header>
                <span>Room <strong id="game-code"></strong></span>
                <span>Hand <span id="game-hand"></span></span>
                <span>Pot: <span id="game-pot">0</span></span>
            </header>
            <div id="game-players"></div>
            <div id="game-actions" class="hidden">
                <button data-action="check">Check</button>
                <button data-action="fold">Fold</button>
                <input id="raise-amount" type="number" min="0" step="5" placeholder="raise to">
                <button data-action="raise">Raise</button>
            </div>
            <div id="game-message"></div>
        </section>
    </main>
    <script type="module" src="js/main.js"></script>
</body>
</html>
```

- [ ] **Step 2: Write `client/css/style.css`**

```css
:root {
    --bg: #1e1e22;
    --fg: #eaeaea;
    --accent: #c4a86a;
    --card: #2a2a31;
    --border: #3a3a44;
}
* { box-sizing: border-box; }
html, body {
    margin: 0; padding: 0;
    background: var(--bg); color: var(--fg);
    font: 15px/1.4 -apple-system, "SF Pro Text", "Noto Sans KR", sans-serif;
}
main { max-width: 720px; margin: 0 auto; padding: 24px; }
.screen { display: block; }
.hidden { display: none !important; }
h1 { font-weight: 700; letter-spacing: -0.02em; }
h2 { font-weight: 600; margin-top: 24px; }
form { display: flex; flex-direction: column; gap: 10px; background: var(--card);
       padding: 18px; border: 1px solid var(--border); border-radius: 8px; }
input, select, button {
    font: inherit; padding: 10px 12px; border-radius: 6px; border: 1px solid var(--border);
    background: #14141a; color: var(--fg);
}
button { cursor: pointer; background: var(--accent); color: #1a1a1a; border: none; font-weight: 600; }
button:disabled { opacity: 0.5; cursor: not-allowed; }
#lobby-players { list-style: none; padding: 0; }
#lobby-players li { padding: 8px 12px; background: var(--card); border: 1px solid var(--border);
                    border-radius: 6px; margin-bottom: 6px; }
#game header { display: flex; gap: 24px; justify-content: space-between; margin-bottom: 16px; }
#game-players { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
                gap: 12px; }
.player-card { background: var(--card); border: 1px solid var(--border); border-radius: 8px;
               padding: 14px; text-align: center; }
.player-card.me { border-color: var(--accent); }
.player-card.turn { box-shadow: 0 0 0 2px var(--accent); }
.player-card .card { font-size: 42px; font-weight: 700; margin: 8px 0; }
.player-card .card.hidden-card { color: #666; }
.player-card .chips { font-size: 12px; opacity: 0.7; }
#game-actions { margin-top: 18px; display: flex; gap: 8px; flex-wrap: wrap; }
#game-actions input { flex: 1 1 120px; }
```

- [ ] **Step 3: Write `client/js/ws_client.js`**

```js
// Thin wrapper around WebSocket. Emits events by type.
const WS_URL = window.IP_WS_URL || "ws://localhost:8080/";

export class WSClient {
    constructor() {
        this.ws = null;
        this.handlers = new Map();
    }

    open() {
        this.ws = new WebSocket(WS_URL);
        this.ws.onmessage = (e) => {
            const msg = JSON.parse(e.data);
            const list = this.handlers.get(msg.type) || [];
            for (const fn of list) fn(msg);
        };
        return new Promise((resolve, reject) => {
            this.ws.onopen = () => resolve();
            this.ws.onerror = () => reject(new Error("WebSocket error"));
        });
    }

    send(obj) { this.ws.send(JSON.stringify(obj)); }

    on(type, fn) {
        if (!this.handlers.has(type)) this.handlers.set(type, []);
        this.handlers.get(type).push(fn);
    }
}
```

- [ ] **Step 4: Write `client/js/ui.js`**

```js
export function showScreen(id) {
    for (const el of document.querySelectorAll(".screen")) el.classList.add("hidden");
    document.getElementById(id).classList.remove("hidden");
}

export function renderLobby(state) {
    document.getElementById("lobby-code").textContent = state.room_code;
    const ul = document.getElementById("lobby-players");
    ul.innerHTML = "";
    for (const nick of state.players) {
        const li = document.createElement("li");
        li.textContent = nick + (nick === state.host ? " (host)" : "");
        ul.appendChild(li);
    }
}

export function renderGame(state, me) {
    document.getElementById("game-code").textContent = state.room_code;
    document.getElementById("game-hand").textContent = state.hand_number;
    document.getElementById("game-pot").textContent = state.pot;
    const container = document.getElementById("game-players");
    container.innerHTML = "";
    for (const p of state.players) {
        const div = document.createElement("div");
        div.className = "player-card";
        if (p.nickname === me) div.classList.add("me");
        if (p.nickname === state.current_turn) div.classList.add("turn");
        const card = p.card === null ? "?" : p.card;
        div.innerHTML = `
            <div><strong>${p.nickname}</strong></div>
            <div class="card ${p.card === null ? "hidden-card" : ""}">${card}</div>
            <div class="chips">${p.chips} chips · bet ${p.bet_this_round}</div>
            <div class="status">${p.status}</div>
        `;
        container.appendChild(div);
    }
    const actions = document.getElementById("game-actions");
    if (state.current_turn === me && state.phase === "BETTING") {
        actions.classList.remove("hidden");
    } else {
        actions.classList.add("hidden");
    }
}

export function setMessage(text) {
    document.getElementById("game-message").textContent = text || "";
}
```

- [ ] **Step 5: Write `client/js/main.js`**

```js
import { WSClient } from "./ws_client.js";
import { showScreen, renderLobby, renderGame, setMessage } from "./ui.js";

const ws = new WSClient();
let myNick = null;
let roomCode = null;
let latestState = null;

async function ensureOpen() {
    if (!ws.ws || ws.ws.readyState > 1) await ws.open();
}

document.getElementById("form-create").addEventListener("submit", async (e) => {
    e.preventDefault();
    const data = new FormData(e.target);
    myNick = data.get("nickname");
    await ensureOpen();
    ws.send({
        type: "create_room",
        nickname: myNick,
        max_players: parseInt(data.get("max_players"), 10),
    });
});

document.getElementById("form-join").addEventListener("submit", async (e) => {
    e.preventDefault();
    const data = new FormData(e.target);
    myNick = data.get("nickname");
    roomCode = data.get("room_code");
    await ensureOpen();
    ws.send({ type: "join_room", room_code: roomCode, nickname: myNick });
});

document.getElementById("btn-start").addEventListener("click", () => {
    ws.send({ type: "start_game" });
});

document.getElementById("game-actions").addEventListener("click", (e) => {
    const btn = e.target.closest("button[data-action]");
    if (!btn) return;
    const action = btn.dataset.action;
    if (action === "raise") {
        const amount = parseInt(document.getElementById("raise-amount").value, 10);
        ws.send({ type: "action", action: "raise", amount });
    } else {
        ws.send({ type: "action", action });
    }
});

ws.on("room_created", (msg) => {
    roomCode = msg.room_code;
    sessionStorage.setItem("ip_room_code", roomCode);
    showScreen("lobby");
    renderLobby({ room_code: roomCode, players: [myNick], host: myNick });
    document.getElementById("btn-start").hidden = false;
});

ws.on("joined_room", (msg) => {
    roomCode = msg.room_code;
    sessionStorage.setItem("ip_room_code", roomCode);
});

ws.on("lobby_update", (msg) => {
    showScreen("lobby");
    renderLobby(msg);
    document.getElementById("btn-start").hidden = (msg.host !== myNick);
});

ws.on("game_state", (msg) => {
    latestState = msg;
    showScreen("game");
    renderGame(msg, myNick);
});

ws.on("hand_over", (msg) => {
    setMessage(`Hand ${msg.hand_number}: ${msg.winner} won ${msg.pot_won}.`);
    setTimeout(() => ws.send({ type: "ready_next_hand" }), 2000);
});

ws.on("game_over", (msg) => {
    setMessage(`GAME OVER — ${msg.winner} wins!`);
});

ws.on("player_disconnected", (msg) => setMessage(`${msg.nickname} disconnected…`));
ws.on("player_reconnected", (msg) => setMessage(`${msg.nickname} reconnected.`));
ws.on("player_left",         (msg) => setMessage(`${msg.nickname} left (${msg.reason}).`));
ws.on("error",               (msg) => setMessage(`Error: ${msg.code} — ${msg.message}`));
```

- [ ] **Step 6: Manual end-to-end test**

Serve the static files: `python3 -m http.server 3000 --directory client/` in one terminal, run the C++ server in another, open `http://localhost:3000` in two tabs. Create a room, join, start game, play to completion.

- [ ] **Step 7: Commit**

```bash
git add client/
git commit -m "$(cat <<'EOF'
Add vanilla JS client UI (milestone M11)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M11.**

---

# Phase 12 — Milestone M12: Security Polish

**Outcome:** Origin check blocks requests from unexpected hosts; IP-based rate limit prevents spam; large frames are rejected.

### Task 12.1: Origin allowlist — TDD

**Files:**
- Create: `server/util/origin.h`
- Create: `server/util/origin.cpp`
- Modify: `tests/test_ws_handshake.cpp`

- [ ] **Step 1: Append to `tests/test_ws_handshake.cpp`**

```cpp
#include "util/origin.h"

TEST_CASE("origin allowlist", "[origin]") {
    ip::util::OriginAllowlist a{{"https://example.vercel.app", "http://localhost:3000"}};
    REQUIRE(a.allows("https://example.vercel.app"));
    REQUIRE(a.allows("http://localhost:3000"));
    REQUIRE_FALSE(a.allows("https://evil.com"));
    REQUIRE_FALSE(a.allows(""));
}
```

- [ ] **Step 2: Write `server/util/origin.h`**

```cpp
#pragma once
#include <string>
#include <unordered_set>
#include <vector>

namespace ip::util {

class OriginAllowlist {
public:
    OriginAllowlist(std::vector<std::string> allowed);
    bool allows(const std::string& origin) const;

private:
    std::unordered_set<std::string> allowed_;
};

} // namespace ip::util
```

- [ ] **Step 3: Write `server/util/origin.cpp`**

```cpp
#include "util/origin.h"

namespace ip::util {

OriginAllowlist::OriginAllowlist(std::vector<std::string> allowed)
    : allowed_(allowed.begin(), allowed.end()) {}

bool OriginAllowlist::allows(const std::string& origin) const {
    if (origin.empty()) return false;
    return allowed_.count(origin) > 0;
}

} // namespace ip::util
```

- [ ] **Step 4: Add to `ip_core`, build, test**

```cmake
    util/origin.cpp
```

- [ ] **Step 5: Check origin in `main.cpp`**

After `parse_handshake` returns `Complete`, check:

```cpp
static const ip::util::OriginAllowlist kOrigins({
    "http://localhost:3000",
    "http://localhost:8080",
    // Production Vercel URL goes here at deploy time.
});

// ...
if (!kOrigins.allows(r.origin)) {
    loop_ptr->send_to(fd, "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
    loop_ptr->close(fd);
    return;
}
```

- [ ] **Step 6: Build, test, commit**

```bash
cmake --build server/build -j && (cd server/build && ctest --output-on-failure)
git add server/util/origin.h server/util/origin.cpp tests/test_ws_handshake.cpp server/main.cpp server/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add Origin allowlist check to handshake (milestone M12)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 12.2: IP-based rate limit + message size cap

**Files:**
- Modify: `server/session/router.cpp`
- Modify: `server/net/ws_frame.cpp`

- [ ] **Step 1: Reject messages larger than 4 KB in the decoder**

In `parse_one_frame`, after computing `payload_len`:

```cpp
    if (payload_len > 4096) { err = "payload too large"; return RawStatus::Error; }
```

- [ ] **Step 2: Simple rate limit — each fd gets at most 10 create/join attempts per 60s**

In `Router`, add a small structure `std::unordered_map<int, std::vector<uint64_t>> attempts_`, update in `handle_create_room` and `handle_join_room` at the top:

```cpp
    constexpr uint64_t kWindowMs = 60000;
    constexpr std::size_t kMax = 10;
    auto& v = attempts_[fd];
    uint64_t now = 0; // passed in from main's time source — see step 3
    v.erase(std::remove_if(v.begin(), v.end(),
        [&](uint64_t t){ return now - t > kWindowMs; }), v.end());
    if (v.size() >= kMax) { send_error(fd, "RATE_LIMITED", "too many attempts"); return; }
    v.push_back(now);
```

For this to know "now" without global state, add a member `uint64_t current_tick_ms_ = 0;` and set it at the top of `on_text`:

```cpp
void Router::on_text(int fd, const std::string& text) {
    current_tick_ms_ = ...; // main passes now_ms via a setter, or pass as arg.
    ...
}
```

Simpler: add a parameter `on_text(int fd, const std::string& text, uint64_t now_ms)` and thread `now_ms` through `main.cpp`.

- [ ] **Step 3: Update signatures and callers accordingly, build, test**

- [ ] **Step 4: Commit**

```bash
git add server/net/ws_frame.cpp server/session/router.h server/session/router.cpp server/main.cpp
git commit -m "$(cat <<'EOF'
Add 4KB frame cap and per-fd rate limit for create/join

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

**End of Milestone M12.**

---

# Phase 13 — Milestone M13: Deployment

**Outcome:** Frontend on Vercel, backend exposed via Cloudflare Tunnel. Real remote playtest works.

### Task 13.1: Vercel config for static client

**Files:**
- Create: `deploy/vercel.json`

- [ ] **Step 1: Write `deploy/vercel.json`**

```json
{
  "$schema": "https://openapi.vercel.sh/vercel.json",
  "version": 2,
  "cleanUrls": true,
  "public": true,
  "buildCommand": null,
  "outputDirectory": "client"
}
```

- [ ] **Step 2: Commit**

```bash
git add deploy/vercel.json
git commit -m "$(cat <<'EOF'
Add vercel.json pointing at client/ as the static root

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13.2: Cloudflare Tunnel config template

**Files:**
- Create: `deploy/cloudflared-config.example.yml`

- [ ] **Step 1: Write `deploy/cloudflared-config.example.yml`**

```yaml
# Copy this to ~/.cloudflared/config.yml and replace the placeholders.
tunnel: REPLACE_WITH_YOUR_TUNNEL_ID
credentials-file: /Users/you/.cloudflared/REPLACE_WITH_YOUR_TUNNEL_ID.json

ingress:
  - hostname: game.REPLACE_WITH_YOUR_DOMAIN
    service: http://localhost:8080
    originRequest:
      noTLSVerify: true
      connectTimeout: 30s
  - service: http_status:404
```

- [ ] **Step 2: Commit**

```bash
git add deploy/cloudflared-config.example.yml
git commit -m "$(cat <<'EOF'
Add cloudflared ingress template

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13.3: Bake WSS URL into client via a config file

**Files:**
- Create: `client/js/config.js`
- Modify: `client/js/ws_client.js`
- Modify: `client/index.html`

- [ ] **Step 1: Create `client/js/config.js`**

```js
// Set this to your Cloudflare Tunnel subdomain before deploy.
// For local dev, the file is overridden by setting window.IP_WS_URL earlier.
window.IP_WS_URL = window.IP_WS_URL || "wss://game.REPLACE_WITH_YOUR_DOMAIN/";
```

- [ ] **Step 2: Include it before `main.js` in `index.html`**

```html
    <script src="js/config.js"></script>
    <script type="module" src="js/main.js"></script>
```

- [ ] **Step 3: Commit**

```bash
git add client/js/config.js client/index.html
git commit -m "$(cat <<'EOF'
Add config.js for WSS URL override

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 13.4: First real remote playtest

- [ ] **Step 1: Deploy the static client**

```bash
# In deploy/ or repo root, depending on Vercel project config
npx vercel --prod
```

Verify the site loads and the create/join forms render.

- [ ] **Step 2: Start `cloudflared` and the C++ server locally**

```bash
./server/build/ip_server &
cloudflared tunnel run
```

- [ ] **Step 3: Test from two devices**

Open the Vercel URL on a laptop and a phone. Create a room on one, join on the other using the displayed 6-character code. Play through at least one full hand. Intentionally drop the phone's WiFi during a betting turn — confirm the other tab shows `player_disconnected`; reconnect within 15 s and confirm the phone re-joins and the game continues.

- [ ] **Step 4: (No code change needed.) Commit a deployment note if anything changed in config files.**

**End of Milestone M13.**

---

## Self-Review Checklist (for the plan author)

After reaching this point during implementation, re-read the plan against [`PROJECT.md`](../../PROJECT.md) and verify:

- [ ] Every message type in §4 of PROJECT.md is either implemented or explicitly deferred.
- [ ] Every error code in §4.4 is emitted from at least one code path.
- [ ] The reconnect flow matches the invariants in §6.4 (reconnect only with same nickname; nickname collisions rejected; LOCKED rooms reject new players).
- [ ] The BETTING round-end condition handles both "all matched" and "only one active left" cases.
- [ ] Tie-breaking on showdown awards the remainder chip to the earliest seat.
- [ ] Tests cover at minimum: WebSocket frame decoding (including fragmentation and control frame interleaving), handshake accept computation, game-logic edge cases (all-but-one fold, tie split, elimination, game over), and the join/reconnect/timeout flow.

---

## Deferred Items

The plan implements the MVP scope of PROJECT.md. The following items from PROJECT.md §13 are intentionally out of scope for this plan and can be addressed in follow-up plans once this plan lands:

- Second `IGame` implementation to validate the 3-layer separation.
- Spectator mode for eliminated players.
- Turn timer enforcement.
- Structured server-side logging.
- `localStorage`-based reconnection across browser restarts.
- Backend Dockerization.
- In-process TLS in the C++ server.

---

*End of implementation plan.*






