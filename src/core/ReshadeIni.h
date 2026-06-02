#pragma once
#include <filesystem>

namespace core {

/// <summary>The single owner of edits to ReShade.ini. All writers (preset selection and
/// shader search paths) go through these calls so concurrent edits cannot clobber each other:
/// every call serializes on a shared mutex and writes via a temp file + atomic replace.</summary>
namespace ReshadeIni {

/// <summary>Upserts [GENERAL] PresetPath to the given absolute preset path. Does nothing if
/// ReShade.ini is absent (ReShade creates it on first run). Returns true on a successful write.</summary>
bool setPresetPath(const std::filesystem::path& iniPath, const std::filesystem::path& presetAbs);

/// <summary>Ensures [GENERAL] EffectSearchPaths and TextureSearchPaths contain the recursive
/// reshade-shaders roots, replacing any non-recursive form so effects are not listed twice.
/// Does nothing if ReShade.ini is absent. Idempotent.</summary>
bool ensureSearchPaths(const std::filesystem::path& iniPath);

}

}
