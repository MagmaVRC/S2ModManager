#include "Ue4ssInstall.h"
#include "Http.h"
#include "Archive.h"
#include <nlohmann/json.hpp>

namespace core {

namespace {

// Subnautica 2 (UE5) needs the prerelease build; /releases/latest returns the
// old stable one that lacks the dwmapi.dll + ue4ss/ layout.
constexpr const char* kReleasesApi =
    "https://api.github.com/repos/UE4SS-RE/RE-UE4SS/releases?per_page=20";

bool pickAsset(const nlohmann::json& release, LatestRelease& rel) {
    if (!release.contains("assets"))
        return false;
    for (const auto& a : release["assets"]) {
        const std::string name = a.value("name", "");
        if (name.starts_with("UE4SS_v") && name.ends_with(".zip")) {
            rel.version = release.value("tag_name", "");
            rel.assetName = name;
            rel.assetUrl = a.value("browser_download_url", "");
            return !rel.assetUrl.empty();
        }
    }
    return false;
}

}

std::optional<LatestRelease> queryLatest() {
    auto body = httpGet(kReleasesApi);
    if (!body)
        return std::nullopt;

    nlohmann::json j = nlohmann::json::parse(*body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_array())
        return std::nullopt;

    for (const auto& release : j) {
        if (release.value("draft", false))
            continue;
        LatestRelease rel;
        if (pickAsset(release, rel))
            return rel;
    }
    return std::nullopt;
}

InstallResult install(const GamePaths& paths,
                      const std::function<void(InstallPhase, float)>& onProgress) {
    auto report = [&](InstallPhase p, float f) { if (onProgress) onProgress(p, f); };

    report(InstallPhase::Querying, 0.0f);
    auto rel = queryLatest();
    if (!rel)
        return { false, "Couldn't reach GitHub or find a UE4SS release. Check your connection, "
                        "or download it manually from github.com/UE4SS-RE/RE-UE4SS/releases." };

    std::error_code ec;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path(ec) / (L"S2MM_" + std::filesystem::path(rel->assetName).wstring());

    report(InstallPhase::Downloading, 0.0f);
    if (!downloadFile(rel->assetUrl, tmp, [&](float f) { report(InstallPhase::Downloading, f); }))
        return { false, "Download failed (" + rel->assetName + ")." };

    report(InstallPhase::Extracting, 0.0f);
    const std::filesystem::path stage = std::filesystem::temp_directory_path(ec) / L"S2MM_ue4ss_extract";
    std::filesystem::remove_all(stage, ec);
    const bool extracted = extract(tmp, stage, nullptr);
    std::filesystem::remove(tmp, ec);
    if (!extracted) {
        std::filesystem::remove_all(stage, ec);
        return { false, "Couldn't extract the UE4SS archive." };
    }

    // Install the folder that holds dwmapi.dll, not the archive's wrapper folder.
    std::filesystem::path srcRoot;
    for (auto it = std::filesystem::recursive_directory_iterator(stage, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) && it->path().filename() == L"dwmapi.dll") {
            srcRoot = it->path().parent_path();
            break;
        }
    }
    if (srcRoot.empty()) {
        std::filesystem::remove_all(stage, ec);
        return { false, "Extraction finished but dwmapi.dll wasn't found in the archive." };
    }

    std::filesystem::create_directories(paths.binWin64, ec);
    for (auto entry = std::filesystem::directory_iterator(srcRoot, ec);
         !ec && entry != std::filesystem::directory_iterator(); ++entry) {
        std::filesystem::copy(entry->path(), paths.binWin64 / entry->path().filename(),
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        ec.clear();
    }
    std::filesystem::remove_all(stage, ec);

    if (!std::filesystem::exists(paths.ue4ss, ec) ||
        !std::filesystem::exists(paths.binWin64 / "dwmapi.dll", ec))
        return { false, "Extraction finished but UE4SS files are missing — install may be incomplete." };

    return { true, "UE4SS " + (rel->version.empty() ? std::string("installed") : rel->version + " installed") + "." };
}

}
