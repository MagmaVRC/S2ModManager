#include "ReshadeInstall.h"
#include "Http.h"
#include "Paths.h"
#include "ReshadeSignature.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>
#include <windows.h>

namespace core {

namespace {

constexpr const char* kHomepage = "https://reshade.me/";
constexpr const char* kFallbackVersion = "6.7.3";
constexpr const char* kFallbackUrl = "https://reshade.me/downloads/ReShade_Setup_6.7.3_Addon.exe";
constexpr DWORD kInstallTimeoutMs = 120000;

bool iContains(std::string h, std::string n) {
    auto low = [](std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    };
    low(h); low(n);
    return h.find(n) != std::string::npos;
}

bool isHelperExe(const std::string& name) {
    return iContains(name, "crashreport") || iContains(name, "cefsubprocess") ||
           iContains(name, "webhelper") || iContains(name, "epicwebhelper") ||
           iContains(name, "subprocess");
}

}  // namespace

std::optional<ReshadeRelease> reshadeQueryLatest() {
    ReshadeRelease rel;
    if (auto body = httpGet(kHomepage)) {
        // Parse the Addon download link straight off the homepage:
        //   href="/downloads/ReShade_Setup_<ver>_Addon.exe"
        static const std::regex re(R"((/downloads/ReShade_Setup_([0-9]+\.[0-9]+\.[0-9]+)_Addon\.exe))");
        std::smatch m;
        if (std::regex_search(*body, m, re)) {
            rel.version = m[2].str();
            rel.setupName = "ReShade_Setup_" + rel.version + "_Addon.exe";
            rel.setupUrl = "https://reshade.me" + m[1].str();
            rel.fromFallback = false;
            return rel;
        }
    }
    rel.version = kFallbackVersion;
    rel.setupName = "ReShade_Setup_6.7.3_Addon.exe";
    rel.setupUrl = kFallbackUrl;
    rel.fromFallback = true;
    return rel;
}

std::optional<std::filesystem::path> resolveGameExe(const GamePaths& paths) {
    std::error_code ec;
    if (!std::filesystem::is_directory(paths.binWin64, ec))
        return std::nullopt;

    std::vector<std::filesystem::path> shipping;
    std::vector<std::filesystem::path> others;
    for (auto it = std::filesystem::directory_iterator(paths.binWin64, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        if (!it->is_regular_file(ec))
            continue;
        if (narrow(it->path().extension().wstring()) != ".exe")
            continue;
        const std::string name = narrow(it->path().filename().wstring());
        if (isHelperExe(name))
            continue;
        if (iContains(name, "-win64-shipping"))
            shipping.push_back(it->path());
        else
            others.push_back(it->path());
    }

    if (shipping.size() == 1)
        return shipping.front();
    if (shipping.empty() && others.size() == 1)
        return others.front();
    return std::nullopt;
}

bool reshadeInstalled(const GamePaths& paths) {
    std::error_code ec;
    return std::filesystem::exists(paths.binWin64 / "dxgi.dll", ec) &&
           std::filesystem::exists(paths.binWin64 / "ReShade.ini", ec);
}

ReshadeResult reshadeInstall(const GamePaths& paths,
                             const std::function<void(ReshadePhase, float)>& onProgress) {
    auto report = [&](ReshadePhase p, float f) { if (onProgress) onProgress(p, f); };

    report(ReshadePhase::Querying, 0.0f);
    auto exe = resolveGameExe(paths);
    if (!exe)
        return { false, "Couldn't find the Subnautica 2 game exe to install ReShade into.", "" };

    auto rel = reshadeQueryLatest();
    if (!rel)
        return { false, "Couldn't resolve a ReShade download.", "" };

    std::error_code ec;
    const std::filesystem::path setup =
        std::filesystem::temp_directory_path(ec) / (L"S2MM_" + std::filesystem::path(rel->setupName).wstring());

    report(ReshadePhase::Downloading, 0.0f);
    if (!downloadFile(rel->setupUrl, setup, [&](float f) { report(ReshadePhase::Downloading, f); }))
        return { false, "Download failed (" + rel->setupName + ").", "" };

    SignatureCheck sig = verifyReshadeSignature(setup);
    if (!sig.ok) {
        std::filesystem::remove(setup, ec);
        return { false, sig.reason, "" };
    }

    report(ReshadePhase::Installing, 0.0f);
    std::wstring cmd = L"\"" + setup.wstring() + L"\" \"" + exe->wstring() + L"\" --api dxgi --headless";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::filesystem::remove(setup, ec);
        return { false, "Couldn't launch the ReShade installer.", "" };
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, kInstallTimeoutMs);
    DWORD exitCode = 1;
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::filesystem::remove(setup, ec);
        std::filesystem::remove(paths.binWin64 / "dxgi.dll", ec);
        std::filesystem::remove(paths.binWin64 / "ReShade.ini", ec);
        return { false, "ReShade installer timed out and was stopped.", "" };
    }
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::filesystem::remove(setup, ec);

    report(ReshadePhase::Verifying, 1.0f);
    if (!reshadeInstalled(paths))
        return { false, "Installer finished but ReShade files are missing - install may be incomplete.", "" };

    std::string msg = "ReShade " + rel->version + " installed.";
    if (rel->fromFallback)
        msg += " (used a built-in fallback version)";
    return { true, msg, rel->version };
}

ReshadeResult reshadeUninstall(const GamePaths& paths) {
    std::error_code ec;
    int removed = 0;
    auto rm = [&](const std::filesystem::path& p) {
        if (std::filesystem::exists(p, ec)) {
            std::filesystem::remove_all(p, ec);
            ++removed;
        }
    };

    rm(paths.binWin64 / "dxgi.dll");
    rm(paths.binWin64 / "ReShade.ini");
    rm(paths.binWin64 / "ReShade.log");
    rm(paths.binWin64 / "reshade-shaders");
    rm(paths.binWin64 / "reshade-presets");
    rm(paths.binWin64 / "reshade-addons");

    return { true, removed > 0 ? "ReShade removed." : "ReShade was not installed.", "" };
}

}
