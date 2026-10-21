#pragma once
#include "nettrace/protocol.hpp"
#include <chrono>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace nettrace {
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
inline Deadline after_ms(int ms) { return Clock::now() + std::chrono::milliseconds(ms); }
class IoError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class Timeout : public IoError { public: using IoError::IoError; };
class Disconnected : public IoError { public: using IoError::IoError; };
class Socket {
public:
    explicit Socket(NativeSocket fd = invalid_socket) : fd_(fd) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, invalid_socket)) {}
    Socket& operator=(Socket&& other) noexcept;
    NativeSocket native() const { return fd_; }
    void close();
private:
    NativeSocket fd_;
};
Socket connect_tcp(const std::string& host, std::uint16_t port, Deadline deadline);
Socket listen_tcp(const std::string& host, std::uint16_t port);
std::uint16_t local_port(const Socket& socket);
Socket accept_tcp(const Socket& listener, Deadline deadline);
void send_all(const Socket& socket, const Bytes& data, Deadline deadline);
Message receive_message(const Socket& socket, Deadline deadline);
}
