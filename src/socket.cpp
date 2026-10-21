#include "nettrace/socket.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace nettrace {
namespace {
void runtime() {
#ifdef _WIN32
    struct Runtime {
        Runtime() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw IoError("WSAStartup failed"); }
        ~Runtime() { WSACleanup(); }
    };
    static Runtime instance;
    (void)instance;
#endif
}
int error_code() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
bool interrupted(int e) {
#ifdef _WIN32
    return e == WSAEINTR;
#else
    return e == EINTR;
#endif
}
bool would_block(int e) {
#ifdef _WIN32
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
#endif
}
[[noreturn]] void fail(const char* op, int e = error_code()) { throw IoError(std::string(op) + " failed (socket error " + std::to_string(e) + ")"); }
void nonblocking(NativeSocket fd) {
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode)) fail("ioctlsocket");
#else
    const auto flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) fail("fcntl");
#endif
}
Socket make_socket() {
    runtime();
    Socket s(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (s.native() == invalid_socket) fail("socket");
    nonblocking(s.native());
    return s;
}
sockaddr_in address(const std::string& host, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) throw IoError("host must be a numeric IPv4 address");
    return addr;
}
void wait_ready(NativeSocket fd, bool write, Deadline deadline) {
#ifndef _WIN32
    if (fd >= FD_SETSIZE) throw IoError("socket exceeds select FD_SETSIZE");
#endif
    for (;;) {
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) throw Timeout("operation deadline expired");
        timeval tv{};
        tv.tv_sec = static_cast<long>(remaining / 1000000);
        tv.tv_usec = static_cast<long>(remaining % 1000000);
        fd_set set;
        fd_set errors;
        FD_ZERO(&set);
        FD_ZERO(&errors);
        FD_SET(fd, &set);
        FD_SET(fd, &errors);
#ifdef _WIN32
        const int nfds = 0;
#else
        const int nfds = fd + 1;
#endif
        const auto n = select(nfds, write ? nullptr : &set, write ? &set : nullptr, &errors, &tv);
        if (n > 0) return;
        // Some platforms round select timeouts down. Recheck the steady clock.
        if (n == 0) continue;
        if (!interrupted(error_code())) fail("select");
    }
}
Bytes receive_exact(const Socket& socket, std::size_t count, Deadline deadline) {
    Bytes b(count);
    std::size_t offset = 0;
    while (offset < count) {
        wait_ready(socket.native(), false, deadline);
        const auto n = recv(socket.native(), reinterpret_cast<char*>(b.data() + offset), static_cast<int>(count - offset), 0);
        if (n == 0) throw Disconnected(offset ? "peer closed mid-frame" : "peer closed connection");
        if (n < 0) {
            const auto e = error_code();
            if (would_block(e) || interrupted(e)) continue;
            fail("recv", e);
        }
        offset += static_cast<std::size_t>(n);
    }
    return b;
}
}
Socket::~Socket() { close(); }
void Socket::close() {
    if (fd_ == invalid_socket) return;
#ifdef _WIN32
    closesocket(fd_);
#else
    ::close(fd_);
#endif
    fd_ = invalid_socket;
}
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) { close(); fd_ = std::exchange(other.fd_, invalid_socket); }
    return *this;
}
Socket connect_tcp(const std::string& host, std::uint16_t port, Deadline deadline) {
    auto s = make_socket();
    const auto addr = address(host, port);
    if (::connect(s.native(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        const auto e = error_code();
        if (!would_block(e)) fail("connect", e);
        wait_ready(s.native(), true, deadline);
        int status = 0;
#ifdef _WIN32
        int size = sizeof(status);
#else
        socklen_t size = sizeof(status);
#endif
        if (getsockopt(s.native(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&status), &size)) fail("getsockopt");
        if (status) fail("connect", status);
    }
    return s;
}
Socket listen_tcp(const std::string& host, std::uint16_t port) {
    auto s = make_socket();
    int yes = 1;
#ifdef _WIN32
    if (setsockopt(s.native(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes))) fail("setsockopt");
#else
    if (setsockopt(s.native(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) fail("setsockopt");
#endif
    const auto addr = address(host, port);
    if (bind(s.native(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr))) fail("bind");
    if (listen(s.native(), 16)) fail("listen");
    return s;
}
std::uint16_t local_port(const Socket& s) {
    sockaddr_in addr{};
#ifdef _WIN32
    int size = sizeof(addr);
#else
    socklen_t size = sizeof(addr);
#endif
    if (getsockname(s.native(), reinterpret_cast<sockaddr*>(&addr), &size)) fail("getsockname");
    return ntohs(addr.sin_port);
}
Socket accept_tcp(const Socket& listener, Deadline deadline) {
    for (;;) {
        wait_ready(listener.native(), false, deadline);
        Socket s(accept(listener.native(), nullptr, nullptr));
        if (s.native() != invalid_socket) { nonblocking(s.native()); return s; }
        if (!would_block(error_code()) && !interrupted(error_code())) fail("accept");
    }
}
void send_all(const Socket& s, const Bytes& b, Deadline deadline) {
    std::size_t offset = 0;
    while (offset < b.size()) {
        wait_ready(s.native(), true, deadline);
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto n = send(s.native(), reinterpret_cast<const char*>(b.data() + offset), static_cast<int>(std::min<std::size_t>(INT_MAX, b.size() - offset)), flags);
        if (n == 0) throw Disconnected("zero-byte send");
        if (n < 0) {
            const auto e = error_code();
            if (would_block(e) || interrupted(e)) continue;
            fail("send", e);
        }
        offset += static_cast<std::size_t>(n);
    }
}
Message receive_message(const Socket& s, Deadline deadline) {
    auto b = receive_exact(s, header_size, deadline);
    const auto length = payload_length(b);
    auto payload = receive_exact(s, length, deadline);
    b.insert(b.end(), payload.begin(), payload.end());
    return decode(b);
}
}
