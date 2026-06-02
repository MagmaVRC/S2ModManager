#pragma once
#include <imgui.h>

namespace ui {

/// <summary>Animated on/off switch with a sliding knob and colour transition.</summary>
/// <param name="id">Unique widget id.</param>
/// <param name="v">Bound state, toggled on click.</param>
/// <returns>True on the frame the switch is toggled.</returns>
bool toggleSwitch(const char* id, bool* v);

/// <summary>Solid accent button with hover and press animation.</summary>
/// <returns>True when clicked.</returns>
bool primaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// <summary>Solid accent button with a tinted icon left of the label (icon 0 = label only).</summary>
/// <returns>True when clicked.</returns>
bool primaryButton(const char* label, ImTextureID icon, const ImVec2& size = ImVec2(0, 0));

/// <summary>Outlined secondary button with a hover fill animation.</summary>
/// <returns>True when clicked.</returns>
bool ghostButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// <summary>Outlined secondary button with a tinted icon left of the label (icon 0 = label only).</summary>
/// <returns>True when clicked.</returns>
bool ghostButton(const char* label, ImTextureID icon, const ImVec2& size = ImVec2(0, 0));

/// <summary>Outlined destructive button (red outline/text) for irreversible actions.</summary>
/// <returns>True when clicked.</returns>
bool dangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// <summary>Square/rect button showing a centred tinted icon texture, hover-lit.</summary>
/// <param name="id">Unique widget id (also the InvisibleButton key).</param>
/// <param name="icon">Icon texture; 0 draws just the frame.</param>
/// <param name="tint">Base icon colour (brightens toward text colour on hover).</param>
/// <param name="outlined">When true, draws the ghost-button frame behind the icon.</param>
/// <returns>True when clicked.</returns>
bool iconButton(const char* id, ImTextureID icon, const ImVec2& size, ImU32 tint, bool outlined = true);

/// <summary>Draws a small rounded label chip with its top-left at the given screen position.</summary>
void pill(ImVec2 pos, const char* text, ImU32 accent);

/// <summary>Draws an animated indeterminate spinner (a rotating arc) centred at <paramref name="center"/>.</summary>
void spinner(ImVec2 center, float radius, float thickness, ImU32 color);

/// <summary>Total width in pixels a <see cref="pill"/> occupies for the given text.</summary>
[[nodiscard]] float pillWidth(const char* text);

/// <summary>Draws a rounded rectangle outline as marching dashes that scroll over time.</summary>
/// <param name="dl">Target draw list.</param>
/// <param name="a">Top-left corner (screen space).</param>
/// <param name="b">Bottom-right corner (screen space).</param>
/// <param name="col">Dash colour.</param>
/// <param name="rounding">Corner radius in pixels.</param>
/// <param name="thickness">Line thickness in pixels.</param>
/// <param name="phase">Scroll offset in pixels (advance with time for motion).</param>
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                float rounding, float thickness, float phase);

/// <summary>Custom progress bar with a translucent light band that sweeps across the fill.</summary>
/// <param name="id">Unique id (drives the sweep animation).</param>
/// <param name="frac">Progress in [0,1].</param>
/// <param name="size">Bar size in pixels.</param>
void progressBar(const char* id, float frac, ImVec2 size);

}
