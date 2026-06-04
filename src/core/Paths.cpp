#include "Paths.h"
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "Shell32.lib")

namespace core {

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& utf16) {
    if (utf16.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(widen(utf8));
}

std::filesystem::path appConfigDir() {
    static const std::filesystem::path cached = [] {
        PWSTR raw = nullptr;
        std::filesystem::path base;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)))
            base = raw;
        if (raw) CoTaskMemFree(raw);
        if (base.empty()) {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            base = std::filesystem::path(exe).parent_path();
        }
        auto dir = base / L"S2ModManager";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }();
    return cached;
}

std::filesystem::path appConfigFile(const std::wstring& name) {
    return appConfigDir() / name;
}

std::filesystem::path localAppDir() {
    static const std::filesystem::path cached = [] {
        PWSTR raw = nullptr;
        std::filesystem::path base;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
            base = raw;
        if (raw) CoTaskMemFree(raw);
        if (base.empty()) {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            base = std::filesystem::path(exe).parent_path();
        }
        auto dir = base / L"S2MM";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }();
    return cached;
}

std::filesystem::path dataFile() {
    return localAppDir() / L"Data.dat";
}

bool isSafeName(std::string_view name) {
    if (name.empty()) return false;
    if (name.find('/') != std::string_view::npos) return false;
    if (name.find('\\') != std::string_view::npos) return false;
    if (name.find("..") != std::string_view::npos) return false;
    if (name.find(':') != std::string_view::npos) return false;
    return true;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lowerExt(const std::filesystem::path& p) {
    std::string e = narrow(p.extension().wstring());
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool isPakSibling(std::string_view ext) {
    return ext == ".pak" || ext == ".ucas" || ext == ".utoc" || ext == ".sig";
}

}
