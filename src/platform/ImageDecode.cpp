#include "ImageDecode.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include <webp/decode.h>

#include <fstream>
#include <iterator>

namespace platform {

namespace {
/// <summary>True when the buffer begins with a RIFF/WEBP container header.</summary>
bool isWebp(const unsigned char* d, std::size_t n) {
    return n >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F'
        && d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P';
}
}

std::optional<DecodedImage> decodeImageBytes(const unsigned char* data, std::size_t n) {
    if (!data || n == 0)
        return std::nullopt;

    if (isWebp(data, n)) {
        int w = 0, h = 0;
        if (!WebPGetInfo(data, n, &w, &h) || w <= 0 || h <= 0)
            return std::nullopt;
        DecodedImage img;
        img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        if (!WebPDecodeRGBAInto(data, n, img.rgba.data(), img.rgba.size(),
                                w * 4)) {
            return std::nullopt;
        }
        img.w = w;
        img.h = h;
        return img;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(n), &w, &h, &comp, 4);
    if (!px)
        return std::nullopt;
    DecodedImage img;
    img.w = w;
    img.h = h;
    img.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

std::optional<DecodedImage> decodeImageFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty())
        return std::nullopt;
    return decodeImageBytes(bytes.data(), bytes.size());
}

}
