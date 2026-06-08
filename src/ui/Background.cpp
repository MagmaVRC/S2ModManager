#include "Background.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ui {

namespace {

template <class T>
void safeRelease(void*& p) {
    if (p) { static_cast<T*>(p)->Release(); p = nullptr; }
}

// Fullscreen-triangle vertex shader + dual-Kawase down/up sample pixel shaders
// (Marius Bjorge's "Bandwidth-Efficient Rendering", the same scheme modern
// compositors use for their large, soft layer blur). The sample offset comes
// from the b0 constant buffer, already scaled to the input texture's texels.
const char* kShaderSrc = R"hlsl(
cbuffer Params : register(b0) {
    float2 off;   // sample offset in UV space (spread * 0.5 / inputSize)
    int    mode;  // 0 = downsample, 1 = upsample
    int    _pad;
};
Texture2D    tex0  : register(t0);
SamplerState samp0 : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);   // (0,0) (2,0) (0,2)
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 downsample(float2 uv) {
    float4 sum = tex0.Sample(samp0, uv) * 4.0;
    sum += tex0.Sample(samp0, uv - off);
    sum += tex0.Sample(samp0, uv + off);
    sum += tex0.Sample(samp0, uv + float2(off.x, -off.y));
    sum += tex0.Sample(samp0, uv - float2(off.x, -off.y));
    return sum / 8.0;
}

float4 upsample(float2 uv) {
    float4 sum = tex0.Sample(samp0, uv + float2(-off.x * 2.0, 0.0));
    sum += tex0.Sample(samp0, uv + float2(-off.x, off.y)) * 2.0;
    sum += tex0.Sample(samp0, uv + float2(0.0, off.y * 2.0));
    sum += tex0.Sample(samp0, uv + float2(off.x, off.y)) * 2.0;
    sum += tex0.Sample(samp0, uv + float2(off.x * 2.0, 0.0));
    sum += tex0.Sample(samp0, uv + float2(off.x, -off.y)) * 2.0;
    sum += tex0.Sample(samp0, uv + float2(0.0, -off.y * 2.0));
    sum += tex0.Sample(samp0, uv + float2(-off.x, -off.y)) * 2.0;
    return sum / 12.0;
}

float4 PSMain(VSOut i) : SV_Target {
    return mode == 0 ? downsample(i.uv) : upsample(i.uv);
}
)hlsl";

struct Params {
    float offX, offY;
    int   mode;
    int   pad0;
};

}

Background::~Background() { releaseDevice(); }

void Background::setDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    dev_ = device;
    ctx_ = context;
}

bool Background::hasImage() const { return srcSrv_ != nullptr; }

void Background::setBlur(float amount01) {
    amount01 = std::clamp(amount01, 0.0f, 1.0f);
    if (amount01 != blur_) {
        blur_ = amount01;
        blurDirty_ = true;
    }
}

void Background::setDim(float amount01) { dim_ = std::clamp(amount01, 0.0f, 1.0f); }

void Background::setDrift(float amount01, float speed) {
    driftAmount_ = std::clamp(amount01, 0.0f, 1.0f);
    driftSpeed_  = std::clamp(speed, 0.0f, 4.0f);
}

void Background::releasePyramid() {
    for (Level& lv : levels_) {
        safeRelease<ID3D11ShaderResourceView>(lv.srv);
        safeRelease<ID3D11RenderTargetView>(lv.rtv);
        safeRelease<ID3D11Texture2D>(lv.tex);
    }
    levels_.clear();
    baseW_ = baseH_ = 0;
}

void Background::clearImage() {
    safeRelease<ID3D11ShaderResourceView>(srcSrv_);
    safeRelease<ID3D11Texture2D>(srcTex_);
    srcW_ = srcH_ = 0;
    resultSrv_ = nullptr;
    blurDirty_ = true;
}

bool Background::setImage(const platform::DecodedImage& img) {
    if (!dev_ || img.w <= 0 || img.h <= 0 || img.rgba.size() < static_cast<std::size_t>(img.w) * img.h * 4)
        return false;

    clearImage();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = img.w;
    td.Height = img.h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = img.rgba.data();
    srd.SysMemPitch = img.w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev_->CreateTexture2D(&td, &srd, &tex)))
        return false;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(dev_->CreateShaderResourceView(tex, nullptr, &srv))) {
        tex->Release();
        return false;
    }
    srcTex_ = tex;
    srcSrv_ = srv;
    srcW_ = img.w;
    srcH_ = img.h;

    // Force the blur pyramid to be rebuilt for the new source dimensions.
    releasePyramid();
    blurDirty_ = true;
    return true;
}

bool Background::ensurePipeline() {
    if (vs_ && ps_ && cb_ && samp_ && raster_ && blend_)
        return true;
    if (!dev_)
        return false;

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(kShaderSrc, std::strlen(kShaderSrc), nullptr, nullptr, nullptr,
                          "VSMain", "vs_5_0", flags, 0, &vsb, &err))) {
        if (err) err->Release();
        return false;
    }
    if (FAILED(D3DCompile(kShaderSrc, std::strlen(kShaderSrc), nullptr, nullptr, nullptr,
                          "PSMain", "ps_5_0", flags, 0, &psb, &err))) {
        if (err) err->Release();
        vsb->Release();
        return false;
    }

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader*  ps = nullptr;
    HRESULT hr = dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    if (SUCCEEDED(hr))
        hr = dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    vsb->Release();
    psb->Release();
    if (FAILED(hr)) {
        if (vs) vs->Release();
        return false;
    }
    vs_ = vs;
    ps_ = ps;

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(Params);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* cb = nullptr;
    if (FAILED(dev_->CreateBuffer(&cbd, nullptr, &cb)))
        return false;
    cb_ = cb;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* samp = nullptr;
    if (FAILED(dev_->CreateSamplerState(&sd, &samp)))
        return false;
    samp_ = samp;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    if (FAILED(dev_->CreateRasterizerState(&rd, &rs)))
        return false;
    raster_ = rs;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState* bs = nullptr;
    if (FAILED(dev_->CreateBlendState(&bd, &bs)))
        return false;
    blend_ = bs;
    return true;
}

bool Background::ensurePyramid(int levels) {
    if (srcW_ <= 0 || srcH_ <= 0 || levels < 2)
        return false;

    // Downscale so the longest edge is at most 1024 px: cheaper and softens the blur.
    int scale = std::max(1, (std::max(srcW_, srcH_) + 1023) / 1024);
    int baseW = std::max(2, srcW_ / scale);
    int baseH = std::max(2, srcH_ / scale);

    // Reuse the existing chain when the base size and depth already match.
    if (baseW == baseW_ && baseH == baseH_ && static_cast<int>(levels_.size()) == levels)
        return true;

    releasePyramid();

    auto makeLevel = [&](int w, int h) -> Level {
        Level lv{};
        lv.w = w; lv.h = h;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &tex)))
            return lv;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(dev_->CreateRenderTargetView(tex, nullptr, &rtv)) ||
            FAILED(dev_->CreateShaderResourceView(tex, nullptr, &srv))) {
            if (rtv) rtv->Release();
            tex->Release();
            return lv;
        }
        lv.tex = tex; lv.rtv = rtv; lv.srv = srv;
        return lv;
    };

    int w = baseW, h = baseH;
    for (int i = 0; i < levels; ++i) {
        Level lv = makeLevel(w, h);
        if (!lv.tex) { releasePyramid(); return false; }
        levels_.push_back(lv);
        w = std::max(2, w / 2);
        h = std::max(2, h / 2);
    }
    baseW_ = baseW;
    baseH_ = baseH;
    return true;
}

void Background::ensureBlur() {
    if (!hasImage()) {
        resultSrv_ = nullptr;
        return;
    }
    if (!blurDirty_)
        return;

    // More blur => a deeper pyramid (each level doubles the effective radius). Below a
    // small threshold, show the source unblurred. `spread` adds fine control between levels.
    int   levels = 2 + static_cast<int>(std::floor(blur_ * 5.0f));   // 2..7
    float spread = 1.0f + blur_;                                     // 1..2 texel offset
    if (blur_ < 0.02f || !ensurePipeline() || !ensurePyramid(levels)) {
        resultSrv_ = srcSrv_;
        blurDirty_ = false;
        return;
    }
    levels = static_cast<int>(levels_.size());

    auto runPass = [&](void* inSrv, const Level& out, float inW, float inH, int mode) {
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(out.w);
        vp.Height = static_cast<float>(out.h);
        vp.MaxDepth = 1.0f;
        ctx_->RSSetViewports(1, &vp);

        ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(out.rtv);
        ctx_->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(static_cast<ID3D11Buffer*>(cb_), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            Params p{};
            // Offset is expressed in the *input* texture's texels (half-pixel * spread).
            p.offX = spread * 0.5f / inW;
            p.offY = spread * 0.5f / inH;
            p.mode = mode;
            std::memcpy(m.pData, &p, sizeof(p));
            ctx_->Unmap(static_cast<ID3D11Buffer*>(cb_), 0);
        }

        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* nullVB = nullptr;
        UINT stride = 0, offset = 0;
        ctx_->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);

        ctx_->VSSetShader(static_cast<ID3D11VertexShader*>(vs_), nullptr, 0);
        ctx_->PSSetShader(static_cast<ID3D11PixelShader*>(ps_), nullptr, 0);
        ID3D11Buffer* cb = static_cast<ID3D11Buffer*>(cb_);
        ctx_->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(inSrv);
        ctx_->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* samp = static_cast<ID3D11SamplerState*>(samp_);
        ctx_->PSSetSamplers(0, 1, &samp);

        ctx_->RSSetState(static_cast<ID3D11RasterizerState*>(raster_));
        const float bf[4] = { 0, 0, 0, 0 };
        ctx_->OMSetBlendState(static_cast<ID3D11BlendState*>(blend_), bf, 0xffffffff);
        ctx_->OMSetDepthStencilState(nullptr, 0);

        ctx_->Draw(3, 0);

        // Unbind the SRV so the same texture can be a render target in the next pass.
        ID3D11ShaderResourceView* none = nullptr;
        ctx_->PSSetShaderResources(0, 1, &none);
    };

    // Downsample: source -> level0, then each level halves into the next.
    runPass(srcSrv_, levels_[0], static_cast<float>(srcW_), static_cast<float>(srcH_), 0);
    for (int i = 1; i < levels; ++i)
        runPass(levels_[i - 1].srv, levels_[i], static_cast<float>(levels_[i - 1].w),
                static_cast<float>(levels_[i - 1].h), 0);

    // Upsample back up the chain; the final pass lands in level0, our result.
    for (int i = levels - 1; i >= 1; --i)
        runPass(levels_[i].srv, levels_[i - 1], static_cast<float>(levels_[i].w),
                static_cast<float>(levels_[i].h), 1);

    resultSrv_ = levels_[0].srv;
    blurDirty_ = false;
}

bool Background::draw(const ImVec2& displaySize) {
    ensureBlur();
    if (!resultSrv_)
        return false;

    ImVec2 p0(0.0f, 0.0f);
    ImVec2 p1 = displaySize;
    bool driftMoved = false;
    if (driftAmount_ > 0.001f) {
        float extraX = driftAmount_ * 0.06f * displaySize.x;
        float extraY = driftAmount_ * 0.06f * displaySize.y;
        float t = static_cast<float>(ImGui::GetTime()) * driftSpeed_ * 0.15f;

        float dx = (std::sin(t * 1.00f)        * 0.6f + std::sin(t * 0.37f + 2.1f) * 0.4f) * extraX;
        float dy = (std::sin(t * 0.83f + 1.7f) * 0.6f + std::sin(t * 0.31f + 4.3f) * 0.4f) * extraY;

        float zoom = (std::sin(t * 0.21f + 0.6f) * 0.5f + 0.5f) * 0.3f;
        float zx = extraX * zoom;
        float zy = extraY * zoom;

        p0 = ImVec2(-extraX + dx - zx, -extraY + dy - zy);
        p1 = ImVec2(displaySize.x + extraX + dx + zx, displaySize.y + extraY + dy + zy);

        float delta = std::fabs(p0.x - lastP0_.x) + std::fabs(p0.y - lastP0_.y)
                    + std::fabs(p1.x - lastP1_.x) + std::fabs(p1.y - lastP1_.y);
        driftMoved = delta >= 0.5f;
        lastP0_ = p0;
        lastP1_ = p1;
    }

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(resultSrv_)), p0, p1);
    if (dim_ > 0.001f) {
        ImU32 col = IM_COL32(0, 0, 0, static_cast<int>(dim_ * 255.0f));
        bg->AddRectFilled(ImVec2(0, 0), displaySize, col);
    }
    return driftMoved;
}

void Background::releaseDevice() {
    clearImage();
    releasePyramid();
    safeRelease<ID3D11VertexShader>(vs_);
    safeRelease<ID3D11PixelShader>(ps_);
    safeRelease<ID3D11Buffer>(cb_);
    safeRelease<ID3D11SamplerState>(samp_);
    safeRelease<ID3D11RasterizerState>(raster_);
    safeRelease<ID3D11BlendState>(blend_);
    dev_ = nullptr;
    ctx_ = nullptr;
}

}
