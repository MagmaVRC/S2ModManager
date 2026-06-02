#pragma once
#include <optional>
#include <string>
#include <vector>

namespace platform {

/// <summary>Shows a native folder-picker dialog.</summary>
/// <param name="title">Dialog title.</param>
/// <returns>The chosen folder as a UTF-8 path, or none if cancelled.</returns>
[[nodiscard]] std::optional<std::string> pickFolder(const char* title);

/// <summary>Shows a native file-picker for mod archives (zip/7z/rar), allowing multi-select.</summary>
/// <param name="title">Dialog title.</param>
/// <returns>The chosen files as UTF-8 paths, empty if cancelled.</returns>
[[nodiscard]] std::vector<std::string> pickArchives(const char* title);

/// <summary>Shows a native Save dialog for a single file.</summary>
/// <param name="title">Dialog title.</param>
/// <param name="defaultName">Pre-filled file name (UTF-8), or null.</param>
/// <param name="filterName">Human label for the file-type filter (e.g. L"S2 Profile").</param>
/// <param name="filterPattern">Glob for the filter (e.g. L"*.s2profile").</param>
/// <param name="defaultExt">Extension appended when the user types none (e.g. L"s2profile"), or null.</param>
/// <returns>The chosen path as a UTF-8 path, or none if cancelled.</returns>
[[nodiscard]] std::optional<std::string> saveFile(const char* title, const char* defaultName,
                                                  const wchar_t* filterName, const wchar_t* filterPattern,
                                                  const wchar_t* defaultExt);

/// <summary>Shows a native Open dialog for a single existing file of one type.</summary>
/// <param name="title">Dialog title.</param>
/// <param name="filterName">Human label for the file-type filter (e.g. L"S2 Profile").</param>
/// <param name="filterPattern">Glob for the filter (e.g. L"*.s2profile").</param>
/// <returns>The chosen path as a UTF-8 path, or none if cancelled.</returns>
[[nodiscard]] std::optional<std::string> pickFile(const char* title,
                                                  const wchar_t* filterName, const wchar_t* filterPattern);

/// <summary>Opens a folder in the system file browser (selecting it if it's a file).</summary>
/// <param name="path">UTF-8 path to a folder or file.</param>
void openInExplorer(const std::string& path);

}
