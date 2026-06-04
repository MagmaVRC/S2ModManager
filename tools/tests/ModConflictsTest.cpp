#include "../../src/core/ModConflicts.h"

#include <cassert>

namespace {

core::ProfileMod pak(int id, const char* name, const char* stem, const char* subdir, bool enabled = true) {
    core::ProfileMod m;
    m.id = id;
    m.kind = core::ModKind::Pak;
    m.name = name;
    m.stem = stem;
    m.subdir = subdir;
    m.enabled = enabled;
    return m;
}

core::ProfileMod ue4ss(int id, const char* name, bool enabled = true) {
    core::ProfileMod m;
    m.id = id;
    m.kind = core::ModKind::Ue4ss;
    m.name = name;
    m.enabled = enabled;
    return m;
}

}

int main() {
    std::vector<core::ProfileMod> mods = {
        pak(1, "First Pak", "Shared", core::kContentMods),
        pak(2, "Second Pak", "shared", core::kContentMods),
        pak(3, "Logic Pak", "Shared", core::kLogicMods),
        pak(4, "Disabled Pak", "shared", core::kContentMods, false),
        ue4ss(5, "CoolMod"),
        ue4ss(6, "coolmod"),
        ue4ss(7, "DisabledUe4ss", false),
        ue4ss(8, "disableduE4ss", false),
    };

    const auto conflicts = core::detectModConflicts(mods);

    assert(conflicts.size() == 2);
    assert(conflicts[0].kind == core::ModConflictKind::Pak);
    assert(conflicts[0].target == "Shared");
    assert(conflicts[0].names.size() == 2);
    assert(conflicts[0].names[0] == "First Pak");
    assert(conflicts[0].names[1] == "Second Pak");

    assert(conflicts[1].kind == core::ModConflictKind::Ue4ss);
    assert(conflicts[1].target == "CoolMod");
    assert(conflicts[1].names.size() == 2);
    assert(conflicts[1].names[0] == "CoolMod");
    assert(conflicts[1].names[1] == "coolmod");
}
