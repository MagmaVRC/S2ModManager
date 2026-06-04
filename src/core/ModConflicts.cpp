#include "ModConflicts.h"
#include <algorithm>
#include <cctype>
#include <map>

namespace core {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void appendConflicts(std::vector<ModConflict>& out, ModConflictKind kind,
                     const std::map<std::string, std::vector<std::string>>& groups,
                     const std::map<std::string, std::string>& targets) {
    for (const auto& [key, names] : groups) {
        if (names.size() < 2)
            continue;
        auto target = targets.find(key);
        out.push_back({ kind, target == targets.end() ? key : target->second, names });
    }
}

}

std::vector<ModConflict> detectModConflicts(const std::vector<ProfileMod>& mods) {
    std::map<std::string, std::vector<std::string>> pakGroups;
    std::map<std::string, std::string> pakTargets;
    std::map<std::string, std::vector<std::string>> ue4ssGroups;
    std::map<std::string, std::string> ue4ssTargets;

    for (const auto& m : mods) {
        if (!m.enabled)
            continue;
        if (m.kind == ModKind::Pak) {
            if (m.stem.empty())
                continue;
            const std::string key = m.subdir + "/" + lower(m.stem);
            pakGroups[key].push_back(m.name);
            pakTargets.emplace(key, m.stem);
        } else {
            const std::string key = lower(m.name);
            ue4ssGroups[key].push_back(m.name);
            ue4ssTargets.emplace(key, m.name);
        }
    }

    std::vector<ModConflict> out;
    appendConflicts(out, ModConflictKind::Pak, pakGroups, pakTargets);
    appendConflicts(out, ModConflictKind::Ue4ss, ue4ssGroups, ue4ssTargets);
    return out;
}

}
