#pragma once
#include <filesystem>
#include <string>

namespace core {

/// <summary>Resolved Subnautica 2 install paths.</summary>
struct GamePaths {
    std::filesystem::path root;       // .../common/Subnautica2
    std::filesystem::path project;    // root/Subnautica2
    std::filesystem::path binWin64;   // project/Binaries/Win64
    std::filesystem::path ue4ss;      // binWin64/ue4ss
    std::filesystem::path ue4ssMods;  // ue4ss/Mods
    std::filesystem::path pakMods;    // project/Content/Paks/~mods (content/IoStore paks)
    std::filesystem::path logicMods;  // project/Content/Paks/LogicMods (blueprint mods)
};

/// <summary>Locates the game and exposes its derived mod paths.</summary>
class Game {
public:
    /// <summary>Resolves from an explicit root folder, or auto-detects via Steam when empty.</summary>
    [[nodiscard]] static Game resolve(const std::string& overrideRoot);

    /// <summary>Whether a valid Subnautica 2 install was resolved.</summary>
    [[nodiscard]] bool valid() const { return valid_; }

    /// <summary>Whether UE4SS is installed (ue4ss folder and dwmapi.dll present).</summary>
    [[nodiscard]] bool ue4ssInstalled() const;

    /// <summary>Forgets the cached UE4SS check so the next call re-probes the disk (e.g. after install).</summary>
    void invalidateCache() { cachedUe4ss_ = -1; }

    /// <summary>Resolved paths. Empty when not <see cref="valid"/>.</summary>
    [[nodiscard]] const GamePaths& paths() const { return paths_; }

    /// <summary>The game root as a UTF-8 string.</summary>
    [[nodiscard]] std::string rootUtf8() const;

private:
    GamePaths paths_;
    bool valid_ = false;
    mutable int cachedUe4ss_ = -1;
};

}
