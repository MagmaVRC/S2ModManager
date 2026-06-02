#pragma once
#include "../platform/ImageDecode.h"
#include <imgui.h>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace ui {

/// <summary>Renders a full-window background image with a modern GPU dual-Kawase blur
/// (downsample/upsample pyramid), an optional slow parallax drift, and a darkening
/// overlay. Owns its own D3D11 resources; the blurred result is cached and only
/// re-rendered when the image, blur amount, or downscale size changes.</summary>
class Background {
public:
    Background() = default;
    ~Background();
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;

    /// <summary>Supplies the D3D11 device/context used to build textures and run the blur.</summary>
    void setDevice(ID3D11Device* device, ID3D11DeviceContext* context);

    /// <summary>Uploads a decoded image as the new background source. Returns false on GPU failure.</summary>
    bool setImage(const platform::DecodedImage& img);

    /// <summary>Drops the current image so nothing is drawn.</summary>
    void clearImage();

    /// <summary>True when an image is loaded and ready to draw.</summary>
    [[nodiscard]] bool hasImage() const;

    /// <summary>Sets blur strength in [0,1]; re-renders the cached blur lazily on the next draw.</summary>
    void setBlur(float amount01);

    /// <summary>Sets darkening strength in [0,1] (alpha of a black overlay).</summary>
    void setDim(float amount01);

    /// <summary>Sets slow parallax drift: pan distance in [0,1] and a speed multiplier in [0,2].
    /// Zero amount keeps the image perfectly static.</summary>
    void setDrift(float amount01, float speed);

    /// <summary>Blits the cached blurred image across the window and applies the dim overlay.
    /// Call once per frame while building the ImGui frame, before drawing panels.</summary>
    void draw(const ImVec2& displaySize);

    /// <summary>Releases every D3D11 resource. Must run before the device is destroyed.</summary>
    void releaseDevice();

private:
    // One level of the dual-Kawase pyramid: a render-target texture plus its views.
    struct Level {
        void* tex = nullptr;   // ID3D11Texture2D*
        void* rtv = nullptr;   // ID3D11RenderTargetView*
        void* srv = nullptr;   // ID3D11ShaderResourceView*
        int   w = 0, h = 0;
    };

    bool ensurePipeline();          // compile shaders / create states once
    bool ensurePyramid(int levels); // (re)create the downscaled pyramid targets
    void releasePyramid();
    void ensureBlur();              // run the down/up passes if dirty

    ID3D11Device*        dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;

    // Source image.
    void* srcTex_ = nullptr;   // ID3D11Texture2D*
    void* srcSrv_ = nullptr;   // ID3D11ShaderResourceView*
    int   srcW_ = 0, srcH_ = 0;

    // Dual-Kawase pyramid (level 0 = base downscale, each deeper level is half-size).
    std::vector<Level> levels_;
    int   baseW_ = 0, baseH_ = 0;
    void* resultSrv_ = nullptr;   // points at srcSrv_ or a level srv; not owned

    // Pipeline objects.
    void* vs_ = nullptr;     // ID3D11VertexShader*
    void* ps_ = nullptr;     // ID3D11PixelShader*
    void* cb_ = nullptr;     // ID3D11Buffer* (params)
    void* samp_ = nullptr;   // ID3D11SamplerState*
    void* raster_ = nullptr; // ID3D11RasterizerState*
    void* blend_ = nullptr;  // ID3D11BlendState* (opaque)

    float blur_ = 0.0f;
    float dim_  = 0.0f;
    float driftAmount_ = 0.0f;
    float driftSpeed_  = 1.0f;
    bool  blurDirty_ = true;
};

}
