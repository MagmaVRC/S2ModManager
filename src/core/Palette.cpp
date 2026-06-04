#include "Palette.h"
#include <cstdio>

namespace core {

namespace {
constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return static_cast<std::uint32_t>(a) << 24 | static_cast<std::uint32_t>(b) << 16 | static_cast<std::uint32_t>(g) << 8 | r;
}
}

Palette defaultSubnautica() {
    // Underwater HUD: near-black teal background, dark translucent-reading teal panels,
    // cyan accent for primary actions, amber for the selected/active highlight.
    return Palette{
        rgba(9, 20, 26), rgba(18, 40, 49), rgba(27, 55, 66), rgba(40, 75, 88), rgba(46, 102, 117),
        rgba(72, 200, 222), rgba(122, 224, 240), rgba(72, 200, 222, 90), rgba(230, 150, 70),
        rgba(232, 146, 60), rgba(248, 178, 98),
        rgba(206, 224, 230), rgba(238, 248, 250), rgba(138, 168, 178), rgba(238, 248, 250),
    };
}

Palette defaultDark() {
    return Palette{
        rgba(56, 56, 56), rgba(45, 45, 45), rgba(67, 67, 67), rgba(84, 84, 84), rgba(26, 26, 26),
        rgba(62, 110, 158), rgba(86, 141, 192), rgba(62, 110, 158, 90), rgba(202, 145, 62),
        rgba(220, 150, 70), rgba(240, 175, 92),
        rgba(196, 196, 196), rgba(236, 236, 236), rgba(162, 162, 162), rgba(236, 236, 236),
    };
}

Palette defaultLight() {
    return Palette{
        rgba(238, 240, 243), rgba(248, 249, 251), rgba(228, 231, 236), rgba(214, 219, 226), rgba(196, 202, 211),
        rgba(45, 120, 200), rgba(70, 145, 224), rgba(45, 120, 200, 70), rgba(190, 120, 30),
        rgba(212, 140, 40), rgba(232, 160, 60),
        rgba(40, 44, 52), rgba(16, 18, 22), rgba(95, 100, 110), rgba(16, 18, 22),
    };
}

std::string colorToHex(std::uint32_t c) {
    unsigned r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF, a = (c >> 24) & 0xFF;
    char buf[10];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    return buf;
}

std::uint32_t hexToColor(std::string_view s, std::uint32_t fallback) {
    if (s.size() < 7 || s[0] != '#') return fallback;
    auto hx = [&](int i) -> int {
        char c = s[i];
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[8];
    int n = (s.size() >= 9) ? 8 : 6;
    for (int i = 0; i < n; ++i) { v[i] = hx(i + 1); if (v[i] < 0) return fallback; }
    unsigned r = v[0] * 16 + v[1], g = v[2] * 16 + v[3], b = v[4] * 16 + v[5];
    unsigned a = (n == 8) ? static_cast<unsigned>(v[6] * 16 + v[7]) : 255u;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

}
