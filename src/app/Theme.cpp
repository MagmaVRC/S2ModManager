#include "Theme.h"
#include "../ui/Colors.h"
#include <imgui.h>
#include <filesystem>
#include <windows.h>

namespace app {

namespace {
Fonts g_fonts;
}

Fonts& fonts() { return g_fonts; }

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    static const ImWchar kRanges[] = { 0x0020, 0x00FF, 0 };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.GlyphRanges = kRanges;

    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::filesystem::path fontsDir = std::filesystem::path(winDir) / L"Fonts";
    auto fontPath = [&](const wchar_t* file) { return fontsDir / file; };
    auto load = [&](const wchar_t* file, float size) -> ImFont* {
        std::filesystem::path p = fontPath(file);
        if (!std::filesystem::exists(p)) return nullptr;
        return io.Fonts->AddFontFromFileTTF(p.string().c_str(), size, &cfg);
    };

    // Base sizes; live scaling is done by style.FontScaleMain/FontScaleDpi (ImGui 1.92
    // scales fonts dynamically, so no atlas reload is needed when the UI scale changes).
    g_fonts.regular  = load(L"segoeui.ttf", 14.0f);
    g_fonts.semibold = load(L"seguisb.ttf", 15.0f);
    g_fonts.title    = load(L"seguisb.ttf", 20.0f);
    g_fonts.label    = load(L"segoeui.ttf", 11.5f);

    if (!g_fonts.regular)
        g_fonts.regular = io.Fonts->AddFontDefault();
    // Fall back to the body font for any weight the system is missing.
    if (!g_fonts.semibold) g_fonts.semibold = g_fonts.regular;
    if (!g_fonts.title)    g_fonts.title    = g_fonts.semibold;
    if (!g_fonts.label)    g_fonts.label    = g_fonts.regular;
    io.FontDefault = g_fonts.regular;
}

void applyTheme(const core::Palette& palette, float scale) {
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 7.0f;
    s.GrabRounding      = 7.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 7.0f;
    s.TabRounding       = 7.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.WindowPadding     = ImVec2(10.0f, 10.0f);
    s.FramePadding      = ImVec2(9.0f, 6.0f);
    s.ItemSpacing       = ImVec2(8.0f, 7.0f);
    s.ItemInnerSpacing  = ImVec2(7.0f, 5.0f);
    s.ScrollbarSize     = 11.0f;
    s.GrabMinSize       = 11.0f;

    s.ScaleAllSizes(scale);
    ui::applyPalette(palette);
}

}
