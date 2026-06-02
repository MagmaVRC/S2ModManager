#include "Steam.h"
#include "Paths.h"
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace core {

namespace {
constexpr int kAppId = 1962700;

std::optional<std::wstring> steamInstallPath() {
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return std::wstring(buf);
    return std::nullopt;
}

std::string nextQuoted(const std::string& s, std::size_t from) {
    std::size_t a = s.find('"', from);
    if (a == std::string::npos) return {};
    std::size_t b = s.find('"', a + 1);
    if (b == std::string::npos) return {};
    return s.substr(a + 1, b - a - 1);
}

std::string unescape(const std::string& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            switch (v[i + 1]) {
                case '\\': out += '\\'; ++i; break;
                case '"':  out += '"';  ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += v[i]; break;
            }
        } else {
            out += v[i];
        }
    }
    return out;
}

std::string valueAfterKey(const std::string& text, const std::string& key) {
    std::size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return {};
    return nextQuoted(text, k + key.size() + 2);
}

std::vector<std::filesystem::path> libraries(const std::wstring& steam) {
    std::vector<std::filesystem::path> libs;
    libs.emplace_back(steam);

    std::ifstream in(std::filesystem::path(steam) / "steamapps" / "libraryfolders.vdf");
    if (!in) return libs;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::size_t pos = 0;
    while ((pos = text.find("\"path\"", pos)) != std::string::npos) {
        std::string v = unescape(nextQuoted(text, pos + 6));
        if (!v.empty())
            libs.emplace_back(v);
        pos += 6;
    }
    return libs;
}
}

std::optional<std::filesystem::path> findSubnautica2() {
    auto steam = steamInstallPath();
    if (!steam) return std::nullopt;

    for (const auto& lib : libraries(*steam)) {
        std::string installDir = "Subnautica2";
        std::ifstream acf(lib / "steamapps" / ("appmanifest_" + std::to_string(kAppId) + ".acf"));
        if (acf) {
            std::string text((std::istreambuf_iterator<char>(acf)), std::istreambuf_iterator<char>());
            std::string v = valueAfterKey(text, "installdir");
            if (!v.empty() && isSafeName(v))
                installDir = v;
        }
        auto root = lib / "steamapps" / "common" / installDir;
        std::error_code ec;
        if (std::filesystem::exists(root / "Subnautica2.exe", ec))
            return root;
    }
    return std::nullopt;
}

bool launchGame() {
    std::wstring url = L"steam://rungameid/" + std::to_wstring(kAppId);
    HINSTANCE r = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

}
