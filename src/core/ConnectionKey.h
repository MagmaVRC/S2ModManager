#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core {

/// <summary>An Ed25519 key pair, stored as raw 32-byte halves.</summary>
struct KeyPair {
    std::array<std::uint8_t, 32> publicKey{};
    std::array<std::uint8_t, 32> privateKey{};
};

/// <summary>The contents carried by a connection key: where to reach the host and
/// the public key used to authenticate it.</summary>
struct ConnectionKeyData {
    std::string                  externalIp;   // dotted IPv4
    std::uint16_t                port = 0;
    std::array<std::uint8_t, 32> publicKey{};
};

/// <summary>Generates a fresh Ed25519 key pair. Returns false on OpenSSL failure.</summary>
[[nodiscard]] bool generateKeyPair(KeyPair& out);

/// <summary>Signs <paramref name="message"/> with a raw Ed25519 private key.</summary>
/// <returns>The 64-byte signature, or empty on failure.</returns>
[[nodiscard]] std::vector<std::uint8_t> sign(const std::array<std::uint8_t, 32>& privateKey,
                                             const std::vector<std::uint8_t>& message);

/// <summary>Verifies an Ed25519 signature against a raw public key.</summary>
[[nodiscard]] bool verify(const std::array<std::uint8_t, 32>& publicKey,
                          const std::vector<std::uint8_t>& message,
                          const std::vector<std::uint8_t>& signature);

/// <summary>Encodes connection details into the copy-paste key string
/// (base64 of a magic-gated, CRC-checked binary blob).</summary>
[[nodiscard]] std::string encodeConnectionKey(const ConnectionKeyData& data);

/// <summary>Parses and validates a connection key string. Rejects bad magic,
/// version, length, or CRC. Whitespace is ignored.</summary>
[[nodiscard]] std::optional<ConnectionKeyData> parseConnectionKey(std::string_view text);

}  // namespace core
