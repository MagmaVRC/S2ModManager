#pragma once
#include "Game.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace core {

/// <summary>A resolved ReShade Addon setup download.</summary>
struct ReshadeRelease {
    std::string version;        // e.g. "6.7.3"
    std::string setupUrl;       // absolute URL to ReShade_Setup_<ver>_Addon.exe
    std::string setupName;      // e.g. "ReShade_Setup_6.7.3_Addon.exe"
    bool fromFallback = false;  // true when the homepage scrape failed and the baked-in URL was used
};

/// <summary>Result of a ReShade install or uninstall attempt.</summary>
struct ReshadeResult {
    bool ok = false;
    std::string message;
    std::string version;   // installed version on success, when known
};

/// <summary>Phases reported through the install progress callback.</summary>
enum class ReshadePhase { Querying, Downloading, Installing, Verifying };

/// <summary>Scrapes reshade.me for the current Addon-build setup exe, falling back to a baked-in URL.</summary>
/// <returns>The resolved release, or nullopt only if even the fallback URL is unusable.</returns>
[[nodiscard]] std::optional<ReshadeRelease> reshadeQueryLatest();

/// <summary>Locates the UE5 shipping exe under Binaries/Win64 that ReShade must target.</summary>
/// <returns>The shipping exe path, or nullopt if it cannot be unambiguously determined.</returns>
[[nodiscard]] std::optional<std::filesystem::path> resolveGameExe(const GamePaths& paths);

/// <summary>Whether ReShade is installed (dxgi.dll and ReShade.ini present in Binaries/Win64).</summary>
[[nodiscard]] bool reshadeInstalled(const GamePaths& paths);

/// <summary>Downloads the ReShade Addon setup, verifies its signature, and runs it headless
/// against the game exe (DX12 -> dxgi).</summary>
/// <param name="paths">Resolved game paths (must be valid).</param>
/// <param name="onProgress">Reports phase + a 0..1 fraction. May be empty.</param>
/// <returns>Result with ok=true only after dxgi.dll and ReShade.ini are present.</returns>
[[nodiscard]] ReshadeResult reshadeInstall(const GamePaths& paths,
                                           const std::function<void(ReshadePhase, float)>& onProgress);

/// <summary>Removes ReShade's known files and folders from Binaries/Win64, including managed
/// per-profile presets.</summary>
[[nodiscard]] ReshadeResult reshadeUninstall(const GamePaths& paths);

}
