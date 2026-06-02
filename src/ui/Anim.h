#pragma once
#include <imgui.h>
#include <cmath>

namespace ui {

/// <summary>Marks that an animation moved this frame, so the render loop keeps drawing.</summary>
void markAnimActive();

/// <summary>Returns whether any animation moved since the last call, then clears the flag.</summary>
[[nodiscard]] bool consumeAnimActive();

/// <summary>Cubic ease-out curve for t in [0,1].</summary>
[[nodiscard]] float easeOutCubic(float t);

/// <summary>Symmetric ease-in-out cubic curve for t in [0,1].</summary>
[[nodiscard]] float easeInOutCubic(float t);

/// <summary>Linearly interpolates between two packed colours.</summary>
[[nodiscard]] ImU32 lerpColor(ImU32 a, ImU32 b, float t);

/// <summary>Frame-rate-independent exponential smoothing toward a target.</summary>
/// <param name="cur">Current value.</param>
/// <param name="target">Value to ease toward.</param>
/// <param name="lambda">Decay rate (1/seconds); higher is snappier.</param>
/// <param name="dt">Frame delta time in seconds.</param>
/// <returns>The eased value, snapped to target once within epsilon.</returns>
[[nodiscard]] inline float expDamp(float cur, float target, float lambda, float dt) {
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;   // clamp huge stalls so motion never overshoots wildly
    cur += (target - cur) * (1.0f - std::exp(-lambda * dt));
    if (std::fabs(target - cur) < 0.0005f) cur = target;
    return cur;
}

/// <summary>Frame-rate independent value that eases toward a target each frame.</summary>
struct AnimFloat {
    float value = 0.0f;
    float target = 0.0f;
    float speed = 14.0f;
    bool  initialized = false;

    /// <summary>Sets the target, snapping instantly the first time it is used.</summary>
    void to(float t) {
        target = t;
        if (!initialized) { value = t; initialized = true; }
    }

    /// <summary>Advances the eased value by one frame and returns it.</summary>
    float update(float dt) {
        float prev = value;
        value = expDamp(value, target, speed, dt);
        if (value != prev) markAnimActive();
        return value;
    }
};

/// <summary>Per-widget smoothing stored in ImGui state storage, keyed by id.</summary>
/// <returns>The eased value approaching <paramref name="target"/>.</returns>
float animTo(ImGuiID id, float target, float speed);

/// <summary>Seeds the eased value stored for an id so a later animTo eases from this point.</summary>
void animSet(ImGuiID id, float v);

/// <summary>Critically/under-damped spring toward a target, stored in ImGui state storage by id.</summary>
/// <param name="id">Unique widget id (value and velocity are stored under derived keys).</param>
/// <param name="target">Resting position.</param>
/// <param name="omega">Angular frequency (stiffness); higher reaches the target faster.</param>
/// <param name="zeta">Damping ratio: 1.0 = no overshoot, &lt;1.0 = springy overshoot.</param>
/// <returns>The current spring position.</returns>
float springTo(ImGuiID id, float target, float omega, float zeta);

/// <summary>Combined layout scale (DPI scale x user scale).</summary>
[[nodiscard]] float uiScale();

/// <summary>Sets the DPI scale factor.</summary>
void setUiScale(float scale);

/// <summary>Sets the user-chosen scale multiplier applied on top of DPI.</summary>
void setUserScale(float scale);

/// <summary>Rounds a coordinate to the nearest whole pixel.</summary>
[[nodiscard]] inline float px(float v) { return std::floor(v + 0.5f); }

/// <summary>Draws text with pixel-snapped position for crisp glyphs.</summary>
void textSnapped(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text);

}
