#pragma once
#include <imgui.h>
#include "../core/Palette.h"

namespace ui {

inline ImU32 colBg        = IM_COL32( 56,  56,  56, 255);
inline ImU32 colSurface   = IM_COL32( 45,  45,  45, 255);
inline ImU32 colSurface2  = IM_COL32( 67,  67,  67, 255);
inline ImU32 colSurface3  = IM_COL32( 84,  84,  84, 255);
inline ImU32 colBorder    = IM_COL32( 26,  26,  26, 255);
inline ImU32 colAccent    = IM_COL32( 62, 110, 158, 255);
inline ImU32 colAccentHi  = IM_COL32( 86, 141, 192, 255);
inline ImU32 colAccentDim = IM_COL32( 62, 110, 158,  90);
inline ImU32 colWarn      = IM_COL32(202, 145,  62, 255);
inline ImU32 colSelect    = IM_COL32(232, 146,  60, 255);
inline ImU32 colSelectHi  = IM_COL32(248, 178,  98, 255);
inline ImU32 colText      = IM_COL32(196, 196, 196, 255);
inline ImU32 colTextHi    = IM_COL32(236, 236, 236, 255);
inline ImU32 colTextDim   = IM_COL32(162, 162, 162, 255);
inline ImU32 colInk       = IM_COL32(236, 236, 236, 255);

/// <summary>Converts a packed ImU32 colour to an ImVec4.</summary>
inline ImVec4 toVec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

/// <summary>Copies a core::Palette into the col* globals and the ImGui style colours.</summary>
void applyPalette(const core::Palette& p);

}
