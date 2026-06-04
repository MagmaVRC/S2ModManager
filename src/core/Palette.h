#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace core {

/// <summary>A complete set of UI role colours (packed RGBA, IM_COL32 byte order).</summary>
struct Palette {
    std::uint32_t bg, surface, surface2, surface3, border;
    std::uint32_t accent, accentHi, accentDim, warn;
    std::uint32_t select, selectHi;   // secondary accent for selected/active state (amber in the HUD theme)
    std::uint32_t text, textHi, textDim, ink;
};

/// <summary>The built-in Subnautica HUD palette (default look): teal/cyan with amber selection.</summary>
Palette defaultSubnautica();

/// <summary>The built-in minimal dark palette.</summary>
Palette defaultDark();

/// <summary>The built-in minimal light palette.</summary>
Palette defaultLight();

/// <summary>Formats a packed RGBA colour as "#RRGGBBAA".</summary>
std::string colorToHex(std::uint32_t c);

/// <summary>Parses "#RRGGBBAA" (or "#RRGGBB"); returns fallback on malformed input.</summary>
std::uint32_t hexToColor(std::string_view s, std::uint32_t fallback);

}
