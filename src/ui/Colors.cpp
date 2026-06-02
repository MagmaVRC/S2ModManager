#include "Colors.h"

namespace ui {

void applyPalette(const core::Palette& p) {
    colBg = p.bg; colSurface = p.surface; colSurface2 = p.surface2; colSurface3 = p.surface3;
    colBorder = p.border; colAccent = p.accent; colAccentHi = p.accentHi; colAccentDim = p.accentDim;
    colWarn = p.warn; colSelect = p.select; colSelectHi = p.selectHi;
    colText = p.text; colTextHi = p.textHi; colTextDim = p.textDim; colInk = p.ink;

    ImGuiStyle& s = ImGui::GetStyle();
    s.Colors[ImGuiCol_WindowBg]           = toVec(colBg);
    s.Colors[ImGuiCol_ChildBg]            = toVec(colSurface);
    s.Colors[ImGuiCol_PopupBg]            = toVec(colSurface);
    s.Colors[ImGuiCol_Border]             = toVec(colBorder);
    s.Colors[ImGuiCol_FrameBg]            = toVec(colSurface2);
    s.Colors[ImGuiCol_FrameBgHovered]     = toVec(colSurface3);
    s.Colors[ImGuiCol_FrameBgActive]      = toVec(colSurface3);
    s.Colors[ImGuiCol_Text]               = toVec(colText);
    s.Colors[ImGuiCol_TextDisabled]       = toVec(colTextDim);
    s.Colors[ImGuiCol_Button]             = toVec(colSurface2);
    s.Colors[ImGuiCol_ButtonHovered]      = toVec(colSurface3);
    s.Colors[ImGuiCol_ButtonActive]       = toVec(colAccent);
    s.Colors[ImGuiCol_Header]             = toVec(colSurface2);
    s.Colors[ImGuiCol_HeaderHovered]      = toVec(colSurface3);
    s.Colors[ImGuiCol_HeaderActive]       = toVec(colAccent);
    s.Colors[ImGuiCol_CheckMark]          = toVec(colAccentHi);
    s.Colors[ImGuiCol_SliderGrab]         = toVec(colAccent);
    s.Colors[ImGuiCol_SliderGrabActive]   = toVec(colAccentHi);
    s.Colors[ImGuiCol_Tab]                = toVec(colSurface);
    s.Colors[ImGuiCol_TabHovered]         = toVec(colSurface3);
    s.Colors[ImGuiCol_TabSelected]        = toVec(colSurface2);
    s.Colors[ImGuiCol_TabDimmed]          = toVec(colSurface);
    s.Colors[ImGuiCol_TabDimmedSelected]  = toVec(colSurface2);
    s.Colors[ImGuiCol_TitleBg]            = toVec(colSurface);
    s.Colors[ImGuiCol_TitleBgActive]      = toVec(colSurface);
    s.Colors[ImGuiCol_ScrollbarBg]        = toVec(colSurface);
    s.Colors[ImGuiCol_ScrollbarGrab]      = toVec(colSurface3);
    s.Colors[ImGuiCol_Separator]          = toVec(colBorder);
    ImVec4 modalDim = toVec(colBg); modalDim.w = 0.34f;
    s.Colors[ImGuiCol_ModalWindowDimBg]   = modalDim;
}

}
