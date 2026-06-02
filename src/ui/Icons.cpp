#include "Icons.h"
#include "Anim.h"
#include "../platform/ImageDecode.h"
#include "../platform/ResourceExtract.h"
#include "../../resources/resource.h"
#include <cstdint>
#include <d3d11.h>

namespace ui {

namespace {

IconSet g_icons;

int resourceIdOf(Icon i) {
    switch (i) {
        case Icon::Search:       return IDR_ICON_SEARCH;
        case Icon::File:         return IDR_ICON_FILE;
        case Icon::Folder:       return IDR_ICON_FOLDER;
        case Icon::FolderOpen:   return IDR_ICON_FOLDER_OPEN;
        case Icon::ChevronUp:    return IDR_ICON_CHEVRON_UP;
        case Icon::ChevronDown:  return IDR_ICON_CHEVRON_DOWN;
        case Icon::ArrowUp:      return IDR_ICON_ARROW_UP;
        case Icon::ArrowDown:    return IDR_ICON_ARROW_DOWN;
        case Icon::Settings:     return IDR_ICON_SETTINGS;
        case Icon::Share:        return IDR_ICON_SHARE;
        case Icon::Play:         return IDR_ICON_PLAY;
        case Icon::Plus:         return IDR_ICON_PLUS;
        case Icon::Package:      return IDR_ICON_PACKAGE;
        case Icon::Box:          return IDR_ICON_BOX;
        case Icon::Grip:         return IDR_ICON_GRIP;
        case Icon::Close:        return IDR_ICON_CLOSE;
        case Icon::Trash:        return IDR_ICON_TRASH;
        case Icon::Key:          return IDR_ICON_KEY;
        case Icon::Copy:         return IDR_ICON_COPY;
        case Icon::Check:        return IDR_ICON_CHECK;
        case Icon::Download:     return IDR_ICON_DOWNLOAD;
        case Icon::Upload:       return IDR_ICON_UPLOAD;
        case Icon::Link:         return IDR_ICON_LINK;
        case Icon::CircleCheck:  return IDR_ICON_CIRCLE_CHECK;
        case Icon::Alert:        return IDR_ICON_ALERT;
        case Icon::HardDrive:    return IDR_ICON_HARD_DRIVE;
        case Icon::Send:         return IDR_ICON_SEND;
        case Icon::FileDown:     return IDR_ICON_FILE_DOWN;
        case Icon::FileUp:       return IDR_ICON_FILE_UP;
        case Icon::Wifi:         return IDR_ICON_WIFI;
        case Icon::ChevronLeft:  return IDR_ICON_CHEVRON_LEFT;
        case Icon::Logo:         return IDR_LOGO;
        default:                 return 0;
    }
}

void iconSamplerCallback(const ImDrawList*, const ImDrawCmd*) {
    g_icons.bindSampler();
}

}  // namespace

IconSet& icons() { return g_icons; }

IconSet::~IconSet() { release(); }

void IconSet::init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context)
        return;
    release();
    ctx_ = context;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;   // bilinear within the nearest mip, no inter-mip blur
    sd.MaxAnisotropy = 1;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MipLODBias = 0.0f;             // pick the mip at/just below display size (clean downscale + slight upscale = crisp)
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* samp = nullptr;
    device->CreateSamplerState(&sd, &samp);
    samp_ = samp;

    for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
        std::vector<unsigned char> bytes = platform::readEmbeddedResource(resourceIdOf(static_cast<Icon>(i)));
        if (bytes.empty())
            continue;
        auto img = platform::decodeImageBytes(bytes.data(), bytes.size());
        if (!img || img->w <= 0 || img->h <= 0)
            continue;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = img->w;
        td.Height = img->h;
        td.MipLevels = 0;                 // full chain, filled by GenerateMips
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &tex)))
            continue;
        context->UpdateSubresource(tex, 0, nullptr, img->rgba.data(), img->w * 4, 0);
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(device->CreateShaderResourceView(tex, nullptr, &srv))) {
            tex->Release();
            continue;
        }
        context->GenerateMips(srv);
        tex_[i] = tex;
        srv_[i] = srv;
        loaded_ = true;
    }
}

void IconSet::release() {
    for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
        if (srv_[i]) { static_cast<ID3D11ShaderResourceView*>(srv_[i])->Release(); srv_[i] = nullptr; }
        if (tex_[i]) { static_cast<ID3D11Texture2D*>(tex_[i])->Release(); tex_[i] = nullptr; }
    }
    if (samp_) { static_cast<ID3D11SamplerState*>(samp_)->Release(); samp_ = nullptr; }
    ctx_ = nullptr;
    loaded_ = false;
}

void IconSet::bindSampler() const {
    if (!ctx_ || !samp_)
        return;
    ID3D11SamplerState* samp = static_cast<ID3D11SamplerState*>(samp_);
    static_cast<ID3D11DeviceContext*>(ctx_)->PSSetSamplers(0, 1, &samp);
}

ImTextureID IconSet::tex(Icon i) const {
    int idx = static_cast<int>(i);
    if (idx < 0 || idx >= static_cast<int>(Icon::Count) || !srv_[idx])
        return 0;
    return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv_[idx]));
}

void IconSet::draw(ImDrawList* dl, Icon i, ImVec2 pos, float size, ImU32 tint) const {
    ImTextureID t = tex(i);
    if (!t)
        return;
    ImVec2 a(px(pos.x), px(pos.y));
    drawIconTex(dl, t, a, ImVec2(a.x + size, a.y + size), tint);
}

void drawIconTex(ImDrawList* dl, ImTextureID tex, const ImVec2& a, const ImVec2& b, ImU32 tint) {
    if (!tex)
        return;
    dl->AddCallback(iconSamplerCallback, nullptr);
    dl->AddImage(tex, a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

}
