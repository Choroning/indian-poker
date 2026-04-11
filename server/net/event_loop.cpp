#include "net/event_loop.h"

#include <sys/select.h>
#include <sys/time.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

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

    for (auto& [cfd, c] : conns_) {
        FD_SET(cfd, &rfds);
        if (c->wants_write()) FD_SET(cfd, &wfds);
        if (cfd > maxfd) maxfd = cfd;
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
            int new_fd = listener_.accept_one();
            if (new_fd < 0) break;
            conns_.emplace(new_fd, std::make_unique<Connection>(new_fd));
            if (on_connect_) on_connect_(new_fd);
        }
    }

    std::vector<int> to_erase;
    for (auto& [cfd, c] : conns_) {
        if (FD_ISSET(cfd, &wfds)) c->flush();

        if (FD_ISSET(cfd, &rfds)) {
            ssize_t r = c->read_some();
            if (r == 0) {
                if (on_disconnect_) on_disconnect_(cfd);
                to_erase.push_back(cfd);
                continue;
            }
            if (r > 0) {
                auto chunk = c->take_input();
                if (on_data_) on_data_(cfd, chunk);
            }
        }

        if (c->closed()) {
            if (on_disconnect_) on_disconnect_(cfd);
            to_erase.push_back(cfd);
        }
    }

    for (int erased_fd : to_erase) conns_.erase(erased_fd);
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
