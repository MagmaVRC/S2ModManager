#include "platform/Win32Window.h"
#include "app/App.h"
#include "ui/Anim.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <windows.h>

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR    pCmdLine,
    _In_ int      nCmdShow)
{

    platform::Win32Window window;
    if (!window.init(L"Subnautica 2 Mod Manager", 1100, 700))
        return 1;

    app::App application;
    application.onScaleChanged(window.contentScale());

    window.onDpiChanged([&application](float scale) {
        application.onScaleChanged(scale);
        ImGui_ImplDX11_InvalidateDeviceObjects();
    });

    application.attachWindow(&window);

    window.run([&](int w, int h) {
        application.render(w, h);
        if (ui::consumeAnimActive())
            window.requestRedraw(2);
    });

    application.onShutdown();
    window.shutdown();
    return 0;
}
