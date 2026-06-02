#include "Anim.h"

namespace ui {

namespace { bool g_animActive = false; }

void markAnimActive() { g_animActive = true; }
bool consumeAnimActive() { bool a = g_animActive; g_animActive = false; return a; }

float easeOutCubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    ImVec4 r(ca.x + (cb.x - ca.x) * t,
             ca.y + (cb.y - ca.y) * t,
             ca.z + (cb.z - ca.z) * t,
             ca.w + (cb.w - ca.w) * t);
    return ImGui::ColorConvertFloat4ToU32(r);
}

float animTo(ImGuiID id, float target, float speed) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    float cur = store->GetFloat(id, target);
    float next = expDamp(cur, target, speed, ImGui::GetIO().DeltaTime);
    if (next != cur) markAnimActive();
    store->SetFloat(id, next);
    return next;
}

void animSet(ImGuiID id, float v) {
    ImGui::GetStateStorage()->SetFloat(id, v);
}

float springTo(ImGuiID id, float target, float omega, float zeta) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID velKey = id ^ 0x9E3779B1u;
    float x = store->GetFloat(id, target);
    float v = store->GetFloat(velKey, 0.0f);

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;

    float force = omega * omega * (target - x) - 2.0f * zeta * omega * v;
    v += force * dt;
    x += v * dt;

    if (std::fabs(target - x) < 0.0005f && std::fabs(v) < 0.0005f) {
        x = target;
        v = 0.0f;
    } else {
        markAnimActive();
    }

    store->SetFloat(id, x);
    store->SetFloat(velKey, v);
    return x;
}

namespace { float g_uiScale = 1.0f; float g_userScale = 1.0f; }

float uiScale() { return g_uiScale * g_userScale; }
void setUiScale(float scale) { g_uiScale = scale; }
void setUserScale(float scale) { g_userScale = scale; }

void textSnapped(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    float ga = ImGui::GetStyle().Alpha;
    if (ga < 1.0f) {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(col);
        v.w *= ga;
        col = ImGui::ColorConvertFloat4ToU32(v);
    }
    dl->AddText(ImVec2(px(pos.x), px(pos.y)), col, text);
}

}
