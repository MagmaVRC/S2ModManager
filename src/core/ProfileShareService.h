#pragma once
#include "Game.h"
#include "ProfileStore.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace core {

/// <summary>Which kind of operation the service is currently running.</summary>
enum class ShareMode { None, Host, Receive, Export, Import };

/// <summary>How a host exposes its listening port to peers.</summary>
struct HostOptions {
    bool          useUpnp   = true;  ///< false: bind a port the user has port-forwarded manually.
    std::uint16_t fixedPort = 0;     ///< Manual mode: the pre-forwarded TCP port to bind.
    std::string   ipOverride;        ///< Manual mode: public IP to publish; empty: auto-detect over HTTPS.
};

/// <summary>Lifecycle of a share/receive operation. The worker thread advances it; the
/// UI thread polls it. <c>Failed</c> always carries a reason in the status message.</summary>
enum class ShareState {
    Idle,
    Preparing,        // gathering the profile / connecting
    MappingPort,      // host: opening the UPnP mapping
    WaitingForPeer,   // host: key is ready, listening
    Authenticating,   // recipient: verifying the host signature
    Transferring,     // streaming files
    Done,
    Failed,
};

/// <summary>Drives peer-to-peer profile transfer on a background thread.
///
/// Symmetric: either side can host. The host maps a port via UPnP, publishes a
/// connection key (external IP + port + Ed25519 public key), and streams the active
/// profile to whoever connects. The recipient pins the key's public key, verifies a
/// signed nonce, then receives and installs the profile.
///
/// While a run is active the owner must not touch the supplied <see cref="ProfileStore"/>
/// from another thread; the UI gates this behind a modal and refreshes afterward.</summary>
class ProfileShareService {
public:
    ProfileShareService() = default;
    ~ProfileShareService();

    ProfileShareService(const ProfileShareService&) = delete;
    ProfileShareService& operator=(const ProfileShareService&) = delete;

    /// <summary>Begins hosting: gather the active profile, expose a port (UPnP or a manually
    /// forwarded one per <paramref name="opts"/>), publish a key, and serve connecting peers
    /// until <see cref="stop"/>. No-op if already running.</summary>
    void startHosting(const GamePaths& paths, ProfileStore& store, const std::string& profileName,
                      const HostOptions& opts = {});

    /// <summary>Begins receiving into a pre-created (empty) profile: parse the key, connect,
    /// authenticate, and import the streamed mods. The caller owns the profile's lifecycle
    /// (rename on success, delete on failure). No-op if already running.</summary>
    void startReceiving(const GamePaths& paths, ProfileStore& store,
                        const std::string& keyText, const std::string& targetProfileId);

    /// <summary>Begins exporting the active profile to a <c>.s2profile</c> file on disk.
    /// No network. No-op if already running.</summary>
    void startExport(ProfileStore& store, const std::string& profileName,
                     const std::filesystem::path& dest);

    /// <summary>Begins importing a <c>.s2profile</c> file into a pre-created (empty) profile.
    /// Mirrors a receive: the caller owns the placeholder profile's lifecycle and
    /// <see cref="takeImportedProfile"/> drives registration. No-op if already running.</summary>
    void startImport(ProfileStore& store, const std::filesystem::path& src,
                     const std::string& targetProfileId);

    /// <summary>Cancels the current operation, tears down the UPnP mapping, and joins.</summary>
    void stop();

    [[nodiscard]] bool busy() const { return running_.load(); }
    [[nodiscard]] bool isHost() const { return isHost_.load(); }
    [[nodiscard]] ShareMode mode() const { return mode_.load(); }
    [[nodiscard]] ShareState state() const { return state_.load(); }
    [[nodiscard]] float progress() const { return progress_.load(); }

    /// <summary>The published connection key (host only); empty until ready.</summary>
    [[nodiscard]] std::string connectionKey() const;

    /// <summary>Latest human-readable status / error message.</summary>
    [[nodiscard]] std::string statusMessage() const;

    /// <summary>The profile name a recipient just imported, taken once. Drives the UI to
    /// register the new profile and refresh the mod list. Returns nullopt until ready.</summary>
    [[nodiscard]] std::optional<std::string> takeImportedProfile();

private:
    void hostWorker(GamePaths paths, ProfileStore* store, std::string profileName, HostOptions opts);
    void receiveWorker(GamePaths paths, ProfileStore* store, std::string keyText, std::string profileId);
    void exportWorker(ProfileStore* store, std::string profileName, std::filesystem::path dest);
    void importWorker(ProfileStore* store, std::filesystem::path src, std::string profileId);
    void setStatus(ShareState state, std::string message);
    void fail(std::string message);

    std::thread             thread_;
    std::atomic<bool>       running_{ false };
    std::atomic<bool>       cancel_{ false };
    std::atomic<bool>       isHost_{ false };
    std::atomic<ShareMode>  mode_{ ShareMode::None };
    std::atomic<ShareState> state_{ ShareState::Idle };
    std::atomic<float>      progress_{ 0.0f };
    std::atomic<std::uint16_t> mappedPort_{ 0 };

    mutable std::mutex mutex_;     // guards the strings below
    std::string status_;
    std::string key_;
    std::string importedProfile_;
    bool        importedReady_ = false;
};

}  // namespace core
