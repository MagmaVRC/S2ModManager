#pragma once
#include <filesystem>
#include <optional>

namespace core {

/// <summary>Locates the Subnautica 2 install root (Steam AppID 1962700) via the Steam client.</summary>
/// <returns>The game root folder (.../steamapps/common/Subnautica2), or none if not found.</returns>
std::optional<std::filesystem::path> findSubnautica2();

/// <summary>Launches Subnautica 2 through Steam (steam://rungameid/1962700) so UE4SS injects on start.</summary>
/// <returns>True if the launch request was accepted by the shell.</returns>
bool launchGame();

}
