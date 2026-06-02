#pragma once
#include "Game.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace core {

/// <summary>Which crosire/reshade-shaders branch to fetch as the standard pack.</summary>
/// <remarks>Slim = the default set shipped with every ReShade install; Latest = the larger
/// community-curated superset.</remarks>
enum class StandardBranch { Slim, Latest };

/// <summary>An installed shader pack and its on-disk footprint.</summary>
struct ShaderPack {
    std::string name;        // unique, filesystem-safe; the Shaders/Textures subfolder name
    std::string source;      // "crosire:slim", "crosire:latest", or "import:<original-name>"
    bool        enabled = true;
    int         effectCount = 0;
    bool        builtin = false;
};

/// <summary>Phases reported through the install/import progress callback.</summary>
enum class ShaderPhase { Querying, Downloading, Extracting, Installing };

/// <summary>Outcome of an install / import / uninstall operation.</summary>
struct ShaderResult {
    bool ok = false;
    std::string message;
    std::string packName;
};

/// <summary>Manages the global ReShade shader library: packs installed once into the game's
/// reshade-shaders tree and shared across all profiles. Packs live on disk where ReShade reads
/// them, never in the profile VFS; a JSON manifest in the app config dir (keyed by game root)
/// tracks each pack's enabled state for clean uninstall.</summary>
class ReshadeShaders {
public:
    explicit ReshadeShaders(GamePaths paths);

    /// <summary>Reads the manifest for this game root and reconciles it against the on-disk
    /// reshade-shaders tree (drops entries whose folders vanished). Safe to call repeatedly.</summary>
    bool load();

    /// <summary>True when at least one pack is installed.</summary>
    [[nodiscard]] bool anyInstalled() const { return !packs_.empty(); }

    /// <summary>The installed packs, enabled and disabled, in install order.</summary>
    [[nodiscard]] const std::vector<ShaderPack>& packs() const { return packs_; }

    /// <summary>Ensures ReShade.ini's search paths include the recursive reshade-shaders roots.</summary>
    bool ensureRecursiveSearchPaths();

    /// <summary>Downloads and installs the standard crosire/reshade-shaders pack.</summary>
    [[nodiscard]] ShaderResult installStandard(StandardBranch branch,
                                               const std::function<void(ShaderPhase, float)>& onProgress);

    /// <summary>Imports a user shader pack from a .zip or a folder, installing its effects and
    /// textures under subfolders named <paramref name="displayName"/>.</summary>
    [[nodiscard]] ShaderResult importPack(const std::filesystem::path& source,
                                          const std::string& displayName,
                                          const std::function<void(ShaderPhase, float)>& onProgress);

    /// <summary>Enables or disables a pack by moving its subfolders into or out of the search tree.</summary>
    bool setEnabled(const std::string& name, bool enabled);

    /// <summary>Removes a pack: deletes its Shaders/ and Textures/ subfolders and manifest entry.</summary>
    bool uninstall(const std::string& name);

private:
    [[nodiscard]] std::filesystem::path shadersDir() const;
    [[nodiscard]] std::filesystem::path texturesDir() const;
    [[nodiscard]] std::filesystem::path disabledDir() const;
    [[nodiscard]] std::filesystem::path manifestFile() const;
    [[nodiscard]] std::string rootKey() const;

    bool readManifest();
    bool writeManifest() const;
    [[nodiscard]] std::string uniqueName(const std::string& desired) const;
    [[nodiscard]] ShaderResult installTree(const std::filesystem::path& shaderRoot, const std::string& name,
                                           const std::string& source);

    GamePaths paths_;
    std::vector<ShaderPack> packs_;
    bool loaded_ = false;
};

}
