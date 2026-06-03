#include "ProfileStore.h"
#include "Paths.h"
#include "ReshadeIni.h"
#include "Ue4ssMods.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <unordered_map>

namespace core {

namespace {

// UE4SS ships these; they live in ue4ss/Mods permanently and are never profile mods.
// Written to mods.txt at these defaults on every activate. "Keybinds" is handled separately.
struct Builtin { const char* name; bool enabled; };
constexpr Builtin kBuiltins[] = {
    { "CheatManagerEnablerMod", true },
    { "ConsoleCommandsMod",     true },
    { "ConsoleEnablerMod",      true },
    { "BPML_GenericFunctions",  true },
    { "BPModLoaderMod",         true },
    { "SplitScreenMod",         false },
    { "LineTraceMod",           false },
};

bool isBuiltin(const std::string& name) {
    if (name == "Keybinds" || name == "shared")
        return true;
    for (const auto& b : kBuiltins)
        if (name == b.name)
            return true;
    return false;
}

bool readFileBytes(const std::filesystem::path& p, Bytes& out) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto size = in.tellg();
    if (size < 0) return false;
    out.resize(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!out.empty())
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return in.good() || in.eof();
}

bool withinDir(const std::filesystem::path& base, const std::filesystem::path& full) {
    const auto rel = full.lexically_normal().lexically_relative(base.lexically_normal());
    return !rel.empty() && *rel.begin() != std::filesystem::path("..");
}

bool writeFileBytes(const std::filesystem::path& p, const Bytes& data) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!data.empty())
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good();
}

std::string lowerExt(const std::filesystem::path& p) {
    std::string e = narrow(p.extension().wstring());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool isPakSibling(const std::string& ext) {
    return ext == ".pak" || ext == ".ucas" || ext == ".utoc" || ext == ".sig";
}

bool looksLikeUe4ssMod(const std::filesystem::path& d) {
    std::error_code ec;
    return std::filesystem::exists(d / "Scripts", ec) ||
           std::filesystem::exists(d / "dlls", ec) ||
           std::filesystem::exists(d / "enabled.txt", ec);
}

// "NNN_" prefix marks a PAK file this manager materialized.
bool isManagedPakFile(const std::string& name) {
    return name.size() > 4 && std::isdigit(static_cast<unsigned char>(name[0]))
        && std::isdigit(static_cast<unsigned char>(name[1]))
        && std::isdigit(static_cast<unsigned char>(name[2])) && name[3] == '_';
}

const char* kindStr(ModKind k) { return k == ModKind::Ue4ss ? "ue4ss" : "pak"; }

}  // namespace

ProfileStore::ProfileStore(GamePaths paths) : paths_(std::move(paths)) {}

ProfileStore::~ProfileStore() {
    if (commitThread_.joinable())
        commitThread_.join();
}

void ProfileStore::joinCommit() const {
    if (commitThread_.joinable())
        commitThread_.join();   // wait out a deferred save before touching vfs_ again
}

void ProfileStore::commitAsync() {
    joinCommit();               // never run two commits at once
    committing_.store(true);
    commitThread_ = std::thread([this] {
        vfs_.commit();
        committing_.store(false);
    });
}

void ProfileStore::flush() {
    commitAsync();   // write pending changes (e.g. batched enable/disable) on the worker; no-op if clean
}

std::string ProfileStore::manifestKey(const std::string& pid) const {
    return "profiles/" + pid + "/manifest.json";
}

std::string ProfileStore::blobKey(const std::string& pid, const ProfileMod& m, const std::string& rel) const {
    return "profiles/" + pid + "/" + kindStr(m.kind) + "/" + std::to_string(m.id) + "/" + rel;
}

std::filesystem::path ProfileStore::materializedDir(const ProfileMod& m) const {
    if (m.kind == ModKind::Ue4ss)
        return paths_.ue4ssMods / pathFromUtf8(m.name);
    return m.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
}

int ProfileStore::nextModId(const std::vector<ProfileMod>& mods) const {
    int n = 0;
    for (const auto& m : mods) n = std::max(n, m.id);
    return n + 1;
}

// ---- index ----

bool ProfileStore::readIndex() {
    std::string txt;
    if (!vfs_.readText("profiles/index.json", txt))
        return false;
    nlohmann::json j = nlohmann::json::parse(txt, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("profiles"))
        return false;
    profiles_.clear();
    for (const auto& jp : j["profiles"])
        profiles_.push_back({ jp.value("id", ""), jp.value("name", "") });
    activeId_ = j.value("activeId", profiles_.empty() ? "" : profiles_.front().id);
    nextSeq_ = j.value("nextSeq", static_cast<int>(profiles_.size()) + 1);
    return !profiles_.empty();
}

void ProfileStore::writeIndex() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : profiles_)
        arr.push_back({ {"id", p.id}, {"name", p.name} });
    nlohmann::json j = { {"activeId", activeId_}, {"nextSeq", nextSeq_}, {"profiles", arr} };
    vfs_.writeText("profiles/index.json", j.dump(2));
}

void ProfileStore::loadBuiltins() {
    builtins_.clear();
    for (const auto& b : kBuiltins)
        builtins_.push_back({ b.name, b.enabled });
    std::string txt;
    if (vfs_.readText("builtins.json", txt)) {
        nlohmann::json j = nlohmann::json::parse(txt, nullptr, false);
        if (j.is_object())
            for (auto& bm : builtins_)
                if (j.contains(bm.name) && j[bm.name].is_boolean())
                    bm.enabled = j[bm.name].get<bool>();
    }
}

void ProfileStore::saveBuiltins() {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& bm : builtins_)
        j[bm.name] = bm.enabled;
    vfs_.writeText("builtins.json", j.dump(2));
}

bool ProfileStore::setBuiltinEnabled(const std::string& name, bool enabled) {
    joinCommit();
    for (auto& b : builtins_)
        if (b.name == name) {
            if (b.enabled == enabled)
                return true;
            b.enabled = enabled;
            saveBuiltins();
            writeUe4ssList(active_);
            return vfs_.commit();
        }
    return false;
}

// ---- manifest ----

std::vector<ProfileMod> ProfileStore::readManifest(const std::string& pid) const {
    std::vector<ProfileMod> out;
    std::string txt;
    if (!vfs_.readText(manifestKey(pid), txt))
        return out;
    nlohmann::json j = nlohmann::json::parse(txt, nullptr, false);
    if (!j.is_array())
        return out;
    for (const auto& e : j) {
        ProfileMod m;
        m.id      = e.value("id", 0);
        m.kind    = e.value("kind", std::string("pak")) == "ue4ss" ? ModKind::Ue4ss : ModKind::Pak;
        m.name    = e.value("name", "");
        m.enabled = e.value("enabled", true);
        m.stem    = e.value("stem", "");
        m.subdir  = e.value("subdir", std::string(kLogicMods));
        m.exts    = e.value("exts", std::vector<std::string>{});
        m.files   = e.value("files", std::vector<std::string>{});
        if (m.id > 0 && !m.name.empty())
            out.push_back(std::move(m));
    }
    return out;
}

void ProfileStore::writeManifest(const std::string& pid, const std::vector<ProfileMod>& mods) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : mods)
        arr.push_back({ {"id", m.id}, {"kind", kindStr(m.kind)}, {"name", m.name},
                        {"enabled", m.enabled}, {"stem", m.stem}, {"subdir", m.subdir},
                        {"exts", m.exts}, {"files", m.files} });
    vfs_.writeText(manifestKey(pid), arr.dump(2));
}

// ---- load + migration ----

bool ProfileStore::load(const std::string& initialProfileName) {
    joinCommit();
    if (!vfs_.open(dataFile()))
        return false;

    loadBuiltins();

    if (readIndex()) {
        active_ = readManifest(activeId_);
        readPresetManifest(activeId_, activePresets_, activePresetId_);
        loaded_ = true;
        return true;
    }

    // First run: create one profile and adopt the mods currently installed in the game.
    const std::string pid = "p" + std::to_string(nextSeq_++);
    profiles_.push_back({ pid, initialProfileName.empty() ? "Vanilla" : initialProfileName });
    activeId_ = pid;
    migrateLegacyInto(pid);
    writeIndex();
    vfs_.commit();
    active_ = readManifest(activeId_);
    loaded_ = true;
    return true;
}

void ProfileStore::migrateLegacyInto(const std::string& pid) {
    std::vector<ProfileMod> adopted;

    // PAK mods from the old single-profile manifest (pakmods.json + paks/NNN blobs).
    std::string pakTxt;
    if (vfs_.readText("pakmods.json", pakTxt)) {
        nlohmann::json j = nlohmann::json::parse(pakTxt, nullptr, false);
        if (j.is_array()) {
            for (const auto& e : j) {
                const int number = e.value("number", 0);
                const std::string stem = e.value("stem", "");
                if (number <= 0 || stem.empty())
                    continue;
                ProfileMod m;
                m.id = nextModId(adopted);
                m.kind = ModKind::Pak;
                m.name = e.value("name", stem);
                m.enabled = e.value("enabled", true);
                m.stem = stem;
                m.subdir = e.value("subdir", std::string(kContentMods));
                const auto exts = e.value("exts", std::vector<std::string>{});
                const std::filesystem::path dir = m.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
                std::vector<std::pair<std::string, Bytes>> files;
                for (const auto& ext : exts) {
                    const std::string fname = std::format("{:03}_{}{}", number, stem, ext);
                    Bytes bytes;
                    const bool ok = m.enabled
                        ? readFileBytes(dir / pathFromUtf8(fname), bytes)
                        : vfs_.read(std::format("paks/{:03}/{}", number, fname), bytes);
                    if (ok) {
                        m.exts.push_back(ext);
                        files.emplace_back(stem + ext, std::move(bytes));
                    }
                }
                if (!m.exts.empty()) {
                    storeModFiles(pid, m, files);
                    adopted.push_back(std::move(m));
                }
            }
        }
    }

    // UE4SS mods: every non-built-in folder under ue4ss/Mods, enabled per mods.txt.
    std::error_code ec;
    if (std::filesystem::is_directory(paths_.ue4ssMods, ec)) {
        for (const auto& entry : readUe4ssMods(paths_.ue4ssMods)) {
            if (isBuiltin(entry.name))
                continue;
            const std::filesystem::path folder = paths_.ue4ssMods / pathFromUtf8(entry.name);
            if (!std::filesystem::is_directory(folder, ec))
                continue;
            ProfileMod m;
            m.id = nextModId(adopted);
            m.kind = ModKind::Ue4ss;
            m.name = entry.name;
            m.enabled = entry.enabled;
            std::vector<std::pair<std::string, Bytes>> files;
            for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (!it->is_regular_file(ec)) continue;
                const auto rel = std::filesystem::relative(it->path(), folder, ec);
                if (ec) { ec.clear(); continue; }
                Bytes bytes;
                if (readFileBytes(it->path(), bytes)) {
                    const std::string relUtf8 = narrow(rel.generic_wstring());
                    m.files.push_back(relUtf8);
                    files.emplace_back(relUtf8, std::move(bytes));
                }
            }
            if (!m.files.empty()) {
                storeModFiles(pid, m, files);
                adopted.push_back(std::move(m));
            }
        }
    }

    writeManifest(pid, adopted);
}

// ---- profile management ----

std::string ProfileStore::activeName() const {
    for (const auto& p : profiles_)
        if (p.id == activeId_)
            return p.name;
    return {};
}

std::string ProfileStore::createProfile(const std::string& name) {
    joinCommit();
    const std::string pid = "p" + std::to_string(nextSeq_++);
    profiles_.push_back({ pid, name });
    writeManifest(pid, {});
    writeIndex();
    vfs_.commit();
    return pid;
}

bool ProfileStore::renameProfile(const std::string& id, const std::string& name) {
    joinCommit();
    for (auto& p : profiles_)
        if (p.id == id) {
            p.name = name;
            writeIndex();
            return vfs_.commit();
        }
    return false;
}

bool ProfileStore::deleteProfile(const std::string& id) {
    joinCommit();
    if (profiles_.size() <= 1)
        return false;
    auto it = std::find_if(profiles_.begin(), profiles_.end(),
                           [&](const ProfileInfo& p) { return p.id == id; });
    if (it == profiles_.end())
        return false;

    vfs_.removePrefix("profiles/" + id + "/");
    profiles_.erase(it);
    if (activeId_ == id)
        activate(profiles_.front().id);   // switches the game over to a surviving profile
    writeIndex();
    return vfs_.commit();
}

std::string ProfileStore::duplicateProfile(const std::string& id, const std::string& newName) {
    joinCommit();
    const std::vector<ProfileMod> src = readManifest(id);
    const std::string pid = "p" + std::to_string(nextSeq_++);
    profiles_.push_back({ pid, newName });
    for (const auto& m : src) {
        std::vector<std::pair<std::string, Bytes>> files;
        const auto rels = m.kind == ModKind::Ue4ss ? m.files : [&] {
            std::vector<std::string> r;
            for (const auto& ext : m.exts) r.push_back(m.stem + ext);
            return r;
        }();
        for (const auto& rel : rels) {
            Bytes bytes;
            if (vfs_.read(blobKey(id, m, rel), bytes))
                files.emplace_back(rel, std::move(bytes));
        }
        storeModFiles(pid, m, files);   // same ids; keys are profile-scoped so no clash
    }
    writeManifest(pid, src);

    std::vector<ReshadePreset> srcPresets;
    int srcActive = 0;
    readPresetManifest(id, srcPresets, srcActive);
    for (const auto& p : srcPresets) {
        Bytes bytes;
        if (vfs_.read(presetBlobKey(id, p.id), bytes))
            vfs_.write(presetBlobKey(pid, p.id), bytes);
    }
    writePresetManifest(pid, srcPresets, srcActive);

    writeIndex();
    vfs_.commit();
    return pid;
}

// ---- materialization ----

bool ProfileStore::storeModFiles(const std::string& pid, const ProfileMod& m,
                                 const std::vector<std::pair<std::string, Bytes>>& files) {
    for (const auto& [rel, bytes] : files)
        vfs_.write(blobKey(pid, m, rel), bytes);
    return true;
}

void ProfileStore::teardown(const std::string& /*pid*/, const std::vector<ProfileMod>& /*mods*/) {
    std::error_code ec;
    // Remove every managed PAK file (NNN_*) from both Paks subfolders.
    for (const auto& dir : { paths_.logicMods, paths_.pakMods }) {
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             !ec && it != std::filesystem::directory_iterator(); ++it) {
            if (it->is_regular_file(ec) && isManagedPakFile(it->path().filename().string()))
                std::filesystem::remove(it->path(), ec);
            ec.clear();
        }
    }
    // Remove every non-built-in UE4SS mod folder.
    for (auto it = std::filesystem::directory_iterator(paths_.ue4ssMods, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        if (!it->is_directory(ec)) { ec.clear(); continue; }
        const std::string name = narrow(it->path().filename().wstring());
        if (!isBuiltin(name))
            std::filesystem::remove_all(it->path(), ec);
        ec.clear();
    }
}

void ProfileStore::materialize(const std::string& pid, const std::vector<ProfileMod>& mods, std::size_t startIndex) {
    std::error_code ec;
    int pakNumber = 0;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        const ProfileMod& m = mods[i];
        const bool enabledPak = m.enabled && m.kind == ModKind::Pak;
        if (enabledPak)
            ++pakNumber;   // count toward numbering even for already-materialized mods
        if (i < startIndex || !m.enabled)
            continue;
        if (m.kind == ModKind::Pak) {
            const std::filesystem::path dir = m.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
            std::filesystem::create_directories(dir, ec);
            for (const auto& ext : m.exts) {
                Bytes bytes;
                const std::filesystem::path target =
                    dir / pathFromUtf8(std::format("{:03}_{}{}", pakNumber, m.stem, ext));
                if (withinDir(dir, target) && vfs_.read(blobKey(pid, m, m.stem + ext), bytes))
                    writeFileBytes(target, bytes);
            }
        } else {
            const std::filesystem::path folder = paths_.ue4ssMods / pathFromUtf8(m.name);
            for (const auto& rel : m.files) {
                Bytes bytes;
                const std::filesystem::path target = folder / pathFromUtf8(rel);
                if (withinDir(paths_.ue4ssMods, target) && vfs_.read(blobKey(pid, m, rel), bytes))
                    writeFileBytes(target, bytes);
            }
        }
    }
    writeUe4ssList(mods);
}

void ProfileStore::writeUe4ssList(const std::vector<ProfileMod>& mods) {
    std::error_code ec;
    std::filesystem::create_directories(paths_.ue4ssMods, ec);

    std::ofstream txt(paths_.ue4ssMods / "mods.txt", std::ios::trunc);
    nlohmann::json arr = nlohmann::json::array();
    if (txt) {
        for (const auto& b : builtins_) {
            txt << b.name << " : " << (b.enabled ? 1 : 0) << "\n";
            arr.push_back({ {"mod_name", b.name}, {"mod_enabled", b.enabled} });
            // enabled.txt would force-load a built-in despite mods.txt : 0.
            if (!b.enabled)
                std::filesystem::remove(paths_.ue4ssMods / pathFromUtf8(b.name) / "enabled.txt", ec);
        }
        for (const auto& m : mods) {
            if (m.kind != ModKind::Ue4ss || !m.enabled)
                continue;
            txt << m.name << " : 1\n";
            arr.push_back({ {"mod_name", m.name}, {"mod_enabled", true} });
        }
        txt << "\n; Built-in keybinds, do not move up!\nKeybinds : 1\n";
    }
    arr.push_back({ {"mod_name", "Keybinds"}, {"mod_enabled", true} });

    std::ofstream js(paths_.ue4ssMods / "mods.json", std::ios::trunc);
    if (js) js << arr.dump(4);
}

// ---- activate / reconcile ----

bool ProfileStore::activate(const std::string& id) {
    joinCommit();
    if (std::find_if(profiles_.begin(), profiles_.end(),
                     [&](const ProfileInfo& p) { return p.id == id; }) == profiles_.end())
        return false;

    std::vector<ProfileMod> target = (id == activeId_) ? active_ : readManifest(id);
    std::vector<ReshadePreset> targetPresets;
    int targetActive = 0;
    readPresetManifest(id, targetPresets, targetActive);

    capturePresetEdits(activeId_, activePresets_);
    teardown(activeId_, active_);
    activeId_ = id;
    active_ = std::move(target);
    materialize(activeId_, active_);

    activePresets_ = std::move(targetPresets);
    activePresetId_ = targetActive;
    materializePresets(activeId_, activePresets_, activePresetId_);

    writeIndex();
    return vfs_.commit();
}

// ---- active-profile mod operations ----

int ProfileStore::installFrom(const std::filesystem::path& sourceTree, const std::string& displayName) {
    if (!loaded_)
        return 0;
    joinCommit();
    std::error_code ec;
    int installed = 0;
    const std::size_t firstNew = active_.size();   // newly added mods land at active_[firstNew..]

    // PAK groups: each .pak plus its same-stem siblings.
    for (auto it = std::filesystem::recursive_directory_iterator(sourceTree, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec) || lowerExt(it->path()) != ".pak")
            continue;
        const std::filesystem::path dir = it->path().parent_path();
        const std::string stem = narrow(it->path().stem().wstring());

        std::string subdir = kLogicMods;
        for (const auto& part : dir)
            if (narrow(part.wstring()) == kContentMods)
                subdir = kContentMods;

        ProfileMod m;
        m.id = nextModId(active_);
        m.kind = ModKind::Pak;
        m.stem = stem;
        m.subdir = subdir;
        m.enabled = true;
        m.name = displayName;

        std::vector<std::pair<std::string, Bytes>> files;
        for (auto sib = std::filesystem::directory_iterator(dir, ec);
             !ec && sib != std::filesystem::directory_iterator(); ++sib) {
            if (!sib->is_regular_file(ec) || narrow(sib->path().stem().wstring()) != stem)
                continue;
            const std::string ext = lowerExt(sib->path());
            if (!isPakSibling(ext))
                continue;
            Bytes bytes;
            if (readFileBytes(sib->path(), bytes)) {
                m.exts.push_back(ext);
                files.emplace_back(stem + ext, std::move(bytes));
            }
        }
        std::sort(m.exts.begin(), m.exts.end());
        if (!m.exts.empty()) {
            storeModFiles(activeId_, m, files);
            active_.push_back(std::move(m));
            ++installed;
        }
    }

    // UE4SS mods: the tree itself, or each qualifying child folder.
    std::vector<std::pair<std::filesystem::path, std::string>> ue4ssDirs;
    if (looksLikeUe4ssMod(sourceTree)) {
        ue4ssDirs.emplace_back(sourceTree, displayName);
    } else {
        for (auto it = std::filesystem::directory_iterator(sourceTree, ec);
             !ec && it != std::filesystem::directory_iterator(); ++it)
            if (it->is_directory(ec) && looksLikeUe4ssMod(it->path()))
                ue4ssDirs.emplace_back(it->path(), narrow(it->path().filename().wstring()));
    }
    for (const auto& [folder, name] : ue4ssDirs) {
        if (!isSafeName(name) || isBuiltin(name))
            continue;
        ProfileMod m;
        m.id = nextModId(active_);
        m.kind = ModKind::Ue4ss;
        m.name = name;
        m.enabled = true;
        std::vector<std::pair<std::string, Bytes>> files;
        for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
            if (!it->is_regular_file(ec)) continue;
            const auto rel = std::filesystem::relative(it->path(), folder, ec);
            if (ec) { ec.clear(); continue; }
            Bytes bytes;
            if (readFileBytes(it->path(), bytes)) {
                const std::string relUtf8 = narrow(rel.generic_wstring());
                m.files.push_back(relUtf8);
                files.emplace_back(relUtf8, std::move(bytes));
            }
        }
        if (!m.files.empty()) {
            storeModFiles(activeId_, m, files);
            active_.push_back(std::move(m));
            ++installed;
        }
    }

    if (installed > 0) {
        writeManifest(activeId_, active_);
        // Only materialize the newly added mods; existing mods stay on disk untouched
        // (their PAK load numbers are unchanged since new mods append after them).
        materialize(activeId_, active_, firstNew);
        commitAsync();   // hand the Data.dat write to a worker; the model is already updated
    }
    return installed;
}

bool ProfileStore::setEnabled(int modId, bool enabled) {
    joinCommit();
    auto it = std::find_if(active_.begin(), active_.end(), [&](const ProfileMod& m) { return m.id == modId; });
    if (it == active_.end())
        return false;
    if (it->enabled == enabled)
        return true;

    const std::size_t idx = static_cast<std::size_t>(it - active_.begin());
    std::error_code ec;

    if (it->kind == ModKind::Pak) {
        // Load number this PAK occupies (enabled) / would occupy: 1 + enabled PAKs before it.
        int before = 0;
        for (std::size_t i = 0; i < idx; ++i)
            if (active_[i].kind == ModKind::Pak && active_[i].enabled)
                ++before;
        const int num = before + 1;
        const std::filesystem::path dir = it->subdir == kContentMods ? paths_.pakMods : paths_.logicMods;

        if (enabled) {
            // Make room: shift following enabled PAKs UP by one (descending = collision-free).
            std::vector<std::size_t> following;
            for (std::size_t i = idx + 1; i < active_.size(); ++i)
                if (active_[i].kind == ModKind::Pak && active_[i].enabled)
                    following.push_back(i);
            for (std::size_t k = following.size(); k-- > 0; ) {
                const ProfileMod& fm = active_[following[k]];
                const int oldNum = num + static_cast<int>(k);   // currently num, num+1, ...
                const std::filesystem::path fdir = fm.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
                for (const auto& ext : fm.exts)
                    std::filesystem::rename(fdir / pathFromUtf8(std::format("{:03}_{}{}", oldNum, fm.stem, ext)),
                                            fdir / pathFromUtf8(std::format("{:03}_{}{}", oldNum + 1, fm.stem, ext)), ec);
            }
            it->enabled = true;
            for (const auto& ext : it->exts) {
                Bytes bytes;
                if (vfs_.read(blobKey(activeId_, *it, it->stem + ext), bytes))
                    writeFileBytes(dir / pathFromUtf8(std::format("{:03}_{}{}", num, it->stem, ext)), bytes);
            }
        } else {
            for (const auto& ext : it->exts)
                std::filesystem::remove(dir / pathFromUtf8(std::format("{:03}_{}{}", num, it->stem, ext)), ec);
            // Shift following enabled PAKs DOWN by one (ascending = collision-free).
            int n = num;
            for (std::size_t i = idx + 1; i < active_.size(); ++i) {
                const ProfileMod& fm = active_[i];
                if (!(fm.kind == ModKind::Pak && fm.enabled))
                    continue;
                const std::filesystem::path fdir = fm.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
                for (const auto& ext : fm.exts)
                    std::filesystem::rename(fdir / pathFromUtf8(std::format("{:03}_{}{}", n + 1, fm.stem, ext)),
                                            fdir / pathFromUtf8(std::format("{:03}_{}{}", n,     fm.stem, ext)), ec);
                ++n;
            }
            it->enabled = false;
        }
    } else {
        it->enabled = enabled;
        const std::filesystem::path folder = paths_.ue4ssMods / pathFromUtf8(it->name);
        if (enabled) {
            for (const auto& rel : it->files) {
                Bytes bytes;
                if (vfs_.read(blobKey(activeId_, *it, rel), bytes))
                    writeFileBytes(folder / pathFromUtf8(rel), bytes);
            }
        } else {
            std::filesystem::remove_all(folder, ec);
        }
    }

    writeManifest(activeId_, active_);
    writeUe4ssList(active_);   // refresh mods.txt (UE4SS on/off); harmless for PAK toggles
    // No commit here: the game folder (source of truth for launch) is already updated. The
    // caller batches the Data.dat write via flush() so rapid toggles don't each rewrite it.
    return true;
}

bool ProfileStore::applyOrder(const std::vector<int>& orderedIds) {
    joinCommit();
    std::vector<ProfileMod> seq;
    seq.reserve(active_.size());
    for (int id : orderedIds) {
        auto it = std::find_if(active_.begin(), active_.end(), [&](const ProfileMod& m) { return m.id == id; });
        if (it != active_.end() &&
            std::find_if(seq.begin(), seq.end(), [&](const ProfileMod& m) { return m.id == id; }) == seq.end())
            seq.push_back(*it);
    }
    for (const auto& m : active_)
        if (std::find_if(seq.begin(), seq.end(), [&](const ProfileMod& s) { return s.id == m.id; }) == seq.end())
            seq.push_back(m);

    // A reorder only changes PAK load numbers (the NNN_ prefix) — never file contents. So just
    // rename the changed files instead of re-materializing every mod. PAK numbering = position
    // among enabled PAKs in load order; compute it for the old and new orders.
    auto pakNumbers = [](const std::vector<ProfileMod>& order) {
        std::unordered_map<int, int> num;
        int n = 0;
        for (const auto& m : order)
            if (m.kind == ModKind::Pak && m.enabled)
                num[m.id] = ++n;
        return num;
    };
    const std::unordered_map<int, int> oldNum = pakNumbers(active_);
    const std::unordered_map<int, int> newNum = pakNumbers(seq);

    auto dirOf = [&](const ProfileMod& m) {
        return m.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
    };
    std::error_code ec;
    // Two passes (old number -> temp -> new number) so a permutation can't collide mid-rename.
    for (const auto& m : seq) {
        if (m.kind != ModKind::Pak || !m.enabled || oldNum.at(m.id) == newNum.at(m.id))
            continue;
        const std::filesystem::path dir = dirOf(m);
        for (const auto& ext : m.exts)
            std::filesystem::rename(dir / pathFromUtf8(std::format("{:03}_{}{}", oldNum.at(m.id), m.stem, ext)),
                                    dir / pathFromUtf8(std::format("__reorder_{}{}", m.id, ext)), ec);
    }
    for (const auto& m : seq) {
        if (m.kind != ModKind::Pak || !m.enabled || oldNum.at(m.id) == newNum.at(m.id))
            continue;
        const std::filesystem::path dir = dirOf(m);
        for (const auto& ext : m.exts)
            std::filesystem::rename(dir / pathFromUtf8(std::format("__reorder_{}{}", m.id, ext)),
                                    dir / pathFromUtf8(std::format("{:03}_{}{}", newNum.at(m.id), m.stem, ext)), ec);
    }

    active_ = std::move(seq);
    writeManifest(activeId_, active_);
    writeUe4ssList(active_);   // UE4SS load order in mods.txt follows the new sequence
    commitAsync();
    return true;
}

bool ProfileStore::setSubdir(int modId, const std::string& subdir) {
    joinCommit();
    for (auto& m : active_)
        if (m.id == modId && m.kind == ModKind::Pak) {
            if (m.subdir == subdir)
                return true;
            m.subdir = subdir;
            writeManifest(activeId_, active_);
            teardown(activeId_, active_);
            materialize(activeId_, active_);
            return vfs_.commit();
        }
    return false;
}

bool ProfileStore::uninstall(int modId) {
    joinCommit();
    auto it = std::find_if(active_.begin(), active_.end(), [&](const ProfileMod& m) { return m.id == modId; });
    if (it == active_.end())
        return false;
    const ProfileMod victim = *it;
    const std::size_t victimIndex = static_cast<std::size_t>(it - active_.begin());

    // Remove only the victim's materialized files and renumber the PAKs after it, instead of
    // tearing down and re-materializing the whole profile (which re-decompressed every mod).
    std::error_code ec;
    if (victim.enabled && victim.kind == ModKind::Pak) {
        // 1-based load number of the victim among enabled PAKs (in load order).
        int victimNum = 0;
        for (std::size_t i = 0; i <= victimIndex; ++i)
            if (active_[i].enabled && active_[i].kind == ModKind::Pak)
                ++victimNum;
        const std::filesystem::path vdir = victim.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
        for (const auto& ext : victim.exts)
            std::filesystem::remove(vdir / pathFromUtf8(std::format("{:03}_{}{}", victimNum, victim.stem, ext)), ec);
        // Shift every following enabled PAK down by one number — a rename, no re-decompress.
        // Ascending order is collision-free: each target slot was just vacated.
        int num = victimNum;
        for (std::size_t i = victimIndex + 1; i < active_.size(); ++i) {
            const ProfileMod& m = active_[i];
            if (!(m.enabled && m.kind == ModKind::Pak))
                continue;
            const std::filesystem::path dir = m.subdir == kContentMods ? paths_.pakMods : paths_.logicMods;
            for (const auto& ext : m.exts)
                std::filesystem::rename(dir / pathFromUtf8(std::format("{:03}_{}{}", num + 1, m.stem, ext)),
                                        dir / pathFromUtf8(std::format("{:03}_{}{}", num,     m.stem, ext)), ec);
            ++num;
        }
    } else if (victim.enabled && victim.kind == ModKind::Ue4ss) {
        std::filesystem::remove_all(paths_.ue4ssMods / pathFromUtf8(victim.name), ec);
    }

    vfs_.removePrefix("profiles/" + activeId_ + "/" + kindStr(victim.kind) + "/" + std::to_string(victim.id) + "/");
    active_.erase(active_.begin() + victimIndex);
    writeManifest(activeId_, active_);
    writeUe4ssList(active_);   // drop a removed UE4SS mod from mods.txt (cheap; no-op for PAK)
    commitAsync();             // defer the Data.dat write to a worker
    return true;
}

bool ProfileStore::stash(int modId, Stashed& out) {
    auto it = std::find_if(active_.begin(), active_.end(), [&](const ProfileMod& m) { return m.id == modId; });
    if (it == active_.end())
        return false;
    out.mod = *it;
    if (!readModFiles(modId, out.files))
        return false;
    return uninstall(modId);
}

int ProfileStore::restore(const Stashed& s) {
    joinCommit();
    const std::size_t firstNew = active_.size();
    ProfileMod m = s.mod;
    m.id = nextModId(active_);
    storeModFiles(activeId_, m, s.files);
    active_.push_back(m);
    writeManifest(activeId_, active_);
    materialize(activeId_, active_, firstNew);   // materialize only the restored mod
    commitAsync();
    return m.id;
}

bool ProfileStore::readModFiles(int modId, std::vector<std::pair<std::string, Bytes>>& out) const {
    joinCommit();
    auto it = std::find_if(active_.begin(), active_.end(), [&](const ProfileMod& m) { return m.id == modId; });
    if (it == active_.end())
        return false;
    out.clear();
    if (it->kind == ModKind::Ue4ss) {
        for (const auto& rel : it->files) {
            Bytes bytes;
            if (!vfs_.read(blobKey(activeId_, *it, rel), bytes))
                return false;
            out.emplace_back(rel, std::move(bytes));
        }
    } else {
        for (const auto& ext : it->exts) {
            Bytes bytes;
            if (!vfs_.read(blobKey(activeId_, *it, it->stem + ext), bytes))
                return false;
            out.emplace_back(it->stem + ext, std::move(bytes));
        }
    }
    return true;
}

int ProfileStore::importModInto(const std::string& profileId, const ProfileMod& spec,
                                const std::vector<std::pair<std::string, Bytes>>& files) {
    joinCommit();
    const bool isActive = profileId == activeId_;
    std::vector<ProfileMod> mods = isActive ? active_ : readManifest(profileId);

    ProfileMod m = spec;
    m.id = nextModId(mods);
    if (m.kind == ModKind::Ue4ss) {
        m.files.clear();
        for (const auto& [rel, _] : files) m.files.push_back(rel);
    } else {
        m.exts.clear();
        for (const auto& [rel, _] : files)
            if (rel.size() > m.stem.size())
                m.exts.push_back(rel.substr(m.stem.size()));   // "<stem>.pak" -> ".pak"
    }
    storeModFiles(profileId, m, files);
    mods.push_back(m);
    writeManifest(profileId, mods);
    if (isActive)
        active_ = std::move(mods);
    vfs_.commit();
    return m.id;
}

// ---- ReShade presets ----

std::string ProfileStore::presetManifestKey(const std::string& pid) const {
    return "profiles/" + pid + "/presets.json";
}

std::string ProfileStore::presetBlobKey(const std::string& pid, int presetId) const {
    return "profiles/" + pid + "/reshade/" + std::to_string(presetId) + "/preset.ini";
}

std::filesystem::path ProfileStore::managedPresetPath(const std::string& name) const {
    return paths_.binWin64 / "reshade-presets" / "managed" / pathFromUtf8(name + ".ini");
}

int ProfileStore::nextPresetId(const std::vector<ReshadePreset>& presets) const {
    int n = 0;
    for (const auto& p : presets) n = std::max(n, p.id);
    return n + 1;
}

void ProfileStore::readPresetManifest(const std::string& pid, std::vector<ReshadePreset>& out, int& activeId) const {
    out.clear();
    activeId = 0;
    std::string txt;
    if (!vfs_.readText(presetManifestKey(pid), txt))
        return;
    nlohmann::json j = nlohmann::json::parse(txt, nullptr, false);
    if (!j.is_object())
        return;
    activeId = j.value("activeId", 0);
    if (j.contains("presets") && j["presets"].is_array())
        for (const auto& e : j["presets"]) {
            ReshadePreset p;
            p.id = e.value("id", 0);
            p.name = e.value("name", "");
            if (p.id > 0 && !p.name.empty())
                out.push_back(std::move(p));
        }
}

void ProfileStore::writePresetManifest(const std::string& pid, const std::vector<ReshadePreset>& presets, int activeId) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : presets)
        arr.push_back({ {"id", p.id}, {"name", p.name} });
    nlohmann::json j = { {"activeId", activeId}, {"presets", arr} };
    vfs_.writeText(presetManifestKey(pid), j.dump(2));
}

void ProfileStore::capturePresetEdits(const std::string& pid, const std::vector<ReshadePreset>& presets) {
    for (const auto& p : presets) {
        Bytes disk;
        if (readFileBytes(managedPresetPath(p.name), disk))
            vfs_.write(presetBlobKey(pid, p.id), disk);
    }
}

void ProfileStore::materializePresets(const std::string& pid, const std::vector<ReshadePreset>& presets, int activeId) {
    std::error_code ec;
    const std::filesystem::path dir = paths_.binWin64 / "reshade-presets" / "managed";
    std::filesystem::remove_all(dir, ec);
    if (presets.empty())
        return;
    std::filesystem::create_directories(dir, ec);
    for (const auto& p : presets) {
        Bytes bytes;
        if (vfs_.read(presetBlobKey(pid, p.id), bytes))
            writeFileBytes(managedPresetPath(p.name), bytes);
    }
    writeReshadePresetPath(activeId);
}

void ProfileStore::writeReshadePresetPath(int activeId) {
    if (activeId == 0)
        return;
    for (const auto& p : activePresets_)
        if (p.id == activeId) {
            ReshadeIni::setPresetPath(paths_.binWin64 / "ReShade.ini", managedPresetPath(p.name));
            return;
        }
}

int ProfileStore::installPreset(const std::filesystem::path& iniFile) {
    if (!loaded_)
        return 0;
    joinCommit();
    Bytes bytes;
    if (!readFileBytes(iniFile, bytes) || bytes.empty())
        return 0;

    std::string name = narrow(iniFile.stem().wstring());
    if (!isSafeName(name))
        name = "Preset";
    auto taken = [&](const std::string& nm) {
        for (const auto& p : activePresets_) if (p.name == nm) return true;
        return false;
    };
    if (taken(name)) {
        std::string base = name;
        int n = 2;
        do { name = base + " " + std::to_string(n++); } while (taken(name));
    }

    ReshadePreset p;
    p.id = nextPresetId(activePresets_);
    p.name = name;
    vfs_.write(presetBlobKey(activeId_, p.id), bytes);
    activePresets_.push_back(p);
    if (activePresetId_ == 0)
        activePresetId_ = p.id;

    std::error_code ec;
    std::filesystem::create_directories(managedPresetPath(p.name).parent_path(), ec);
    writeFileBytes(managedPresetPath(p.name), bytes);
    writeReshadePresetPath(activePresetId_);

    writePresetManifest(activeId_, activePresets_, activePresetId_);
    commitAsync();
    return p.id;
}

bool ProfileStore::setActivePreset(int presetId) {
    joinCommit();
    if (presetId != 0 &&
        std::find_if(activePresets_.begin(), activePresets_.end(),
                     [&](const ReshadePreset& p) { return p.id == presetId; }) == activePresets_.end())
        return false;
    activePresetId_ = presetId;
    writeReshadePresetPath(activePresetId_);
    writePresetManifest(activeId_, activePresets_, activePresetId_);
    commitAsync();
    return true;
}

bool ProfileStore::renamePreset(int presetId, const std::string& newName) {
    joinCommit();
    if (!isSafeName(newName))
        return false;
    auto it = std::find_if(activePresets_.begin(), activePresets_.end(),
                           [&](const ReshadePreset& p) { return p.id == presetId; });
    if (it == activePresets_.end())
        return false;
    for (const auto& p : activePresets_)
        if (p.id != presetId && p.name == newName)
            return false;

    std::error_code ec;
    std::filesystem::rename(managedPresetPath(it->name), managedPresetPath(newName), ec);
    it->name = newName;
    if (activePresetId_ == presetId)
        writeReshadePresetPath(activePresetId_);
    writePresetManifest(activeId_, activePresets_, activePresetId_);
    commitAsync();
    return true;
}

bool ProfileStore::uninstallPreset(int presetId) {
    joinCommit();
    auto it = std::find_if(activePresets_.begin(), activePresets_.end(),
                           [&](const ReshadePreset& p) { return p.id == presetId; });
    if (it == activePresets_.end())
        return false;

    std::error_code ec;
    std::filesystem::remove(managedPresetPath(it->name), ec);
    vfs_.removePrefix("profiles/" + activeId_ + "/reshade/" + std::to_string(presetId) + "/");
    activePresets_.erase(it);
    if (activePresetId_ == presetId)
        activePresetId_ = 0;
    writePresetManifest(activeId_, activePresets_, activePresetId_);
    commitAsync();
    return true;
}

void ProfileStore::reapplyReshadePresets() {
    joinCommit();
    materializePresets(activeId_, activePresets_, activePresetId_);
}

}
