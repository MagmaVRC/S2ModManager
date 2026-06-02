#pragma once
#include "../core/Palette.h"

struct ImFont;

namespace app {

/// <summary>Fonts loaded for the UI.</summary>
struct Fonts {
    ImFont* regular  = nullptr;   ///< Body text (Segoe UI 14).
    ImFont* semibold = nullptr;   ///< Emphasis / top-bar title (Segoe UI Semibold 15).
    ImFont* title    = nullptr;   ///< Section headings "Mods" / mod name (Segoe UI Semibold 20).
    ImFont* label    = nullptr;   ///< Small uppercase column / section labels (Segoe UI 11.5).
};

/// <summary>Returns the loaded font set.</summary>
Fonts& fonts();

/// <summary>Loads UI fonts at the given DPI scale. Call before the first frame and on DPI change.</summary>
/// <param name="scale">DPI scale factor (1.0 = 96 DPI).</param>
void loadFonts(float scale);

/// <summary>Applies style metrics for the given DPI scale and the supplied palette colours.</summary>
/// <param name="palette">Colour set to apply.</param>
/// <param name="scale">DPI scale factor (1.0 = 96 DPI).</param>
void applyTheme(const core::Palette& palette, float scale);

}
