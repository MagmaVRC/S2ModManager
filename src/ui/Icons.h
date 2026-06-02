#pragma once
#include <imgui.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace ui {

/// <summary>UI icon identifiers. Order is independent of resource ids (see Icons.cpp).</summary>
enum class Icon {
    Search, File, Folder, FolderOpen, ChevronUp, ChevronDown, ArrowUp, ArrowDown,
    Settings, Share, Play, Plus, Package, Box, Grip, Close, Trash,
    Key, Copy, Check, Download, Upload, Link, CircleCheck, Alert, HardDrive,
    Send, FileDown, FileUp, Wifi, ChevronLeft, Logo, Count
};

/// <summary>Owns GPU textures for the embedded white-on-transparent icon PNGs and draws
/// them tinted to any colour. Built once the D3D11 device exists; freed before it dies.</summary>
class IconSet {
public:
    IconSet() = default;
    ~IconSet();
    IconSet(const IconSet&) = delete;
    IconSet& operator=(const IconSet&) = delete;

    /// <summary>Decodes every embedded icon and uploads it as a mip-mapped texture.</summary>
    void init(ID3D11Device* device, ID3D11DeviceContext* context);

    /// <summary>Releases all icon textures. Must run before the device is destroyed.</summary>
    void release();

    /// <summary>Binds the trilinear/anisotropic icon sampler (used by the draw callback so
    /// minified icons sample their mip chain instead of aliasing through ImGui's LOD-0 sampler).</summary>
    void bindSampler() const;

    /// <summary>True once at least one icon texture is loaded.</summary>
    [[nodiscard]] bool ready() const { return loaded_; }

    /// <summary>The texture handle for an icon, or 0 if it failed to load.</summary>
    [[nodiscard]] ImTextureID tex(Icon i) const;

    /// <summary>Draws a square icon of pixel <paramref name="size"/> with its top-left at
    /// <paramref name="pos"/>, multiplied by <paramref name="tint"/> (white art becomes the tint colour).
    void draw(ImDrawList* dl, Icon i, ImVec2 pos, float size, ImU32 tint) const;

private:
    void* srv_[static_cast<int>(Icon::Count)] = {};   // ID3D11ShaderResourceView*
    void* tex_[static_cast<int>(Icon::Count)] = {};   // ID3D11Texture2D*
    void* ctx_  = nullptr;   // ID3D11DeviceContext* (not owned)
    void* samp_ = nullptr;   // ID3D11SamplerState* (trilinear + anisotropic)
    bool  loaded_ = false;
};

/// <summary>Process-wide icon set, initialised by App once the render device is ready.</summary>
IconSet& icons();

/// <summary>Draws an icon texture into a draw list through the trilinear/anisotropic sampler
/// (via a draw callback) so it minifies smoothly. Use for any icon AddImage, not raw AddImage.</summary>
void drawIconTex(ImDrawList* dl, ImTextureID tex, const ImVec2& a, const ImVec2& b, ImU32 tint);

}
