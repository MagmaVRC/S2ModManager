#pragma once
#include "Game.h"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace core {

/// <summary>A UE4SS release asset resolved from GitHub.</summary>
struct LatestRelease {
    std::string version;     // release tag, e.g. "v3.0.1"
    std::string assetUrl;    // direct download URL for the zip
    std::string assetName;   // e.g. "UE4SS_v3.0.1.zip"
    std::string assetDigest; // GitHub-reported integrity digest, e.g. "sha256:abc..." (may be empty)
};

/// <summary>Result of an install attempt.</summary>
struct InstallResult {
    bool ok = false;
    std::string message;    // human-readable success/failure detail
};

/// <summary>Phases reported through the install progress callback.</summary>
enum class InstallPhase { Querying, Downloading, Extracting };

/// <summary>Queries the GitHub API for the latest UE4SS release, picking the standard (non-debug) zip.</summary>
/// <returns>The release, or nullopt on network/parse failure or if no matching asset exists.</returns>
[[nodiscard]] std::optional<LatestRelease> queryLatest();

/// <summary>Downloads and installs UE4SS into the game's Binaries/Win64 folder.</summary>
/// <param name="paths">Resolved game paths (must be valid).</param>
/// <param name="onProgress">Reports phase + a 0..1 fraction. May be empty.</param>
/// <returns>Result with ok=true only after dwmapi.dll and the ue4ss folder are present.</returns>
[[nodiscard]] InstallResult install(const GamePaths& paths,
                                    const std::function<void(InstallPhase, float)>& onProgress);

}
