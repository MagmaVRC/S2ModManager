#include "ConnectionKey.h"
#include "Compression.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace core {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = { 'S', '2', 'M', 'P' };
constexpr std::uint8_t kVersion = 1;
// magic(4) + version(1) + ipv4(4) + port(2) + pubkey(32) + crc(4)
constexpr std::size_t kBlobSize = 4 + 1 + 4 + 2 + 32 + 4;

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const Bytes& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        std::uint32_t n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += kB64[n & 63];
    }
    if (std::size_t rem = in.size() - i; rem == 1) {
        std::uint32_t n = in[i] << 16;
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        std::uint32_t n = (in[i] << 16) | (in[i + 1] << 8);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool base64Decode(const std::string& in, Bytes& out) {
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        int v = b64Value(c);
        if (v < 0)
            return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

bool parseIpv4(const std::string& ip, std::array<std::uint8_t, 4>& out) {
    int part = 0, value = 0, digits = 0;
    auto commit = [&]() -> bool {
        if (digits == 0 || value > 255 || part > 3) return false;
        out[part++] = static_cast<std::uint8_t>(value);
        value = 0; digits = 0;
        return true;
    };
    for (char c : ip) {
        if (c == '.') {
            if (!commit()) return false;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            ++digits;
        } else {
            return false;
        }
    }
    if (!commit()) return false;
    return part == 4;
}

}  // namespace

bool generateKeyPair(KeyPair& out) {
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx)
        return false;
    bool ok = EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &pkey) == 1;
    EVP_PKEY_CTX_free(ctx);
    if (!ok || !pkey) {
        if (pkey) EVP_PKEY_free(pkey);
        return false;
    }

    std::size_t pubLen = out.publicKey.size();
    std::size_t privLen = out.privateKey.size();
    ok = EVP_PKEY_get_raw_public_key(pkey, out.publicKey.data(), &pubLen) == 1 &&
         EVP_PKEY_get_raw_private_key(pkey, out.privateKey.data(), &privLen) == 1 &&
         pubLen == out.publicKey.size() && privLen == out.privateKey.size();
    EVP_PKEY_free(pkey);
    return ok;
}

std::vector<std::uint8_t> sign(const std::array<std::uint8_t, 32>& privateKey,
                               const std::vector<std::uint8_t>& message) {
    std::vector<std::uint8_t> sig;
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                  privateKey.data(), privateKey.size());
    if (!pkey)
        return sig;

    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (md && EVP_DigestSignInit(md, nullptr, nullptr, nullptr, pkey) == 1) {
        std::size_t sigLen = 0;
        if (EVP_DigestSign(md, nullptr, &sigLen, message.data(), message.size()) == 1) {
            sig.resize(sigLen);
            if (EVP_DigestSign(md, sig.data(), &sigLen, message.data(), message.size()) == 1)
                sig.resize(sigLen);
            else
                sig.clear();
        }
    }
    if (md) EVP_MD_CTX_free(md);
    EVP_PKEY_free(pkey);
    return sig;
}

bool verify(const std::array<std::uint8_t, 32>& publicKey,
            const std::vector<std::uint8_t>& message,
            const std::vector<std::uint8_t>& signature) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                 publicKey.data(), publicKey.size());
    if (!pkey)
        return false;

    bool ok = false;
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (md && EVP_DigestVerifyInit(md, nullptr, nullptr, nullptr, pkey) == 1) {
        ok = EVP_DigestVerify(md, signature.data(), signature.size(),
                              message.data(), message.size()) == 1;
    }
    if (md) EVP_MD_CTX_free(md);
    EVP_PKEY_free(pkey);
    return ok;
}

std::string encodeConnectionKey(const ConnectionKeyData& data) {
    std::array<std::uint8_t, 4> ip{};
    if (!parseIpv4(data.externalIp, ip))
        return {};

    Bytes blob;
    blob.reserve(kBlobSize);
    blob.insert(blob.end(), kMagic.begin(), kMagic.end());
    blob.push_back(kVersion);
    blob.insert(blob.end(), ip.begin(), ip.end());
    blob.push_back(static_cast<std::uint8_t>(data.port >> 8));
    blob.push_back(static_cast<std::uint8_t>(data.port & 0xFF));
    blob.insert(blob.end(), data.publicKey.begin(), data.publicKey.end());

    std::uint32_t crc = crc32(blob);
    blob.push_back(static_cast<std::uint8_t>(crc >> 24));
    blob.push_back(static_cast<std::uint8_t>(crc >> 16));
    blob.push_back(static_cast<std::uint8_t>(crc >> 8));
    blob.push_back(static_cast<std::uint8_t>(crc & 0xFF));

    return base64Encode(blob);
}

std::optional<ConnectionKeyData> parseConnectionKey(const std::string& text) {
    Bytes blob;
    if (!base64Decode(text, blob) || blob.size() != kBlobSize)
        return std::nullopt;

    if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin()))
        return std::nullopt;
    if (blob[4] != kVersion)
        return std::nullopt;

    Bytes body(blob.begin(), blob.end() - 4);
    std::uint32_t want = (static_cast<std::uint32_t>(blob[kBlobSize - 4]) << 24) |
                         (static_cast<std::uint32_t>(blob[kBlobSize - 3]) << 16) |
                         (static_cast<std::uint32_t>(blob[kBlobSize - 2]) << 8) |
                         static_cast<std::uint32_t>(blob[kBlobSize - 1]);
    if (crc32(body) != want)
        return std::nullopt;

    ConnectionKeyData data;
    std::size_t off = 5;
    data.externalIp = std::to_string(blob[off]) + '.' + std::to_string(blob[off + 1]) + '.' +
                      std::to_string(blob[off + 2]) + '.' + std::to_string(blob[off + 3]);
    off += 4;
    data.port = static_cast<std::uint16_t>((blob[off] << 8) | blob[off + 1]);
    off += 2;
    std::copy(blob.begin() + off, blob.begin() + off + 32, data.publicKey.begin());
    return data;
}

}  // namespace core
