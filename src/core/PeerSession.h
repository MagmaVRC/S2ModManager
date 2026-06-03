#pragma once
#include "Compression.h"

#include <cstdint>
#include <string>

namespace core {

/// <summary>A single TCP connection between two peers, with length-prefixed framing.
/// Pure transport: it knows nothing about profiles or crypto. One instance owns one
/// connection plus (host side) its listening socket.</summary>
class PeerSession {
public:
    PeerSession();
    ~PeerSession();

    PeerSession(const PeerSession&) = delete;
    PeerSession& operator=(const PeerSession&) = delete;

    /// <summary>Outcome of an <see cref="accept"/> call.</summary>
    enum class Accepted { Connection, TimedOut, Error };

    /// <summary>Binds and starts listening on the port. Call once, then <see cref="accept"/>.</summary>
    [[nodiscard]] bool listen(std::uint16_t port, std::string& err);

    /// <summary>Waits up to <paramref name="timeoutMs"/> for a peer. On
    /// <c>Connection</c> the accepted socket becomes the active connection.</summary>
    [[nodiscard]] Accepted accept(int timeoutMs, std::string& err);

    /// <summary>Connects to a host, with a connect timeout.</summary>
    [[nodiscard]] bool connectTo(const std::string& ip, std::uint16_t port, int timeoutMs, std::string& err);

    /// <summary>Closes only the active connection, leaving any listener open to accept again.</summary>
    void closeConnection();

    /// <summary>Sends one frame: 4-byte big-endian length followed by the payload.</summary>
    [[nodiscard]] bool sendFrame(const Bytes& payload, std::string& err);

    /// <summary>Receives one frame, rejecting any longer than <paramref name="maxLen"/> bytes.</summary>
    [[nodiscard]] bool recvFrame(Bytes& out, std::uint32_t maxLen, std::string& err);

    /// <summary>Receives one frame, bounded by the default maximum frame size.</summary>
    [[nodiscard]] bool recvFrame(Bytes& out, std::string& err);

    /// <summary>Closes the connection (and the listener, if any).</summary>
    void close();

private:
    bool sendAll(const std::uint8_t* data, std::size_t len, std::string& err);
    bool recvAll(std::uint8_t* data, std::size_t len, std::string& err);

    std::uintptr_t listenSock_;  // SOCKET; INVALID_SOCKET when unused
    std::uintptr_t sock_;        // SOCKET; the active connection
    bool wsaUp_ = false;
};

}  // namespace core
