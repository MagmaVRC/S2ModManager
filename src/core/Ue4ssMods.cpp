#include "Ue4ssMods.h"
#include "Paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

namespace core {

namespace {
std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}

std::vector<Ue4ssEntry> readUe4ssMods(const std::filesystem::path& modsDir) {
    std::vector<Ue4ssEntry> out;

    std::ifstream in(modsDir / "mods.txt");
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            std::string t = trim(line);
            if (t.empty() || t[0] == ';')
                continue;
            std::size_t colon = t.rfind(':');
            if (colon == std::string::npos)
                continue;
            std::string name = trim(t.substr(0, colon));
            std::string val = trim(t.substr(colon + 1));
            if (!name.empty() && isSafeName(name))
                out.push_back({ name, val == "1" });
        }
    }

    // Folders not yet in mods.txt (e.g. freshly installed) are appended, enabled.
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(modsDir, ec)) {
        if (!de.is_directory(ec))
            continue;
        std::string name = de.path().filename().string();
        if (name == "shared" || !isSafeName(name))
            continue;
        bool known = false;
        for (const auto& e : out)
            if (e.name == name) { known = true; break; }
        if (!known)
            out.push_back({ name, true });
    }
    return out;
}

bool writeUe4ssMods(const std::filesystem::path& modsDir, const std::vector<Ue4ssEntry>& entries) {
    std::vector<Ue4ssEntry> ordered;
    const Ue4ssEntry* keybinds = nullptr;
    for (const auto& e : entries) {
        if (e.name == "Keybinds")
            keybinds = &e;
        else
            ordered.push_back(e);
    }

    // A mod folder's enabled.txt forces UE4SS to load it regardless of mods.txt, so a
    // disabled entry must have its enabled.txt removed. Enabling relies on mods.txt alone.
    std::error_code ec;
    for (const auto& e : entries)
        if (!e.enabled && isSafeName(e.name))
            std::filesystem::remove(modsDir / pathFromUtf8(e.name) / "enabled.txt", ec);

    std::ofstream txt(modsDir / "mods.txt", std::ios::trunc);
    if (!txt) return false;
    for (const auto& e : ordered)
        txt << e.name << " : " << (e.enabled ? 1 : 0) << "\n";
    if (keybinds)
        txt << "\n; Built-in keybinds, do not move up!\n"
            << "Keybinds : " << (keybinds->enabled ? 1 : 0) << "\n";
    txt.close();
    if (txt.fail()) return false;

    nlohmann::json j = nlohmann::json::array();
    for (const auto& e : ordered)
        j.push_back({ {"mod_name", e.name}, {"mod_enabled", e.enabled} });
    if (keybinds)
        j.push_back({ {"mod_name", "Keybinds"}, {"mod_enabled", keybinds->enabled} });

    std::ofstream js(modsDir / "mods.json", std::ios::trunc);
    if (!js) return false;
    js << j.dump(4);
    return js.good();
}

}
