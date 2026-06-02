#include "Widgets.h"
#include "Anim.h"
#include "Colors.h"
#include "Icons.h"
#include <cmath>

namespace ui {

bool toggleSwitch(const char* id, bool* v) {
    ImGuiID wid = ImGui::GetID(id);
    float scale = uiScale();
    float h = ImGui::GetFrameHeight() * 0.82f;
    float w = h * 1.85f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - h) * 0.5f;

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *v = !*v;

    float on01 = *v ? 1.0f : 0.0f;
    float on  = springTo(wid, on01, 26.0f, 0.62f);
    float onC = on < 0.0f ? 0.0f : (on > 1.0f ? 1.0f : on);
    float hov = animTo(wid + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = h * 0.5f;
    ImVec2 e(p.x + w, p.y + h);
    ImU32 track = lerpColor(lerpColor(colSurface3, colSurface2, 0.0f), colAccent, onC);
    dl->AddRectFilled(p, e, track, r);
    dl->AddRect(p, e, lerpColor(colBorder, colAccentHi, onC), r, 0, 1.0f * scale);

    float kr = r - 2.5f * scale + hov * 0.8f * scale;
    float kx = p.x + r + on * (w - 2.0f * r);
    ImVec2 kc(kx, p.y + r);
    dl->AddCircleFilled(ImVec2(kc.x, kc.y + 0.8f * scale), kr, IM_COL32(0, 0, 0, 45));   // drop shadow
    dl->AddCircleFilled(kc, kr, IM_COL32(248, 251, 253, 255));
    return clicked;
}

// kind: 0 = filled accent, 1 = outlined ghost, 2 = outlined danger.
static bool buttonImpl(const char* label, ImVec2 size, int kind, ImU32 accent, ImTextureID icon) {
    ImGuiID id = ImGui::GetID(label);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImGuiStyle& st = ImGui::GetStyle();
    float sc = uiScale();
    float iconSz = icon ? std::floor(ts.y + 2.0f * sc) : 0.0f;
    float gap = icon ? 6.0f * sc : 0.0f;
    float groupW = iconSz + gap + ts.x;
    if (size.x <= 0.0f) size.x = groupW + st.FramePadding.x * 2.0f + 8.0f;
    if (size.y <= 0.0f) size.y = ts.y + st.FramePadding.y * 2.0f;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size);
    bool clicked = ImGui::IsItemClicked();
    float hov = animTo(id, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
    float press = animTo(id + 1u, ImGui::IsItemActive() ? 1.0f : 0.0f, 30.0f);
    float inset = press * sc;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x + inset, p.y + inset);
    ImVec2 b(p.x + size.x - inset, p.y + size.y - inset);
    float rnd = st.FrameRounding;
    float ga = st.Alpha;
    auto A = [ga](ImU32 c) {
        if (ga >= 1.0f) return c;
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c); v.w *= ga;
        return ImGui::ColorConvertFloat4ToU32(v);
    };

    ImU32 txt;
    if (kind == 0) {
        dl->AddRectFilled(a, b, A(lerpColor(accent, colAccentHi, hov)), rnd);
        txt = colTextHi;
    } else {
        ImVec4 dimv = ImGui::ColorConvertU32ToFloat4(accent); dimv.w = 0.16f;
        ImU32 dim = ImGui::ColorConvertFloat4ToU32(dimv);
        dl->AddRectFilled(a, b, A(lerpColor(IM_COL32(0, 0, 0, 0), dim, hov)), rnd);
        dl->AddRect(a, b, A(lerpColor(colSurface3, accent, hov)), rnd, 0, 1.0f);
        txt = (kind == 2) ? lerpColor(accent, colTextHi, hov)
                          : lerpColor(colTextDim, colText, hov);
    }
    float gx = p.x + (size.x - groupW) * 0.5f;
    if (icon) {
        ImVec2 ip(px(gx), px(p.y + (size.y - iconSz) * 0.5f));
        drawIconTex(dl, icon, ip, ImVec2(ip.x + iconSz, ip.y + iconSz), A(txt));
    }
    textSnapped(dl, ImVec2(gx + iconSz + gap, p.y + (size.y - ts.y) * 0.5f), txt, label);
    return clicked;
}

bool primaryButton(const char* label, const ImVec2& size) { return buttonImpl(label, size, 0, colAccent, 0); }
bool ghostButton(const char* label, const ImVec2& size)   { return buttonImpl(label, size, 1, colAccent, 0); }
bool dangerButton(const char* label, const ImVec2& size)  { return buttonImpl(label, size, 2, IM_COL32(212, 96, 96, 255), 0); }
bool primaryButton(const char* label, ImTextureID icon, const ImVec2& size) { return buttonImpl(label, size, 0, colAccent, icon); }
bool ghostButton(const char* label, ImTextureID icon, const ImVec2& size)   { return buttonImpl(label, size, 1, colAccent, icon); }

bool iconButton(const char* id, ImTextureID icon, const ImVec2& size, ImU32 tint, bool outlined) {
    ImGuiID wid = ImGui::GetID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool clicked = ImGui::IsItemClicked();
    float hov = animTo(wid, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
    float press = animTo(wid + 1u, ImGui::IsItemActive() ? 1.0f : 0.0f, 30.0f);
    float inset = press * uiScale();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x + inset, p.y + inset);
    ImVec2 b(p.x + size.x - inset, p.y + size.y - inset);
    float rnd = ImGui::GetStyle().FrameRounding;
    if (outlined) {
        ImVec4 dimv = ImGui::ColorConvertU32ToFloat4(colAccent); dimv.w = 0.16f;
        dl->AddRectFilled(a, b, lerpColor(IM_COL32(0, 0, 0, 0), ImGui::ColorConvertFloat4ToU32(dimv), hov), rnd);
        dl->AddRect(a, b, lerpColor(colSurface3, colAccent, hov), rnd, 0, 1.0f);
    }
    if (icon) {
        float side = size.x < size.y ? size.x : size.y;
        float isz = side - 9.0f * uiScale();
        if (isz < 6.0f) isz = side * 0.6f;
        ImVec2 ip(px(p.x + (size.x - isz) * 0.5f), px(p.y + (size.y - isz) * 0.5f));
        drawIconTex(dl, icon, ip, ImVec2(ip.x + isz, ip.y + isz), lerpColor(tint, colTextHi, hov));
    }
    return clicked;
}

void pill(ImVec2 pos, const char* text, ImU32 accent) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad(8.0f, 3.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 e(pos.x + ts.x + pad.x * 2.0f, pos.y + ts.y + pad.y * 2.0f);
    dl->AddRectFilled(pos, e, lerpColor(accent, colBg, 0.78f), (ts.y + pad.y * 2.0f) * 0.5f);
    textSnapped(dl, ImVec2(pos.x + pad.x, pos.y + pad.y), accent, text);
}

float pillWidth(const char* text) {
    return ImGui::CalcTextSize(text).x + 16.0f;   // text + 8px horizontal padding each side
}

void spinner(ImVec2 center, float radius, float thickness, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = static_cast<float>(ImGui::GetTime());
    const float start = t * 6.0f;                 // rotation speed
    const float arc = 1.6f * 3.14159265358979f;   // ~290 deg sweep, leaving a gap
    const int segs = 24;
    dl->PathClear();
    for (int i = 0; i <= segs; ++i) {
        const float a = start + (static_cast<float>(i) / segs) * arc;
        dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
    }
    dl->PathStroke(color, 0, thickness);
}

void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                float rounding, float thickness, float phase) {
    dl->PathClear();
    dl->PathRect(a, b, rounding, ImDrawFlags_RoundCornersAll);
    const ImVector<ImVec2>& pts = dl->_Path;
    if (pts.Size < 2) { dl->PathClear(); return; }

    const float dash = 7.0f, gap = 5.0f, period = dash + gap;
    float dist = std::fmod(phase, period);
    if (dist < 0.0f) dist += period;
    bool drawing = dist < dash;
    float remain = drawing ? dash - dist : period - dist;

    for (int i = 0; i < pts.Size; ++i) {
        ImVec2 p0 = pts[i];
        ImVec2 p1 = pts[(i + 1) % pts.Size];
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        float segLen = std::sqrt(dx * dx + dy * dy);
        if (segLen < 0.0001f) continue;
        float nx = dx / segLen, ny = dy / segLen;
        float t = 0.0f;
        while (t < segLen) {
            float step = remain < (segLen - t) ? remain : (segLen - t);
            if (drawing)
                dl->AddLine(ImVec2(p0.x + nx * t, p0.y + ny * t),
                            ImVec2(p0.x + nx * (t + step), p0.y + ny * (t + step)),
                            col, thickness);
            t += step;
            remain -= step;
            if (remain <= 0.0001f) { drawing = !drawing; remain = drawing ? dash : gap; }
        }
    }
    dl->PathClear();
}

void progressBar(const char* id, float frac, ImVec2 size) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    ImGuiID wid = ImGui::GetID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();
    ImGui::Dummy(size);

    float r = size.y * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), colSurface3, r);

    float fillW = size.x * frac;
    if (fillW > 0.0f) {
        ImVec2 fa = p, fb(p.x + fillW, p.y + size.y);
        dl->PushClipRect(fa, fb, true);
        dl->AddRectFilled(fa, fb, colAccent, r);

        float t = ImGui::GetStateStorage()->GetFloat(wid, 0.0f);
        t += ImGui::GetIO().DeltaTime * 0.8f;
        if (t > 1.0f) t -= 1.0f;
        ImGui::GetStateStorage()->SetFloat(wid, t);
        markAnimActive();

        float bandW = size.x * 0.22f;
        float bx = p.x - bandW + t * (fillW + bandW);
        ImU32 c0 = IM_COL32(255, 255, 255, 0);
        ImU32 c1 = IM_COL32(255, 255, 255, 46);
        dl->AddRectFilledMultiColor(ImVec2(bx, p.y), ImVec2(bx + bandW * 0.5f, p.y + size.y), c0, c1, c1, c0);
        dl->AddRectFilledMultiColor(ImVec2(bx + bandW * 0.5f, p.y), ImVec2(bx + bandW, p.y + size.y), c1, c0, c0, c1);
        dl->PopClipRect();
    }
}

}
