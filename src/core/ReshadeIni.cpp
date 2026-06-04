#include "ReshadeIni.h"
#include "Paths.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace core::ReshadeIni {
namespace {

std::mutex g_iniMutex;

bool iStartsWith(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

bool readLines(const std::filesystem::path& p, std::vector<std::string>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string line;
    std::istringstream ss(content);
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    return true;
}

bool writeLinesAtomic(const std::filesystem::path& p, const std::vector<std::string>& lines) {
    const std::filesystem::path tmp = p.parent_path() / (p.filename().wstring() + L".s2mm_tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        for (const auto& l : lines)
            out << l << "\r\n";
        if (!out.good())
            return false;
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

// Index of the [GENERAL] header line, or -1.
int generalHeader(const std::vector<std::string>& lines) {
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        if (iStartsWith(trim(lines[i]), "[general]"))
            return i;
    return -1;
}

std::string getGeneralKey(const std::vector<std::string>& lines, const std::string& key) {
    int hdr = generalHeader(lines);
    if (hdr < 0)
        return {};
    const std::string needle = key + "=";
    for (std::size_t i = hdr + 1; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (!t.empty() && t.front() == '[')
            break;
        if (iStartsWith(t, needle))
            return t.substr(needle.size());
    }
    return {};
}

void upsertGeneralKey(std::vector<std::string>& lines, const std::string& key, const std::string& value) {
    const std::string entry = key + "=" + value;
    int hdr = generalHeader(lines);
    if (hdr < 0) {
        if (!lines.empty() && !trim(lines.back()).empty())
            lines.push_back("");
        lines.push_back("[GENERAL]");
        lines.push_back(entry);
        return;
    }
    const std::string needle = key + "=";
    for (std::size_t i = hdr + 1; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (!t.empty() && t.front() == '[')
            break;
        if (iStartsWith(t, needle)) {
            lines[i] = entry;
            return;
        }
    }
    lines.insert(lines.begin() + hdr + 1, entry);
}

std::vector<std::string> splitCsv(const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : v) {
        if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(trim(cur));
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }), out.end());
    return out;
}

bool iEquals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && iStartsWith(a, b);
}

// Drops 'remove' (case-insensitive) and ensures 'add' is present; returns the joined CSV.
std::string mergePaths(const std::string& current, const std::string& remove,
                       std::initializer_list<const char*> add) {
    std::vector<std::string> entries = splitCsv(current);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const std::string& e) { return iEquals(e, remove); }),
                  entries.end());
    for (const char* a : add) {
        bool found = false;
        for (const auto& e : entries)
            if (iEquals(e, a)) { found = true; break; }
        if (!found)
            entries.push_back(a);
    }
    std::string out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i) out += ',';
        out += entries[i];
    }
    return out;
}

}  // namespace

bool setPresetPath(const std::filesystem::path& iniPath, const std::filesystem::path& presetAbs) {
    std::lock_guard<std::mutex> lk(g_iniMutex);
    std::error_code ec;
    if (!std::filesystem::exists(iniPath, ec))
        return false;
    std::vector<std::string> lines;
    if (!readLines(iniPath, lines))
        return false;
    upsertGeneralKey(lines, "PresetPath", narrow(presetAbs.wstring()));
    return writeLinesAtomic(iniPath, lines);
}

bool ensureSearchPaths(const std::filesystem::path& iniPath) {
    std::lock_guard<std::mutex> lk(g_iniMutex);
    std::error_code ec;
    if (!std::filesystem::exists(iniPath, ec))
        return false;
    std::vector<std::string> lines;
    if (!readLines(iniPath, lines))
        return false;

    const std::string effects = mergePaths(getGeneralKey(lines, "EffectSearchPaths"),
                                           ".\\reshade-shaders\\Shaders",
                                           { ".\\reshade-shaders\\Shaders\\**" });
    const std::string textures = mergePaths(getGeneralKey(lines, "TextureSearchPaths"),
                                            ".\\reshade-shaders\\Textures",
                                            { ".\\reshade-shaders\\Textures\\**", ".\\reshade-shaders\\Shaders\\**" });
    upsertGeneralKey(lines, "EffectSearchPaths", effects);
    upsertGeneralKey(lines, "TextureSearchPaths", textures);
    return writeLinesAtomic(iniPath, lines);
}

}
