#pragma once
#include <filesystem>
#include <string>

namespace core {

/// <summary>Widens a UTF-8 string to UTF-16.</summary>
[[nodiscard]] std::wstring widen(const std::string& utf8);

/// <summary>Narrows a UTF-16 string to UTF-8.</summary>
[[nodiscard]] std::string narrow(const std::wstring& utf16);

/// <summary>Builds a path from a UTF-8 string (via UTF-16, so non-ASCII names survive).</summary>
[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string& utf8);

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
[[nodiscard]] bool isSafeName(const std::string& name);

}
