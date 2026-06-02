#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace core {

/// <summary>A UE4SS mod entry from mods.txt (folder name + enabled state).</summary>
struct Ue4ssEntry {
    std::string name;
    bool enabled = true;
};

/// <summary>Reads the UE4SS mod list from a Mods folder (mods.txt, falling back to folder enumeration).</summary>
[[nodiscard]] std::vector<Ue4ssEntry> readUe4ssMods(const std::filesystem::path& modsDir);

/// <summary>Writes mods.txt and mods.json. "Keybinds" is always written last with its required comment.</summary>
/// <returns>True on success.</returns>
bool writeUe4ssMods(const std::filesystem::path& modsDir, const std::vector<Ue4ssEntry>& entries);

}
