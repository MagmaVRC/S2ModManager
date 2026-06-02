#pragma once
#include "Compression.h"
#include "ProfileStore.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace core {

/// <summary>Which kind of mod a bundle entry describes.</summary>
enum class BundleModKind { Pak, Ue4ss };

/// <summary>One file within a bundled mod.</summary>
struct BundleFileMeta {
    std::string   relPath;     // pak: "<stem><ext>"; ue4ss: path relative to the mod folder
    std::uint64_t rawSize = 0; // uncompressed size, for progress + decompression
};

/// <summary>A single mod inside a shared profile, without file bytes.</summary>
struct BundleMod {
    BundleModKind               kind = BundleModKind::Pak;
    std::string                 name;
    bool                        enabled = true;
    std::string                 stem;    // pak only
    std::string                 subdir;  // pak only: "LogicMods" / "~mods"
    std::vector<BundleFileMeta> files;
};

/// <summary>The transferable description of a profile: its name and every mod it contains.</summary>
struct BundleManifest {
    std::string             profileName;
    std::vector<BundleMod>  mods;

    /// <summary>Total uncompressed bytes across all files (for progress reporting).</summary>
    [[nodiscard]] std::uint64_t totalBytes() const;
};

/// <summary>Serializes a manifest to compact JSON for the wire.</summary>
[[nodiscard]] std::string manifestToJson(const BundleManifest& m);

/// <summary>Parses a manifest from JSON. Returns nullopt on malformed input.</summary>
[[nodiscard]] std::optional<BundleManifest> manifestFromJson(const std::string& json);

/// <summary>Builds a manifest describing the store's active profile (PAK + UE4SS).</summary>
[[nodiscard]] bool gatherManifest(const ProfileStore& store, const std::string& profileName,
                                  BundleManifest& out);

/// <summary>Reads the raw bytes of every file of one manifest mod, in file order (host side).
/// Returns false if any file is missing.</summary>
[[nodiscard]] bool readModBytes(const ProfileStore& store, const BundleManifest& m,
                                std::size_t modIdx, std::vector<Bytes>& filesInOrder);

/// <summary>Imports one received mod into <paramref name="profileId"/> from its file bytes
/// (recipient side). Returns true on success.</summary>
[[nodiscard]] bool writeMod(ProfileStore& store, const std::string& profileId,
                            const BundleMod& mod, const std::vector<Bytes>& filesInOrder);

/// <summary>Packs the store's active profile into a single self-contained <c>.s2profile</c>
/// file (manifest + per-file lzma), the same payload the P2P transfer streams. Returns false
/// on an empty profile or any write error. <paramref name="onProgress"/> may be empty.</summary>
[[nodiscard]] bool exportProfile(const ProfileStore& store, const std::string& profileName,
                                 const std::filesystem::path& dest,
                                 const std::function<void(float)>& onProgress = {});

/// <summary>Reads a <c>.s2profile</c> file and installs it into <paramref name="profileId"/>
/// (the recipient side of an export). Returns the packed profile's name, or nullopt if the
/// file is malformed or any mod fails to install. <paramref name="onProgress"/> may be empty.</summary>
[[nodiscard]] std::optional<std::string> importProfile(ProfileStore& store, const std::string& profileId,
                                 const std::filesystem::path& src,
                                 const std::function<void(float)>& onProgress = {});

}  // namespace core
