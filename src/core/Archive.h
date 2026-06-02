#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace core {

/// <summary>Extracts an archive (zip / 7z / rar / tar and friends) into a directory.</summary>
/// <param name="archive">Path to the archive file. Format and compression are auto-detected.</param>
/// <param name="destDir">Directory to extract into; created if missing.</param>
/// <param name="outEntries">Optional; receives the relative paths of files written.</param>
/// <returns>True if at least one file was extracted and no member escaped <paramref name="destDir"/>.</returns>
/// <remarks>Members with absolute paths or ".." traversal are skipped, never written outside the destination.</remarks>
[[nodiscard]] bool extract(const std::filesystem::path& archive,
                           const std::filesystem::path& destDir,
                           std::vector<std::string>* outEntries = nullptr);

}
