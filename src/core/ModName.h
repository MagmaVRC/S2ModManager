#pragma once
#include <optional>
#include <string>

namespace core {

/// <summary>A mod's on-disk stem split into a human-friendly name, version, and Nexus id.</summary>
struct ParsedModName {
    std::string name;             // cleaned display name (never empty)
    std::string version;          // dotted version, e.g. "1.1.2"; empty when unknown
    std::optional<int> nexusId;   // Nexus Mods id when the stem is a Nexus download name
};

/// <summary>Parses a mod file/folder stem.</summary>
/// <remarks>Nexus download names follow <c>Name-modId-v[-v...]-timestamp</c>
/// (e.g. <c>Fin Faster-78-1-0-1778857696</c>). Only when the stem strictly matches that
/// shape — trailing token a plausible 9-10 digit unix timestamp, a numeric mod id, and a
/// numeric version run — are the name, version, and id extracted. Anything else (UE4SS
/// folders, hand-named paks) returns the lightly-cleaned stem with no version or id; the
/// function never guesses.</remarks>
/// <param name="rawStem">The file/folder stem, without extension.</param>
[[nodiscard]] ParsedModName parseModName(const std::string& rawStem);

}
