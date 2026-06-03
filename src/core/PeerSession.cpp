#include "PeerSession.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <string>

namespace core {
namespace {

// Reject absurd frame sizes so a malformed/hostile length can't trigger a huge alloc.
constexpr std::uint32_t kMaxFrame = 3u * 1024u * 1024u * 1024u;  // 3 GiB

std::string lastWsaError(const char* what) {
    return std::string(what) + " (WSA error " + std::to_string(WSAGetLastError()) + ")";
}

}  // namespace

PeerSession::PeerSession()
    : listenSock_(INVALID_SOCKET), sock_(INVALID_SOCKET) {
    WSADATA wsa{};
    wsaUp_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

PeerSession::~PeerSession() {
    close();
    if (wsaUp_)
        WSACleanup();
}

void PeerSession::closeConnection() {
    if (sock_ != INVALID_SOCKET) {
        ::closesocket(static_cast<SOCKET>(sock_));
        sock_ = INVALID_SOCKET;
    }
}

void PeerSession::close() {
    closeConnection();
    if (listenSock_ != INVALID_SOCKET) {
        ::closesocket(static_cast<SOCKET>(listenSock_));
        listenSock_ = INVALID_SOCKET;
    }
}

bool PeerSession::listen(std::uint16_t port, std::string& err) {
    if (!wsaUp_) { err = "Winsock failed to initialize."; return false; }

    SOCKET ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { err = lastWsaError("socket"); return false; }
    listenSock_ = ls;

    BOOL reuse = TRUE;
    ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        err = lastWsaError("bind"); return false;
    }
    if (::listen(ls, 1) == SOCKET_ERROR) {
        err = lastWsaError("listen"); return false;
    }
    return true;
}

PeerSession::Accepted PeerSession::accept(int timeoutMs, std::string& err) {
    if (listenSock_ == INVALID_SOCKET) { err = "Not listening."; return Accepted::Error; }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(static_cast<SOCKET>(listenSock_), &rfds);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
    if (sel == 0) return Accepted::TimedOut;
    if (sel == SOCKET_ERROR) { err = lastWsaError("select"); return Accepted::Error; }

    SOCKET cs = ::accept(static_cast<SOCKET>(listenSock_), nullptr, nullptr);
    if (cs == INVALID_SOCKET) { err = lastWsaError("accept"); return Accepted::Error; }
    sock_ = cs;

    DWORD rwTimeout = 30000;
    ::setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rwTimeout), sizeof(rwTimeout));
    ::setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&rwTimeout), sizeof(rwTimeout));
    return Accepted::Connection;
}

bool PeerSession::connectTo(const std::string& ip, std::uint16_t port, int timeoutMs, std::string& err) {
    if (!wsaUp_) { err = "Winsock failed to initialize."; return false; }

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = lastWsaError("socket"); return false; }
    sock_ = s;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        err = "Malformed host address."; return false;
    }

    u_long nonblocking = 1;
    ::ioctlsocket(s, FIONBIO, &nonblocking);
    int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        err = lastWsaError("connect"); return false;
    }

    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(s, &wfds);
    FD_ZERO(&efds); FD_SET(s, &efds);   // Windows reports a failed connect in the except set, not write
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int sel = ::select(0, nullptr, &wfds, &efds, &tv);
    if (sel == 0) { err = "Could not reach the host (connection timed out)."; return false; }
    if (sel == SOCKET_ERROR) { err = lastWsaError("select"); return false; }

    int soerr = 0; int len = sizeof(soerr);
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &len);
    if (FD_ISSET(s, &efds) || soerr != 0) { err = "Host refused or unreachable."; return false; }

    u_long blocking = 0;
    ::ioctlsocket(s, FIONBIO, &blocking);

    // Cap blocking I/O so a stalled peer cannot hang the worker forever.
    DWORD rwTimeout = 30000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rwTimeout), sizeof(rwTimeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&rwTimeout), sizeof(rwTimeout));
    return true;
}

bool PeerSession::sendAll(const std::uint8_t* data, std::size_t len, std::string& err) {
    std::size_t sent = 0;
    while (sent < len) {
        int n = ::send(static_cast<SOCKET>(sock_),
                       reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(std::min<std::size_t>(len - sent, 1 << 20)), 0);
        if (n == SOCKET_ERROR) { err = lastWsaError("send"); return false; }
        if (n == 0) { err = "Connection closed during send."; return false; }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool PeerSession::recvAll(std::uint8_t* data, std::size_t len, std::string& err) {
    std::size_t got = 0;
    while (got < len) {
        int n = ::recv(static_cast<SOCKET>(sock_),
                       reinterpret_cast<char*>(data + got),
                       static_cast<int>(std::min<std::size_t>(len - got, 1 << 20)), 0);
        if (n == SOCKET_ERROR) { err = lastWsaError("recv"); return false; }
        if (n == 0) { err = "Connection closed by peer."; return false; }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

bool PeerSession::sendFrame(const Bytes& payload, std::string& err) {
    if (payload.size() > kMaxFrame) { err = "Frame too large to send."; return false; }
    std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    std::uint8_t hdr[4] = { static_cast<std::uint8_t>(len >> 24),
                            static_cast<std::uint8_t>(len >> 16),
                            static_cast<std::uint8_t>(len >> 8),
                            static_cast<std::uint8_t>(len) };
    if (!sendAll(hdr, 4, err)) return false;
    if (payload.empty()) return true;
    return sendAll(payload.data(), payload.size(), err);
}

bool PeerSession::recvFrame(Bytes& out, std::uint32_t maxLen, std::string& err) {
    std::uint8_t hdr[4];
    if (!recvAll(hdr, 4, err)) return false;
    std::uint32_t len = (static_cast<std::uint32_t>(hdr[0]) << 24) |
                        (static_cast<std::uint32_t>(hdr[1]) << 16) |
                        (static_cast<std::uint32_t>(hdr[2]) << 8) |
                        static_cast<std::uint32_t>(hdr[3]);
    if (len > maxLen) { err = "Peer announced an oversized frame."; return false; }
    out.resize(len);
    if (len == 0) return true;
    return recvAll(out.data(), len, err);
}

bool PeerSession::recvFrame(Bytes& out, std::string& err) {
    return recvFrame(out, kMaxFrame, err);
}

}  // namespace core
