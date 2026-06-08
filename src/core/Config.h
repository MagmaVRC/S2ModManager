#pragma once
#include <string>
#include "Palette.h"

namespace core {

/// <summary>Background-image settings. Image paths are per-theme; empty means the
/// extracted built-in default for that theme.</summary>
struct BackgroundConfig {
    bool        enabled = true;       // on by default so the HUD theme reads as underwater glass
    std::string darkImage;        // path; empty => extracted default-dark
    std::string lightImage;       // path; empty => extracted default-light
    std::string subnauticaImage;  // path; empty => extracted default-dark (HUD theme)
    float       blur = 0.35f;         // 0..1
    float       dim = 0.48f;          // 0..1 darkening overlay
    float       panelOpacity = 0.72f; // 0..1 content-panel translucency when active
    float       driftAmount = 0.0f;   // 0..1 slow parallax pan distance (0 = static)
    float       driftSpeed = 1.0f;    // 0..2 relative drift speed multiplier
};

/// <summary>Persisted application settings and last-session state.</summary>
struct Config {
    /// <summary>Root folder of the game install. Empty when not yet located.</summary>
    std::string gamePath;
    /// <summary>Override for the pak mods directory. Empty derives it from <see cref="gamePath"/>.</summary>
    std::string pakModsDir;
    /// <summary>Override for the UE4SS mods directory. Empty derives it from <see cref="gamePath"/>.</summary>
    std::string ue4ssModsDir;
    /// <summary>Managed mod library location. Empty places it under the app config directory.</summary>
    std::string libraryPath;
    /// <summary>Whether UE4SS has been detected or installed in the game.</summary>
    bool        ue4ssInstalled = false;
    /// <summary>Installed ReShade version tag (e.g. "6.7.3"); empty when unknown or absent. Display only.</summary>
    std::string reshadeVersion;
    /// <summary>Active theme mode: "subnautica", "dark", or "light".</summary>
    std::string themeMode = "subnautica";
    /// <summary>Editable Subnautica HUD palette (default theme).</summary>
    Palette subnautica = defaultSubnautica();
    /// <summary>Editable minimal light palette.</summary>
    Palette light = defaultLight();
    /// <summary>Editable minimal dark palette.</summary>
    Palette dark = defaultDark();
    /// <summary>Whether vsync (present interval 1) is enabled.</summary>
    bool vsync = true;
    /// <summary>Render mode: 0 = reduced (idle when static), 1 = always (continuous rendering).</summary>
    int renderMode = 0;
    /// <summary>UI scale multiplier applied on top of DPI (0.5–2.5).</summary>
    float uiScale = 1.0f;
    /// <summary>Name of the currently active profile.</summary>
    std::string activeProfileName = "Vanilla";
    /// <summary>Whether the update check considers prerelease tags as candidates.</summary>
    bool includePrereleases = false;
    /// <summary>Saved window geometry from the previous session (client-area size in pixels).</summary>
    int windowWidth = 0;
    int windowHeight = 0;
    int windowX = -1;
    int windowY = -1;
    bool windowMaximized = false;
    /// <summary>Background-image settings.</summary>
    BackgroundConfig background;

    /// <summary>Returns the palette for the active <see cref="themeMode"/>.</summary>
    Palette& activePalette() {
        if (themeMode == "light") return light;
        if (themeMode == "dark")  return dark;
        return subnautica;
    }

    /// <summary>Returns the configured background image path for the active theme
    /// (empty means use the extracted built-in default).</summary>
    std::string& activeBackgroundImage() {
        if (themeMode == "light") return background.lightImage;
        if (themeMode == "dark")  return background.darkImage;
        return background.subnauticaImage;
    }

    /// <summary>Loads config from the app config directory.</summary>
    /// <returns>Stored config, or defaults when absent or corrupt.</returns>
    [[nodiscard]] static Config load();

    /// <summary>Writes config to the app config directory.</summary>
    /// <returns>True on success, false on I/O error.</returns>
    bool save() const;
};

}
