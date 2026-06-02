#pragma once
#include <string>

namespace core {

/// <summary>A ReShade preset (.ini) stored as a per-profile VFS blob. Exactly one preset is
/// active per profile; the active one is written into ReShade.ini's PresetPath.</summary>
struct ReshadePreset {
    int         id = 0;     // stable per-profile id; never reused
    std::string name;       // display name and the materialized file stem
};

}
