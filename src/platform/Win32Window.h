#pragma once
#include <functional>
#include <vector>
#include <filesystem>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ImDrawList;

namespace platform {

/// <summary>Owns the Win32 window, the D3D11 device and swap chain, and the ImGui backends.</summary>
class Win32Window {
public:
    /// <summary>Creates the window, graphics device, and ImGui backends.</summary>
    /// <param name="title">Window title.</param>
    /// <param name="width">Initial client width.</param>
    /// <param name="height">Initial client height.</param>
    /// <returns>True on success, false if device or window creation failed.</returns>
    bool init(const wchar_t* title, int width, int height);

    /// <summary>Runs the message and render loop until the window is closed.</summary>
    /// <param name="frameFn">Called once per frame with the current client width and height, inside an active ImGui frame.</param>
    void run(const std::function<void(int, int)>& frameFn);

    /// <summary>Destroys the ImGui backends, graphics device, and window.</summary>
    void shutdown();

    /// <summary>Returns the DPI scale of the window's current monitor (1.0 = 96 DPI).</summary>
    float contentScale() const;

    /// <summary>Returns the D3D11 device backing the swap chain, or null before init.</summary>
    ID3D11Device* device() const;

    /// <summary>Returns the immediate D3D11 context, or null before init.</summary>
    ID3D11DeviceContext* context() const;

    /// <summary>Registers a callback invoked when the window moves to a different-DPI monitor.</summary>
    /// <param name="cb">Receives the new DPI scale factor.</param>
    void onDpiChanged(std::function<void(float)> cb);

    /// <summary>Enables or disables vsync (present interval) at runtime.</summary>
    void setVSync(bool on);

    /// <summary>Registers a callback invoked with the paths of files dropped onto the window.</summary>
    void onFilesDropped(std::function<void(std::vector<std::filesystem::path>)> cb);

    /// <summary>Registers a callback invoked when a file drag enters (true) or leaves/drops (false) the window.</summary>
    void onDragState(std::function<void(bool)> cb);

    /// <summary>Requests that the render loop draw at least <paramref name="frames"/> more frames
    /// without blocking, so ongoing animations stay smooth in the power-saving idle loop.</summary>
    void requestRedraw(int frames = 2);

    /// <summary>Invalidates the backend font/device texture so reloaded fonts take effect.</summary>
    void invalidateFontTexture();

    /// <summary>Registers a callback run after each frame is presented (between ImGui frames),
    /// the only safe place to rebuild the font atlas or other device resources.</summary>
    void onBetweenFrames(std::function<void()> cb);

    /// <summary>Enables a layered backdrop blur: the scene rendered before <paramref name="markerDrawList"/>
    /// is blurred and everything from that draw list on is composited sharp on top. Pass amount 0 to disable.</summary>
    /// <param name="markerDrawList">The overlay window's draw list (split point); null disables.</param>
    /// <param name="amount">Blur strength in [0,1].</param>
    void setBackdropBlur(const ImDrawList* markerDrawList, float amount);

    Win32Window() = default;
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    ~Win32Window();
};

}
