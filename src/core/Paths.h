#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace core {

/// <summary>Widens a UTF-8 string to UTF-16.</summary>
[[nodiscard]] std::wstring widen(std::string_view utf8);

/// <summary>Narrows a UTF-16 string to UTF-8.</summary>
[[nodiscard]] std::string narrow(const std::wstring& utf16);

/// <summary>Builds a path from a UTF-8 string (via UTF-16, so non-ASCII names survive).</summary>
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);

/// <summary>Returns the application config directory (%APPDATA%/S2ModManager), creating it if missing.</summary>
std::filesystem::path appConfigDir();

/// <summary>Resolves a file name inside the application config directory.</summary>
/// <param name="name">File name to append.</param>
/// <returns>Full path under <see cref="appConfigDir"/>. Does not touch the filesystem.</returns>
std::filesystem::path appConfigFile(const std::wstring& name);

/// <summary>Returns the local data directory (%LOCALAPPDATA%/S2MM), creating it if missing.</summary>
std::filesystem::path localAppDir();

/// <summary>Returns the path to the packed data store (%LOCALAPPDATA%/S2MM/Data.dat).</summary>
std::filesystem::path dataFile();

/// <summary>Returns true if the name is safe for use as a filename (no separators or traversal).</summary>
[[nodiscard]] bool isSafeName(std::string_view name);

/// <summary>Strips leading and trailing whitespace.</summary>
[[nodiscard]] std::string trim(std::string_view s);

/// <summary>Returns the lowercased file extension (including the dot) of a path.</summary>
[[nodiscard]] std::string lowerExt(const std::filesystem::path& p);

/// <summary>Returns true for PAK-family extensions (.pak, .ucas, .utoc, .sig).</summary>
[[nodiscard]] bool isPakSibling(std::string_view ext);

}
