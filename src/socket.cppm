module;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#endif

export module mcpplibs.tinyhttps:socket;

import std;
import :platform;

namespace mcpplibs::tinyhttps {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_FD = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_FD = -1;
#endif

export class Socket {
public:
    Socket() = default;

    ~Socket() {
        close();
    }

    // Non-copyable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Move constructor
    Socket(Socket&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = INVALID_SOCKET_FD;
    }

    // Move assignment
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = INVALID_SOCKET_FD;
        }
        return *this;
    }

    [[nodiscard]] bool is_valid() const {
        return fd_ != INVALID_SOCKET_FD;
    }

    bool connect(const char* host, int port, int timeoutMs) {
        // Close existing connection if any
        if (is_valid()) {
            close();
        }

        auto portStr = std::to_string(port);

        // Resolve via the system resolver (getaddrinfo). On Termux/Android a
        // musl-static build can't — its nameservers live in $PREFIX/etc/resolv.conf
        // which libc never reads — so fall back to a manual DNS query there.
        auto try_resolved = [&](const char* node, bool numeric) -> bool {
            struct addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            if (numeric) hints.ai_flags = AI_NUMERICHOST;

            struct addrinfo* result = nullptr;
            if (::getaddrinfo(node, portStr.c_str(), &hints, &result) != 0 || result == nullptr) {
                return false;
            }
            bool ok = connect_addrinfo(result, timeoutMs);
            ::freeaddrinfo(result);
            return ok;
        };

        if constexpr (platform::is_windows) {
            return try_resolved(host, /*numeric=*/false);
        } else {
            // Fall back to a manual DNS query when libc can't resolve (Termux:
            // nameservers live in $PREFIX/etc/resolv.conf, which libc ignores).
            auto try_manual = [&]() -> bool {
                // DNS must be snappy: a UDP query to a working resolver answers
                // in well under a second. Cap it hard (independent of the much
                // larger connect timeout) so an intermittently-dropped packet to
                // 8.8.8.8 can't stall a connect for tens of seconds per host.
                // Plain ternary, not std::min: <winsock2.h> defines a `min`
                // macro that would mangle std::min on the (compiled-but-discarded)
                // Windows branch of this if constexpr.
                constexpr int kDnsTimeoutMs = 2500;
                int dnsTimeout = (timeoutMs > 0 && timeoutMs < kDnsTimeoutMs)
                                     ? timeoutMs : kDnsTimeoutMs;
                for (const auto& ip : platform::resolve_fallback(host, dnsTimeout)) {
                    if (try_resolved(ip.c_str(), /*numeric=*/true)) return true;
                }
                return false;
            };

            // No libc resolver config but a relocatable one exists → resolve
            // manually first to avoid a multi-second stall on a dead 127.0.0.1:53.
            if (!platform::system_resolver_configured()) {
                return try_manual() || try_resolved(host, /*numeric=*/false);
            }
            return try_resolved(host, /*numeric=*/false) || try_manual();
        }
    }

    // Connect to the first reachable address in a resolved list.
    bool connect_addrinfo(struct addrinfo* result, int timeoutMs) {
        for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
            SocketHandle fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == INVALID_SOCKET_FD) {
                continue;
            }

            // Set non-blocking
            if (!set_non_blocking(fd, true)) {
                close_handle(fd);
                continue;
            }

            int rc = ::connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen));

            bool connected = false;
            if (rc == 0) {
                connected = true;
            } else {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
                if (errno == EINPROGRESS) {
#endif
                    // Wait for connection with timeout
                    if (poll_fd(fd, timeoutMs, false)) {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0 && err == 0) {
                            connected = true;
                        }
                    }
                }
            }

            if (connected) {
                // Restore blocking mode
                set_non_blocking(fd, false);
                fd_ = fd;
                return true;
            }

            close_handle(fd);
        }

        return false;
    }

    int read(char* buf, int len) {
        if (!is_valid()) return -1;
        return static_cast<int>(::recv(fd_, buf, len, 0));
    }

    int write(const char* buf, int len) {
        if (!is_valid()) return -1;
        return static_cast<int>(::send(fd_, buf, len, 0));
    }

    bool wait_readable(int timeoutMs) {
        if (!is_valid()) return false;
        return poll_fd(fd_, timeoutMs, true);
    }

    bool wait_writable(int timeoutMs) {
        if (!is_valid()) return false;
        return poll_fd(fd_, timeoutMs, false);
    }

    [[nodiscard]] SocketHandle native_handle() const {
        return fd_;
    }

    void close() {
        if (is_valid()) {
            close_handle(fd_);
            fd_ = INVALID_SOCKET_FD;
        }
    }

    static void platform_init() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    static void platform_cleanup() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

private:
    SocketHandle fd_ = INVALID_SOCKET_FD;

    static bool set_non_blocking(SocketHandle fd, bool nonBlocking) {
#ifdef _WIN32
        u_long mode = nonBlocking ? 1 : 0;
        return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1) return false;
        if (nonBlocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return ::fcntl(fd, F_SETFL, flags) == 0;
#endif
    }

    static bool poll_fd(SocketHandle fd, int timeoutMs, bool forRead) {
#ifdef _WIN32
        WSAPOLLFD pfd{};
        pfd.fd = fd;
        pfd.events = forRead ? POLLIN : POLLOUT;
        int ret = WSAPoll(&pfd, 1, timeoutMs);
        return ret > 0 && (pfd.revents & (pfd.events | POLLERR | POLLHUP));
#else
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = forRead ? POLLIN : POLLOUT;
        int ret = ::poll(&pfd, 1, timeoutMs);
        return ret > 0 && (pfd.revents & (pfd.events | POLLERR | POLLHUP));
#endif
    }

    static void close_handle(SocketHandle fd) {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
    }
};

} // namespace mcpplibs::tinyhttps
