#pragma once
#include "ProfileStore.h"
#include <string>
#include <vector>

namespace core {

enum class ModConflictKind { Pak, Ue4ss };

struct ModConflict {
    ModConflictKind kind = ModConflictKind::Pak;
    std::string target;
    std::vector<std::string> names;
};

[[nodiscard]] std::vector<ModConflict> detectModConflicts(const std::vector<ProfileMod>& mods);

}
