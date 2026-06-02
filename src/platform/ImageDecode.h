#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace platform {

/// <summary>A decoded image as tightly-packed, top-down RGBA8 pixels.</summary>
struct DecodedImage {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgba;   // w*h*4 bytes
};

/// <summary>Decodes PNG/JPEG/WebP bytes to RGBA8.</summary>
/// <param name="data">Encoded image bytes.</param>
/// <param name="n">Byte count.</param>
/// <returns>The decoded image, or none if the format is unsupported or corrupt.</returns>
[[nodiscard]] std::optional<DecodedImage> decodeImageBytes(const unsigned char* data, std::size_t n);

/// <summary>Reads and decodes a PNG/JPEG/WebP file to RGBA8.</summary>
/// <param name="path">Path to the image file.</param>
/// <returns>The decoded image, or none on read failure or unsupported/corrupt data.</returns>
[[nodiscard]] std::optional<DecodedImage> decodeImageFile(const std::filesystem::path& path);

}
