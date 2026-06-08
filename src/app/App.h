#pragma once
#include "../core/Config.h"
#include "../core/Game.h"
#include "../core/ProfileStore.h"
#include "../core/ReshadeShaders.h"
#include "../core/ProfileShareService.h"
#include "../core/UpdateCheck.h"
#include "History.h"
#include "../ui/Anim.h"
#include "../ui/Background.h"
#include "../platform/Win32Window.h"
#include <imgui.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app {

/// <summary>A single mod shown in the list.</summary>
struct ModEntry {
    std::string name;            // raw on-disk stem; the store/undo identity
    std::string author;
    std::string displayName;     // cleaned name parsed from the stem; falls back to name
    std::string version;         // parsed version (e.g. "1.1.2"); empty when unknown
    std::optional<int> nexusId;  // parsed Nexus mod id when the stem is a Nexus download name
    core::ModKind type = core::ModKind::Pak;
    bool enabled = true;
    int modId = 0;   // links the row to its core::ProfileMod in the active profile
    std::string rowKey;  // cached std::to_string(modId) for per-frame lookups
};

/// <summary>Stage of the background UE4SS install worker.</summary>
enum class Ue4ssStage { Idle, Querying, Downloading, Extracting, Done, Failed };

/// <summary>Stage of a background ReShade install or uninstall worker.</summary>
enum class ReshadeStage { Idle, Querying, Downloading, Installing, Verifying, Uninstalling, Done, Failed };

/// <summary>A transient, self-dismissing notification card.</summary>
struct Toast {
    std::string text;
    ImU32 color = IM_COL32(255, 255, 255, 255);
    float ttl = 3.2f;   // seconds remaining
    float age = 0.0f;   // seconds shown
};

/// <summary>Owns application state and renders the single-screen main window each frame.</summary>
class App {
public:
    App();
    ~App();

    /// <summary>Renders one frame into the current (already begun) ImGui frame.</summary>
    /// <param name="displayW">Client area width in pixels.</param>
    /// <param name="displayH">Client area height in pixels.</param>
    void render(int displayW, int displayH);

    /// <summary>Reloads fonts and re-applies the active palette/scale for a new DPI factor.</summary>
    void onScaleChanged(float dpiScale);

    /// <summary>Connects the window so settings can drive vsync and file drops.</summary>
    void attachWindow(platform::Win32Window* win);

    /// <summary>Restores the window position and size from the saved config.</summary>
    void restoreWindow(platform::Win32Window& win);

    /// <summary>Saves the current window position and size to config.</summary>
    void saveWindow(platform::Win32Window& win);

    /// <summary>Releases GPU-backed resources (the background) before the device is torn down.</summary>
    void onShutdown();

private:
    void renderTopBar();
    void renderProfileCombo(float width);
    void renderShareModal();      // P2P share/receive dialog
    void pollProfileShare();      // per-frame: applies a received profile once done
    void renderModList();
    void renderDetails();
    void renderBanners();   // SN2ModSettings dependency + PAK conflict warnings, above the list
    void renderSettingsView(float a);
    void renderOnboarding();
    void renderToasts();

    void saveConfig();
    void applyActivePalette();
    void reloadBackground();   // extract defaults, then (re)load the active image or clear it
    [[nodiscard]] bool backgroundActive() const;          // a background image is loaded and shown
    [[nodiscard]] ImU32 panelBg(ImU32 opaque) const;      // base colour, alpha-scaled when a background is active
    void resolveGame();
    void setGamePath(const std::string& path);
    void detectGame();
    void browseGame();
    void loadMods();             // full reload: reopens Data.dat (blocks on any pending save)
    void syncModsFromStore();    // refresh rows from the in-memory store, no disk reload

    void toast(std::string text, ImU32 color);
    void installMods(const std::vector<std::filesystem::path>& sources);   // starts the background extract worker
    void pollInstall();   // per-frame: installs staged trees into the store once extraction finishes
    void addModsClicked();
    void launchClicked();   // starts the game via Steam (the former "Deploy" button)

    /// <summary>True when a UE4SS mod references SN2ModSettings but its dependency folder is missing.</summary>
    [[nodiscard]] bool sn2ModSettingsMissing() const;
    /// <summary>Scans installed UE4SS mods' text files for a SN2ModSettings reference (require/use).</summary>
    [[nodiscard]] bool anyModNeedsSn2ModSettings() const;
    [[nodiscard]] std::vector<std::string> modConflictWarnings() const;

    void installUe4ss();          // starts the background download/install worker (no-op if already running)
    void pollUe4ssInstall();      // per-frame: applies the result once the worker finishes

    void installReshade();        // starts the background ReShade download/install worker
    void pollReshadeInstall();    // per-frame: applies the install result; reconciles the active preset
    void uninstallReshade();      // starts the background ReShade uninstall worker
    void pollReshadeUninstall();  // per-frame: applies the uninstall result
    void installStandardShaders(int branch);   // 0 = slim, 1 = latest; background download
    void importShaderPack();      // picks a .zip/folder and installs it as a shader pack (background)
    void pollReshadeShader();     // per-frame: applies a shader pack op result
    [[nodiscard]] bool reshadeWorkerActive() const;   // any ReShade worker running (gates profile switch)
    void checkForUpdates(bool notifyWhenCurrent);   // starts a background app update check
    void pollUpdateCheck();       // per-frame: applies update check results once done
    void reorderInStore();        // pushes the current mods_ order into the active profile

    void setModEnabled(int index, bool enabled, bool record = true);   // toggle a mod, optionally recording undo
    void processToggles();   // per-frame: applies one deferred enable/disable so the click stays instant
    void moveMod(int from, int to, bool record = true);                // reorder a mod, optionally recording undo
    void uninstallByName(const std::vector<std::string>& names);       // remove mods by name (no history)

    core::Config config_;
    std::array<char, 512> gamePathBuffer_{};

    platform::Win32Window* window_ = nullptr;
    float uiScaleSetting_ = 1.0f;   // value bound to the Settings slider (the pending choice)
    float uiScaleCurrent_ = 1.0f;   // scale actually applied to the layout; lerps toward the slider value
    float scaleAnimFrom_ = 1.0f;    // scale the lerp started from
    float scaleAnimTo_ = 1.0f;      // scale the lerp eases toward
    float scaleAnimT_ = -1.0f;      // seconds elapsed into the 250ms scale lerp, or <0 when idle

    core::Game game_;
    bool gameReady_ = false;
    bool sn2ModSettingsNeeded_ = false;   // cached: a UE4SS mod references SN2ModSettings (recomputed on load/sync)
    std::vector<std::string> conflictWarnings_;  // cached conflict banners (recomputed on load/sync)
    std::string statusMessage_;

    ui::Background background_;
    bool backgroundDefaultsExtracted_ = false;

    core::ProfileShareService shareService_;
    bool shareModalOpen_ = false;
    bool shareModalWasOpen_ = false;
    int  shareTab_ = 0;                       // 0 = Send, 1 = Receive, 2 = File
    int  hostPortMode_ = 0;                   // Send tab: 0 = UPnP, 1 = manual port forwarding
    std::array<char, 8>  hostPortBuffer_{};   // manual mode: forwarded TCP port
    std::array<char, 64> hostIpBuffer_{};     // manual mode: optional public IP override
    std::array<char, 1024> receiveKeyBuffer_{};
    std::string receivingProfileId_;          // placeholder profile a receive/import imports into
    double keyCopiedAt_ = -100.0;             // time the connection key was last copied (for button feedback)

    std::vector<ModEntry> mods_;
    std::optional<core::ProfileStore> store_;
    int selected_ = -1;
    int dragIndex_ = -1;
    float dragGrabOffsetY_ = 0.0f;

    bool settingsOpen_ = false;
    ui::AnimFloat settingsAnim_{ 0.0f, 0.0f, 24.0f };   // snappier open/close than the 14 default
    ui::AnimFloat shareAnim_{ 0.0f, 0.0f, 24.0f };
    ui::AnimFloat installCount_;   // eased mod count for the count-up subtitle

    std::array<char, 128> searchBuf_{};   // mod-list filter text
    int  typeFilter_ = 0;                 // 0 = all, 1 = PAK, 2 = UE4SS
    std::unordered_map<int, bool> togglePending_;   // modId -> desired enabled
    bool focusSearch_ = false;            // request focus into the search box next frame
    std::unordered_set<int> knownRows_;   // mod ids present last frame, to detect new rows for the enter animation
    ui::AnimFloat dropAnim_;              // 0 = no drag over window, 1 = file dragged over
    bool dragOver_ = false;

    float saveTimer_ = 0.0f;
    bool saveDirty_ = false;
    int pendingUninstall_ = -1;
    bool requestUninstallConfirm_ = false;

    std::vector<Toast> toasts_;
    float dpiScale_ = 1.0f;
    bool fontReloadPending_ = false;
    bool showExtraCols_ = true;
    bool showVerCol_ = true;
    bool compactBar_ = false;
    ui::AnimFloat detailsAnim_{ 1.0f, 1.0f, 14.0f };
    ui::AnimFloat colFadeAnim_{ 1.0f, 1.0f, 14.0f };
    ui::AnimFloat verFadeAnim_{ 1.0f, 1.0f, 14.0f };

    History history_;     // undo/redo stack

    std::thread ue4ssThread_;
    std::atomic<Ue4ssStage> ue4ssStage_{ Ue4ssStage::Idle };
    std::atomic<float> ue4ssProgress_{ 0.0f };
    std::atomic<bool> ue4ssDone_{ false };        // set by worker when finished; cleared by poll
    std::mutex ue4ssMsgMutex_;
    std::string ue4ssMsg_;                         // guarded by ue4ssMsgMutex_

    // ReShade: separate install / uninstall workers (never share done atomics, so a result can't be lost).
    std::optional<core::ReshadeShaders> shaders_;
    std::mutex reshadeMsgMutex_;                    // guards all reshade*Msg_ / version strings below

    std::thread reshadeInstallThread_;
    std::atomic<ReshadeStage> reshadeInstallStage_{ ReshadeStage::Idle };
    std::atomic<float> reshadeInstallProgress_{ 0.0f };
    std::atomic<bool> reshadeInstallDone_{ false };
    std::string reshadeInstallMsg_;
    std::string reshadeInstallVersion_;

    std::thread reshadeUninstallThread_;
    std::atomic<ReshadeStage> reshadeUninstallStage_{ ReshadeStage::Idle };
    std::atomic<bool> reshadeUninstallDone_{ false };
    std::string reshadeUninstallMsg_;

    std::thread reshadeShaderThread_;
    std::atomic<bool> reshadeShaderBusy_{ false };
    std::atomic<bool> reshadeShaderDone_{ false };
    std::atomic<float> reshadeShaderProgress_{ 0.0f };
    std::string reshadeShaderMsg_;                  // guarded by reshadeMsgMutex_

    std::thread updateThread_;
    std::atomic<bool> updateBusy_{ false };
    std::atomic<bool> updateDone_{ false };
    std::atomic<bool> updateNotifyWhenCurrent_{ false };
    std::mutex updateMutex_;
    core::UpdateCheckResult updateResult_;

    struct StagedMod {
        std::filesystem::path tree;       // folder to install from
        std::filesystem::path scratch;    // temp dir to remove after install; empty for dropped folders
        std::string name;                 // display name
        bool extractFailed = false;       // archive couldn't be read
        bool copyFailed = false;          // a PAK sibling failed to stage
        std::string failName;             // source name for the failure toast
    };
    std::thread installThread_;
    std::atomic<bool> installBusy_{ false };       // an ingest is extracting/installing
    std::atomic<bool> installDone_{ false };       // set by worker when extraction finishes; cleared by poll
    std::vector<StagedMod> staged_;                // worker output; read by poll only after join
    std::vector<std::string> installBefore_;       // mod names before install, for undo detection (UI thread only)
    std::filesystem::path installTempRoot_;        // ingest temp root; removed by poll
};

}
