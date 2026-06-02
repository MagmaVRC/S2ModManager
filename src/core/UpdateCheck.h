#pragma once
#include <optional>
#include <string>

namespace core {

/// <summary>Latest application release metadata resolved from GitHub.</summary>
struct AppRelease {
    std::string version;     // release tag, e.g. "v0.2.0"
    std::string url;         // browser URL for the release
};

/// <summary>Result of checking for a newer S2ModManager release.</summary>
struct UpdateCheckResult {
    bool ok = false;
    bool updateAvailable = false;
    std::string currentVersion;
    std::string latestVersion;
    std::string releaseUrl;
    std::string message;
};

/// <summary>Queries GitHub for the latest non-draft release.</summary>
/// <param name="includePrereleases">When true, prerelease tags are eligible too.</param>
[[nodiscard]] std::optional<AppRelease> queryLatestAppRelease(bool includePrereleases = false);

/// <summary>Checks whether the latest GitHub release is newer than this build.</summary>
/// <param name="includePrereleases">When true, prerelease tags are eligible too.</param>
[[nodiscard]] UpdateCheckResult checkForUpdates(bool includePrereleases = false);

}
