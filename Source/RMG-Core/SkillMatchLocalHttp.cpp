#include "SkillMatchLocalHttp.hpp"
#include <chrono>
#include <cctype>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket InvalidSocket = INVALID_SOCKET;
static void closeSocket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
static constexpr Socket InvalidSocket = -1;
static void closeSocket(Socket s) { close(s); }
#endif

namespace {
using Clock = std::chrono::steady_clock;
bool waitSocket(Socket s, bool write, Clock::time_point deadline)
{
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now());
    if (remaining.count() <= 0) return false;
    timeval tv{static_cast<long>(remaining.count() / 1000000), static_cast<long>(remaining.count() % 1000000)};
    fd_set set; FD_ZERO(&set); FD_SET(s, &set);
    return select(static_cast<int>(s + 1), write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}
}

SkillMatchOutbox::Delivery SkillMatchPostLocal(uint16_t port, const std::string& id, const std::string& payload)
{
    using Delivery = SkillMatchOutbox::Delivery;
    if (!port || id.empty() || id.size() > 128 || payload.empty() || payload.size() > 16384) return Delivery::Reject;
    for (unsigned char c : id) if (!(std::isalnum(c) || c == '-' || c == '_')) return Delivery::Reject;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return Delivery::Retry;
    struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
#endif
    Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == InvalidSocket) return Delivery::Retry;
    struct Guard { Socket s; ~Guard() { closeSocket(s); } } guard{s};
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonblocking) != 0) return Delivery::Retry;
#else
    if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) return Delivery::Retry;
#endif
    const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (!waitSocket(s, true, deadline)) return Delivery::Retry;
    int error = 0;
#ifdef _WIN32
    int len = sizeof(error);
#else
    socklen_t len = sizeof(error);
#endif
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0 || error) return Delivery::Retry;
    const std::string request = "POST /observations HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Type: application/json\r\nIdempotency-Key: " + id + "\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
    size_t sent = 0;
    while (sent < request.size()) {
        if (!waitSocket(s, true, deadline)) return Delivery::Retry;
#ifdef _WIN32
        int n = send(s, request.data() + sent, static_cast<int>(request.size() - sent), 0);
#else
        int n = send(s, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return Delivery::Retry;
        sent += static_cast<size_t>(n);
    }
    std::string response; char buf[512];
    while (response.find("\r\n\r\n") == std::string::npos && response.size() < 4096) {
        if (!waitSocket(s, false, deadline)) return Delivery::Retry;
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return Delivery::Retry;
        response.append(buf, n);
    }
    if (response.size() > 4096 || response.find("\r\n\r\n") == std::string::npos) return Delivery::Reject;
    int status = 0; char version[9]{};
    if (std::sscanf(response.c_str(), "%8s %d", version, &status) != 2 ||
        (std::string(version) != "HTTP/1.1" && std::string(version) != "HTTP/1.0")) return Delivery::Reject;
    if (status == 204) return Delivery::Ack;
    if (status == 429 || (status >= 500 && status <= 599)) return Delivery::Retry;
    return Delivery::Reject;
}
