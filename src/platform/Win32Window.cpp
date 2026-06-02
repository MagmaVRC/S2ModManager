#include "Win32Window.h"
#include "../../resources/resource.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <oleidl.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>
#include <filesystem>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
using Microsoft::WRL::ComPtr;

ComPtr<ID3D11Device>           g_device;
ComPtr<ID3D11DeviceContext>    g_context;
ComPtr<IDXGISwapChain>         g_swap;
ComPtr<ID3D11RenderTargetView> g_rtv;
HWND                           g_hwnd = nullptr;
bool                           g_running = true;
bool                           g_minimized = false;
UINT                           g_resizeW = 0;
UINT                           g_resizeH = 0;
const std::function<void(int, int)>* g_frameFn = nullptr;
std::function<void(float)>     g_dpiCb;
std::function<void(std::vector<std::filesystem::path>)> g_dropCb;
std::function<void(bool)>      g_dragStateCb;
std::function<void()>          g_betweenFramesCb;

bool g_vsync = true;
int  g_redrawFrames = 4;

class DropTarget : public IDropTarget {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --ref_; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD, POINTL, DWORD* eff) override {
        hasFiles_ = HasHDrop(obj);
        if (hasFiles_ && g_dragStateCb) g_dragStateCb(true);
        *eff = hasFiles_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* eff) override {
        *eff = hasFiles_ ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (g_dragStateCb) g_dragStateCb(false); hasFiles_ = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD, POINTL, DWORD* eff) override {
        if (g_dragStateCb) g_dragStateCb(false);
        std::vector<std::filesystem::path> paths;
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg;
        if (SUCCEEDED(obj->GetData(&fmt, &stg))) {
            HDROP drop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
            if (drop) {
                UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < n; ++i) {
                    UINT len = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring buf(len, L'\0');
                    DragQueryFileW(drop, i, buf.data(), len + 1);
                    paths.emplace_back(buf);
                }
                GlobalUnlock(stg.hGlobal);
            }
            ReleaseStgMedium(&stg);
        }
        if (g_dropCb && !paths.empty()) g_dropCb(std::move(paths));
        *eff = DROPEFFECT_COPY;
        return S_OK;
    }
private:
    static bool HasHDrop(IDataObject* obj) {
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return obj && obj->QueryGetData(&fmt) == S_OK;
    }
    ULONG ref_ = 1;
    bool hasFiles_ = false;
};

DropTarget* g_dropTarget = nullptr;

const void* g_blurMarker = nullptr;
float       g_blurAmount = 0.0f;

constexpr int kMaxMips = 2;

ComPtr<ID3D11Texture2D>          g_sceneTex;
ComPtr<ID3D11RenderTargetView>   g_sceneRTV;
ComPtr<ID3D11ShaderResourceView> g_sceneSRV;
ComPtr<ID3D11Texture2D>          g_mipTex[kMaxMips];
ComPtr<ID3D11RenderTargetView>   g_mipRTV[kMaxMips];
ComPtr<ID3D11ShaderResourceView> g_mipSRV[kMaxMips];
UINT g_mipW[kMaxMips] = {}, g_mipH[kMaxMips] = {};
int  g_mipN = 0;
UINT g_blurW = 0, g_blurH = 0;

ComPtr<ID3D11VertexShader> g_bVS;
ComPtr<ID3D11PixelShader>  g_bDown, g_bUp;
ComPtr<ID3D11Buffer>       g_bCB;
ComPtr<ID3D11SamplerState> g_bSamp;
ComPtr<ID3D11BlendState>   g_bBlend;

struct BlurParams { float halfX, halfY, pad0, pad1; };

const char* kBlurHlsl = R"hlsl(
cbuffer P : register(b0) { float2 halfpixel; float2 pad; };
Texture2D t : register(t0);
SamplerState s : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1);
    return o;
}
float4 PSDown(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float2 h = halfpixel;
    float4 c = t.Sample(s, uv) * 4.0;
    c += t.Sample(s, uv - h);
    c += t.Sample(s, uv + h);
    c += t.Sample(s, uv + float2(h.x, -h.y));
    c += t.Sample(s, uv - float2(h.x, -h.y));
    return c / 8.0;
}
float4 PSUp(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float2 h = halfpixel;
    float4 c = t.Sample(s, uv + float2(-h.x * 2.0, 0.0));
    c += t.Sample(s, uv + float2(-h.x, h.y)) * 2.0;
    c += t.Sample(s, uv + float2(0.0, h.y * 2.0));
    c += t.Sample(s, uv + float2(h.x, h.y)) * 2.0;
    c += t.Sample(s, uv + float2(h.x * 2.0, 0.0));
    c += t.Sample(s, uv + float2(h.x, -h.y)) * 2.0;
    c += t.Sample(s, uv + float2(0.0, -h.y * 2.0));
    c += t.Sample(s, uv + float2(-h.x, -h.y)) * 2.0;
    return c / 12.0;
}
)hlsl";

bool EnsureBlurPipeline() {
    if (g_bVS && g_bDown && g_bUp && g_bCB && g_bSamp && g_bBlend) return true;
    if (!g_device) return false;
    ComPtr<ID3DBlob> vsb, dnb, upb, err;
    if (FAILED(D3DCompile(kBlurHlsl, std::strlen(kBlurHlsl), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsb, &err))) return false;
    if (FAILED(D3DCompile(kBlurHlsl, std::strlen(kBlurHlsl), nullptr, nullptr, nullptr, "PSDown", "ps_5_0", 0, 0, &dnb, &err))) return false;
    if (FAILED(D3DCompile(kBlurHlsl, std::strlen(kBlurHlsl), nullptr, nullptr, nullptr, "PSUp", "ps_5_0", 0, 0, &upb, &err))) return false;
    if (FAILED(g_device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_bVS))) return false;
    if (FAILED(g_device->CreatePixelShader(dnb->GetBufferPointer(), dnb->GetBufferSize(), nullptr, &g_bDown))) return false;
    if (FAILED(g_device->CreatePixelShader(upb->GetBufferPointer(), upb->GetBufferSize(), nullptr, &g_bUp))) return false;
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(BlurParams);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_device->CreateBuffer(&cbd, nullptr, &g_bCB))) return false;
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_device->CreateSamplerState(&sd, &g_bSamp))) return false;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_device->CreateBlendState(&bd, &g_bBlend))) return false;
    return true;
}

bool MakeRT(UINT w, UINT h, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    tex.Reset(); rtv.Reset(); srv.Reset();
    if (FAILED(g_device->CreateTexture2D(&td, nullptr, &tex))) return false;
    if (FAILED(g_device->CreateRenderTargetView(tex.Get(), nullptr, &rtv))) return false;
    if (FAILED(g_device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return false;
    return true;
}

void CleanupBlur() {
    g_sceneTex.Reset(); g_sceneRTV.Reset(); g_sceneSRV.Reset();
    for (int i = 0; i < kMaxMips; ++i) { g_mipTex[i].Reset(); g_mipRTV[i].Reset(); g_mipSRV[i].Reset(); }
    g_bVS.Reset(); g_bDown.Reset(); g_bUp.Reset(); g_bCB.Reset(); g_bSamp.Reset(); g_bBlend.Reset();
    g_blurW = g_blurH = 0; g_mipN = 0;
}

bool EnsureSceneTargets(UINT w, UINT h) {
    if (w == 0 || h == 0 || !g_device) return false;
    if (g_sceneTex && g_blurW == w && g_blurH == h) return true;
    g_sceneTex.Reset(); g_sceneRTV.Reset(); g_sceneSRV.Reset();
    for (int i = 0; i < kMaxMips; ++i) { g_mipTex[i].Reset(); g_mipRTV[i].Reset(); g_mipSRV[i].Reset(); }
    g_mipN = 0;
    if (!EnsureBlurPipeline()) return false;
    if (!MakeRT(w, h, g_sceneTex, g_sceneRTV, g_sceneSRV)) return false;
    UINT mw = w, mh = h;
    g_mipN = 0;
    for (int i = 0; i < kMaxMips; ++i) {
        mw = mw / 2 < 1 ? 1 : mw / 2;
        mh = mh / 2 < 1 ? 1 : mh / 2;
        if (mw < 8 || mh < 8) break;
        if (!MakeRT(mw, mh, g_mipTex[i], g_mipRTV[i], g_mipSRV[i])) break;
        g_mipW[i] = mw; g_mipH[i] = mh; ++g_mipN;
    }
    g_blurW = w; g_blurH = h;
    return g_mipN > 0;
}

void BlurPass(ID3D11ShaderResourceView* in, ID3D11RenderTargetView* out, UINT dstW, UINT dstH,
              float halfX, float halfY, ID3D11PixelShader* ps) {
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(dstW); vp.Height = static_cast<float>(dstH); vp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtv = out;
    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_context->Map(g_bCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        BlurParams p{ halfX, halfY, 0.0f, 0.0f };
        std::memcpy(m.pData, &p, sizeof(p));
        g_context->Unmap(g_bCB.Get(), 0);
    }
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* nullVB = nullptr; UINT z = 0;
    g_context->IASetVertexBuffers(0, 1, &nullVB, &z, &z);
    g_context->VSSetShader(g_bVS.Get(), nullptr, 0);
    g_context->PSSetShader(ps, nullptr, 0);
    ID3D11Buffer* cb = g_bCB.Get();
    g_context->PSSetConstantBuffers(0, 1, &cb);
    g_context->PSSetShaderResources(0, 1, &in);
    ID3D11SamplerState* sm = g_bSamp.Get();
    g_context->PSSetSamplers(0, 1, &sm);
    const float bf[4] = { 0, 0, 0, 0 };
    g_context->OMSetBlendState(g_bBlend.Get(), bf, 0xffffffff);
    g_context->OMSetDepthStencilState(nullptr, 0);
    g_context->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    g_context->PSSetShaderResources(0, 1, &none);
}

void CreateRTV() {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))))
        g_device->CreateRenderTargetView(back.Get(), nullptr, &g_rtv);
}

void CleanupRTV() { g_rtv.Reset(); }

bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &sd, &g_swap, &g_device, &got, &g_context);
    if (FAILED(hr))
        return false;
    CreateRTV();
    return true;
}

void CleanupDevice() {
    CleanupBlur();
    CleanupRTV();
    g_swap.Reset();
    g_context.Reset();
    g_device.Reset();
}

void ApplyPendingResize() {
    if (g_resizeW == 0 || g_resizeH == 0)
        return;
    CleanupRTV();
    HRESULT hr = g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
    g_resizeW = g_resizeH = 0;
    if (SUCCEEDED(hr))
        CreateRTV();
}

// Also called synchronously from WM_SIZE so drawing continues during the modal resize loop.
void RenderFrame() {
    if (!g_frameFn || g_minimized || !g_rtv)
        return;

    ApplyPendingResize();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    (*g_frameFn)(rc.right - rc.left, rc.bottom - rc.top);

    ImGui::Render();
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ID3D11RenderTargetView* rtv = g_rtv.Get();
    ImDrawData* dd = ImGui::GetDrawData();

    UINT cw = static_cast<UINT>(rc.right - rc.left), ch = static_cast<UINT>(rc.bottom - rc.top);
    int split = -1;
    if (g_blurAmount > 0.001f && g_blurMarker)
        for (int i = 0; i < dd->CmdListsCount; ++i)
            if (dd->CmdLists[i] == g_blurMarker) { split = i; break; }

    bool layered = false;
    if (split > 0 && EnsureBlurPipeline() && EnsureSceneTargets(cw, ch)) {
        ImDrawData sceneDD = *dd, overDD = *dd;
        static ImVector<ImDrawList*> sceneLists, overLists;
        sceneLists.resize(0); overLists.resize(0);
        for (int i = 0; i < dd->CmdListsCount; ++i)
            (i < split ? sceneLists : overLists).push_back(dd->CmdLists[i]);
        sceneDD.CmdLists = sceneLists; sceneDD.CmdListsCount = sceneLists.Size;
        overDD.CmdLists = overLists;   overDD.CmdListsCount = overLists.Size;

        ID3D11RenderTargetView* srtv = g_sceneRTV.Get();
        g_context->OMSetRenderTargets(1, &srtv, nullptr);
        g_context->ClearRenderTargetView(srtv, clear);
        ImGui_ImplDX11_RenderDrawData(&sceneDD);

        float amt = g_blurAmount < 1.0f ? g_blurAmount : 1.0f;
        float k = 0.5f + amt * 2.5f;
        auto srvOf = [&](int lvl) { return lvl < 0 ? g_sceneSRV.Get() : g_mipSRV[lvl].Get(); };
        auto sizeOf = [&](int lvl, UINT& w, UINT& h) {
            if (lvl < 0) { w = g_blurW; h = g_blurH; } else { w = g_mipW[lvl]; h = g_mipH[lvl]; }
        };
        for (int i = 0; i < g_mipN; ++i) {
            UINT sw, sh; sizeOf(i - 1, sw, sh);
            BlurPass(srvOf(i - 1), g_mipRTV[i].Get(), g_mipW[i], g_mipH[i],
                     k * 0.5f / sw, k * 0.5f / sh, g_bDown.Get());
        }
        for (int i = g_mipN - 1; i > 0; --i) {
            BlurPass(g_mipSRV[i].Get(), g_mipRTV[i - 1].Get(), g_mipW[i - 1], g_mipH[i - 1],
                     k * 0.5f / g_mipW[i], k * 0.5f / g_mipH[i], g_bUp.Get());
        }
        BlurPass(g_mipSRV[0].Get(), rtv, g_blurW, g_blurH,
                 k * 0.5f / g_mipW[0], k * 0.5f / g_mipH[0], g_bUp.Get());

        g_context->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(&overDD);
        layered = true;
    }

    if (!layered) {
        g_context->OMSetRenderTargets(1, &rtv, nullptr);
        g_context->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(dd);
    }

    HRESULT hr = g_swap->Present(g_vsync ? 1 : 0, 0);
    if (hr == DXGI_STATUS_OCCLUDED)
        ::Sleep(16);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR: case WM_SETCURSOR:
        case WM_SETFOCUS: case WM_KILLFOCUS:
            if (g_redrawFrames < 4) g_redrawFrames = 4;
            break;
        default: break;
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
        case WM_SIZE:
            g_minimized = (wParam == SIZE_MINIMIZED);
            if (!g_minimized && g_device) {
                g_resizeW = static_cast<UINT>(LOWORD(lParam));
                g_resizeH = static_cast<UINT>(HIWORD(lParam));
                RenderFrame();
            }
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_dpiCb)
                g_dpiCb(static_cast<float>(HIWORD(wParam)) / 96.0f);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 860;
            mmi->ptMinTrackSize.y = 540;
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
}

namespace platform {

bool Win32Window::init(const wchar_t* title, int width, int height) {
    g_running = true;
    g_minimized = false;
    g_resizeW = g_resizeH = 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HINSTANCE hInst = GetModuleHandle(nullptr);
    HICON hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                0, 0, LR_DEFAULTSIZE | LR_SHARED));
    HICON hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
        hInst, hIcon, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"S2MM", hIconSm };
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    g_hwnd = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return false;

    if (!CreateDevice(g_hwnd)) {
        CleanupDevice();
        DestroyWindow(g_hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    OleInitialize(nullptr);
    g_dropTarget = new DropTarget();
    RegisterDragDrop(g_hwnd, g_dropTarget);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());
    return true;
}

void Win32Window::run(const std::function<void(int, int)>& frameFn) {
    g_frameFn = &frameFn;
    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }
        if (!g_running)
            break;

        if (g_minimized) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        RenderFrame();

        // Between frames: safe point to rebuild device resources such as fonts.
        if (g_betweenFramesCb)
            g_betweenFramesCb();

        // Idle unless animating or handling input; the timeout bounds any missed redraw.
        if (g_redrawFrames > 0) {
            --g_redrawFrames;
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, 200, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    g_frameFn = nullptr;
}

float Win32Window::contentScale() const {
    if (!g_hwnd) return 1.0f;
    UINT dpi = GetDpiForWindow(g_hwnd);
    return dpi ? (float)dpi / 96.0f : 1.0f;
}

ID3D11Device* Win32Window::device() const { return g_device.Get(); }

ID3D11DeviceContext* Win32Window::context() const { return g_context.Get(); }

void Win32Window::onDpiChanged(std::function<void(float)> cb) {
    g_dpiCb = std::move(cb);
}

void Win32Window::setVSync(bool on) { g_vsync = on; }

void Win32Window::onFilesDropped(std::function<void(std::vector<std::filesystem::path>)> cb) {
    g_dropCb = std::move(cb);
}

void Win32Window::onDragState(std::function<void(bool)> cb) {
    g_dragStateCb = std::move(cb);
}

void Win32Window::requestRedraw(int frames) {
    if (frames > g_redrawFrames) g_redrawFrames = frames;
}

void Win32Window::invalidateFontTexture() {
    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Win32Window::onBetweenFrames(std::function<void()> cb) {
    g_betweenFramesCb = std::move(cb);
}

void Win32Window::setBackdropBlur(const ImDrawList* markerDrawList, float amount) {
    g_blurMarker = markerDrawList;
    g_blurAmount = amount;
}

void Win32Window::shutdown() {
    if (!g_hwnd) return;
    RevokeDragDrop(g_hwnd);
    if (g_dropTarget) { g_dropTarget->Release(); g_dropTarget = nullptr; }
    OleUninitialize();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    g_dpiCb = {};
    g_dropCb = {};
    g_dragStateCb = {};
    g_betweenFramesCb = {};
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    UnregisterClassW(L"S2MM", GetModuleHandle(nullptr));
}

Win32Window::~Win32Window() { shutdown(); }

}
