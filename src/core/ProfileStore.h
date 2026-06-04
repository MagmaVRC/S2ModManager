#pragma once
#include "Compression.h"
#include "Game.h"
#include "ReshadePreset.h"
#include "Vfs.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace core {

/// <summary>Which Paks subfolder a PAK mod installs into.</summary>
/// <remarks>"LogicMods" = blueprint mods loaded by BPModLoaderMod; "~mods" = content/IoStore paks.</remarks>
constexpr const char* kLogicMods = "LogicMods";
constexpr const char* kContentMods = "~mods";

/// <summary>Category of a mod within a profile.</summary>
enum class ModKind { Pak, Ue4ss };

/// <summary>A mod belonging to a profile. The profile's VFS store always holds every
/// mod's file bytes; enabled mods are additionally materialized into the game folder.</summary>
struct ProfileMod {
    int id = 0;                       // stable per-profile id, used as the VFS key segment
    ModKind kind = ModKind::Pak;
    std::string name;                 // display name; for UE4SS also the Mods/ folder name
    bool enabled = true;
    std::string stem;                 // PAK: shared base name, e.g. "DP_Markers"
    std::string subdir = kLogicMods;  // PAK: "LogicMods" or "~mods"
    std::vector<std::string> exts;    // PAK: {".pak", ".ucas", ...}
    std::vector<std::string> files;   // UE4SS: paths relative to the mod folder
};

/// <summary>A profile's stable id and display name.</summary>
struct ProfileInfo {
    std::string id;     // stable slug ("p1", "p2", ...); never changes on rename
    std::string name;   // display name
};

/// <summary>A built-in UE4SS mod (ships with the loader) and its current on/off state.</summary>
struct BuiltinMod {
    std::string name;
    bool enabled = true;
};

/// <summary>Owns the managed store (VFS / Data.dat) and every profile. A profile is an
/// isolated mod set; the active profile is materialized into the game folder, and
/// <see cref="activate"/> reconciles the game when switching. PAK and UE4SS mods are both
/// stored as VFS blobs keyed by profile; built-in UE4SS mods are never treated as profile
/// mods and are written to mods.txt at their default states on every activate.</summary>
class ProfileStore {
public:
    explicit ProfileStore(GamePaths paths);
    ~ProfileStore();

    /// <summary>True while a deferred save (the Data.dat write) is still running on a worker
    /// thread. The in-memory model is already up to date; only the on-disk copy is pending.</summary>
    [[nodiscard]] bool saving() const { return committing_.load(); }

    /// <summary>Opens the VFS and reads the profile index. On first run (no index), creates
    /// a profile named <paramref name="initialProfileName"/> and adopts the mods currently
    /// installed in the game into it. Safe to call repeatedly.</summary>
    bool load(const std::string& initialProfileName);

    [[nodiscard]] const std::vector<ProfileInfo>& profiles() const { return profiles_; }
    [[nodiscard]] const std::string& activeId() const { return activeId_; }
    [[nodiscard]] std::string activeName() const;
    [[nodiscard]] const std::vector<ProfileMod>& mods() const { return active_; }

    /// <summary>The built-in UE4SS mods and their states (global, shared across profiles).</summary>
    [[nodiscard]] const std::vector<BuiltinMod>& builtins() const { return builtins_; }
    /// <summary>Enables/disables a built-in UE4SS mod, rewrites mods.txt, and persists it.</summary>
    bool setBuiltinEnabled(const std::string& name, bool enabled);

    /// <summary>Creates an empty profile and returns its id. Does not switch to it.</summary>
    std::string createProfile(const std::string& name);
    bool renameProfile(const std::string& id, const std::string& name);
    /// <summary>Deletes a profile and all its stored mods. Refuses to delete the last profile.</summary>
    bool deleteProfile(const std::string& id);
    /// <summary>Copies a profile (manifest + all mod blobs) into a new one; returns its id.</summary>
    std::string duplicateProfile(const std::string& id, const std::string& newName);

    /// <summary>Switches the active profile: tears the current profile's materialized mods
    /// out of the game folder and materializes the target profile's enabled mods, rewriting
    /// mods.txt. No-op-safe if already active. Returns false only on a hard store error.</summary>
    bool activate(const std::string& id);

    /// <summary>Installs every mod found under a source tree into the active profile
    /// (PAK groups and/or UE4SS mod folders), materializing enabled ones. Returns the count.</summary>
    int installFrom(const std::filesystem::path& sourceTree, const std::string& displayName);

    /// <summary>Enables or disables a mod in the active profile, materializing or removing
    /// its game files. The VFS copy is always retained. Does NOT write Data.dat — call
    /// <see cref="flush"/> once after a batch of toggles to persist them.</summary>
    bool setEnabled(int modId, bool enabled);

    /// <summary>Persists pending in-memory changes to Data.dat on a worker thread (no-op if
    /// nothing is dirty). Used to batch the save after one or more <see cref="setEnabled"/> calls.</summary>
    void flush();

    /// <summary>Reorders the active profile's mods. <paramref name="orderedIds"/> lists mod
    /// ids in the desired sequence; PAK load numbers and UE4SS mods.txt order follow it.</summary>
    bool applyOrder(const std::vector<int>& orderedIds);

    /// <summary>Moves a PAK mod between LogicMods and ~mods (no-op for UE4SS mods).</summary>
    bool setSubdir(int modId, const std::string& subdir);

    /// <summary>Removes a mod from the active profile (game files + VFS blobs + manifest).</summary>
    bool uninstall(int modId);

    /// <summary>A captured mod (record + file bytes) for undo of an uninstall.</summary>
    struct Stashed {
        ProfileMod mod;
        std::vector<std::pair<std::string, Bytes>> files;   // {relName, bytes}
    };
    [[nodiscard]] bool stash(int modId, Stashed& out);
    /// <summary>Re-adds a stashed mod to the active profile. Returns its new mod id, or 0.</summary>
    int restore(const Stashed& s);

    /// <summary>Reads every file of a mod from the store, for export. Each entry is a
    /// relative file name (PAK: "&lt;stem&gt;&lt;ext&gt;"; UE4SS: folder-relative path) and its bytes.</summary>
    [[nodiscard]] bool readModFiles(int modId, std::vector<std::pair<std::string, Bytes>>& out) const;

    /// <summary>Adds a fully specified mod to a given profile's store from in-memory bytes
    /// (P2P import). Does not materialize. Returns the new mod id, or 0.</summary>
    int importModInto(const std::string& profileId, const ProfileMod& spec,
                      const std::vector<std::pair<std::string, Bytes>>& files);

    // ---- ReShade presets (per profile) ----

    /// <summary>The active profile's ReShade presets, in import order.</summary>
    [[nodiscard]] const std::vector<ReshadePreset>& presets() const { return activePresets_; }

    /// <summary>The id of the active profile's selected preset, or 0 if none.</summary>
    [[nodiscard]] int activePresetId() const { return activePresetId_; }

    /// <summary>Imports a ReShade preset .ini from disk into the active profile, materializing it
    /// next to the game exe and selecting it if it is the first. Returns the new id, or 0.</summary>
    int installPreset(const std::filesystem::path& iniFile);

    /// <summary>Selects the active preset for the current profile and rewrites ReShade.ini's
    /// PresetPath. Pass 0 to clear the selection.</summary>
    bool setActivePreset(int presetId);

    /// <summary>Renames a preset, renaming its materialized file and re-pointing ReShade.ini if active.</summary>
    bool renamePreset(int presetId, const std::string& newName);

    /// <summary>Removes a preset from the active profile (VFS blob + materialized file).</summary>
    bool uninstallPreset(int presetId);

    /// <summary>Re-materializes the active profile's presets next to the game exe and re-points
    /// ReShade.ini. Used to reconcile after ReShade is installed.</summary>
    void reapplyReshadePresets();

private:
    void shiftPaksDown(std::size_t afterIdx, int startNum);
    [[nodiscard]] std::string manifestKey(const std::string& pid) const;
    [[nodiscard]] std::string blobKey(const std::string& pid, const ProfileMod& m, const std::string& rel) const;
    [[nodiscard]] std::filesystem::path materializedDir(const ProfileMod& m) const;

    [[nodiscard]] std::vector<ProfileMod> readManifest(const std::string& pid) const;
    void writeManifest(const std::string& pid, const std::vector<ProfileMod>& mods);

    /// <summary>Writes enabled mods' bytes into the game folder. <paramref name="startIndex"/>
    /// skips materializing mods before that position (they are already on disk) while still
    /// counting their enabled PAKs toward the load-order numbering, so installing a mod only
    /// writes the new files instead of re-materializing the whole profile.</summary>
    void materialize(const std::string& pid, const std::vector<ProfileMod>& mods, std::size_t startIndex = 0);
    void teardown(const std::string& pid, const std::vector<ProfileMod>& mods);
    void writeUe4ssList(const std::vector<ProfileMod>& mods);   // mods.txt/json: built-ins + this profile's UE4SS

    [[nodiscard]] std::string presetManifestKey(const std::string& pid) const;
    [[nodiscard]] std::string presetBlobKey(const std::string& pid, int presetId) const;
    [[nodiscard]] std::filesystem::path managedPresetPath(const std::string& name) const;
    [[nodiscard]] int nextPresetId(const std::vector<ReshadePreset>& presets) const;
    void readPresetManifest(const std::string& pid, std::vector<ReshadePreset>& out, int& activeId) const;
    void writePresetManifest(const std::string& pid, const std::vector<ReshadePreset>& presets, int activeId);
    void capturePresetEdits(const std::string& pid, const std::vector<ReshadePreset>& presets);
    void materializePresets(const std::string& pid, const std::vector<ReshadePreset>& presets, int activeId);
    void writeReshadePresetPath(int activeId);

    bool readIndex();
    void writeIndex();
    void loadBuiltins();
    void saveBuiltins();
    void migrateLegacyInto(const std::string& pid);   // adopt current game install on first run
    [[nodiscard]] int nextModId(const std::vector<ProfileMod>& mods) const;
    [[nodiscard]] bool storeModFiles(const std::string& pid, const ProfileMod& m,
                                     const std::vector<std::pair<std::string, Bytes>>& files);

    void commitAsync();          // serialize the current in-memory state to Data.dat on a worker
    void joinCommit() const;     // block until any in-flight async commit finishes (keeps vfs_ single-owner)

    GamePaths paths_;
    Vfs vfs_;
    std::vector<ProfileInfo> profiles_;
    std::string activeId_;
    int nextSeq_ = 1;                 // monotonic source of profile ids
    std::vector<ProfileMod> active_;  // active profile's mods, in load order
    std::vector<BuiltinMod> builtins_;
    std::vector<ReshadePreset> activePresets_;   // active profile's ReShade presets
    int activePresetId_ = 0;                     // selected preset for the active profile, or 0
    bool loaded_ = false;

    // Deferred save. install/uninstall/restore finish their in-memory + game-folder work
    // synchronously, then hand the (slow) Data.dat write to this worker. Every other method
    // that touches vfs_ calls joinCommit() first, so vfs_ only ever has one owner at a time.
    mutable std::thread commitThread_;
    mutable std::atomic<bool> committing_{ false };

    // Serializes every public entry point that touches vfs_ so a background share worker
    // (importModInto / readModFiles) cannot race the UI thread. Recursive: some operations
    // call siblings (deleteProfile -> activate, stash -> uninstall).
    mutable std::recursive_mutex mutex_;
};

}
