#pragma once
#include <cstdint>
#include <vector>

namespace core {

/// <summary>Raw byte buffer.</summary>
using Bytes = std::vector<std::uint8_t>;

/// <summary>Compresses a buffer as an xz (LZMA) stream.</summary>
/// <param name="in">Input bytes.</param>
/// <param name="preset">LZMA preset 0-9 (default 0 = fastest).</param>
/// <returns>Compressed bytes, or an empty buffer on failure.</returns>
[[nodiscard]] Bytes lzmaCompress(const Bytes& in, std::uint32_t preset = 0);

/// <summary>Decompresses an xz (LZMA) stream into a buffer of known size.</summary>
/// <param name="in">Compressed bytes.</param>
/// <param name="rawSize">Expected uncompressed size.</param>
/// <param name="out">Receives the decompressed bytes.</param>
/// <returns>True on success.</returns>
[[nodiscard]] bool lzmaDecompress(const Bytes& in, std::size_t rawSize, Bytes& out);

/// <summary>Compresses a buffer as a zstd frame (multithreaded for large inputs).</summary>
/// <param name="in">Input bytes.</param>
/// <param name="level">zstd level (default 3 = fast, good ratio).</param>
/// <returns>Compressed bytes, or an empty buffer on failure.</returns>
[[nodiscard]] Bytes zstdCompress(const Bytes& in, int level = 3);

/// <summary>Decompresses a zstd frame into a buffer of known size.</summary>
/// <returns>True on success.</returns>
[[nodiscard]] bool zstdDecompress(const Bytes& in, std::size_t rawSize, Bytes& out);

/// <summary>Computes a CRC-32 (IEEE) over a buffer.</summary>
[[nodiscard]] std::uint32_t crc32(const Bytes& in);

}
