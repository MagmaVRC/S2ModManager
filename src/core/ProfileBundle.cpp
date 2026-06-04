#include "ProfileBundle.h"
#include "Paths.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>

namespace core {
namespace {

const char* kindStr(ModKind k) { return k == ModKind::Pak ? "pak" : "ue4ss"; }


// On-disk .s2profile container: magic + version, then the same manifest + per-file lzma
// payload the P2P transfer streams, minus the network handshake.
constexpr std::array<char, 4> kFileMagic = { 'S', '2', 'P', 'F' };
constexpr std::uint8_t        kFileVersion = 1;

void putU32(std::ostream& os, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) os.put(static_cast<char>((v >> (i * 8)) & 0xFF));
}
void putU64(std::ostream& os, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) os.put(static_cast<char>((v >> (i * 8)) & 0xFF));
}

bool readN(std::istream& is, void* dst, std::size_t n) {
    is.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(is.gcount()) == n;
}
bool getU32(std::istream& is, std::uint32_t& v) {
    std::uint8_t b[4];
    if (!readN(is, b, 4)) return false;
    v = (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) | b[3];
    return true;
}
bool getU64(std::istream& is, std::uint64_t& v) {
    std::uint8_t b[8];
    if (!readN(is, b, 8)) return false;
    v = 0;
    for (std::uint8_t c : b) v = (v << 8) | c;
    return true;
}

// Frames over the wire cap raw file size implicitly; cap it here too so a corrupt file can't
// drive a huge allocation. 4 GiB is far above any real mod file.
constexpr std::uint64_t kMaxFileBytes = 4ull * 1024 * 1024 * 1024;

bool safeRel(const std::string& rel) {
    if (rel.empty() || rel.find("..") != std::string::npos)
        return false;
    if (rel.front() == '/' || rel.front() == '\\')
        return false;
    return rel.find(':') == std::string::npos;   // reject drive letters and NTFS data streams
}

}  // namespace

std::uint64_t BundleManifest::totalBytes() const {
    std::uint64_t total = 0;
    for (const auto& m : mods)
        for (const auto& f : m.files)
            total += f.rawSize;
    return total;
}

std::string manifestToJson(const BundleManifest& m) {
    nlohmann::json mods = nlohmann::json::array();
    for (const auto& mod : m.mods) {
        nlohmann::json files = nlohmann::json::array();
        for (const auto& f : mod.files)
            files.push_back({ {"path", f.relPath}, {"size", f.rawSize} });
        mods.push_back({ {"kind", kindStr(mod.kind)},
                         {"name", mod.name},
                         {"enabled", mod.enabled},
                         {"stem", mod.stem},
                         {"subdir", mod.subdir},
                         {"files", files} });
    }
    nlohmann::json j = { {"profileName", m.profileName}, {"mods", mods} };
    return j.dump();
}

std::optional<BundleManifest> manifestFromJson(std::string_view json) {
    nlohmann::json j = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!j.is_object() || !j.contains("mods") || !j["mods"].is_array())
        return std::nullopt;

    BundleManifest m;
    m.profileName = j.value("profileName", std::string("Shared profile"));
    for (const auto& jm : j["mods"]) {
        BundleMod mod;
        mod.kind    = jm.value("kind", std::string("pak")) == "ue4ss" ? ModKind::Ue4ss : ModKind::Pak;
        mod.name    = jm.value("name", "");
        mod.enabled = jm.value("enabled", true);
        mod.stem    = jm.value("stem", "");
        mod.subdir  = jm.value("subdir", std::string(kLogicMods));
        if (jm.contains("files") && jm["files"].is_array()) {
            for (const auto& jf : jm["files"]) {
                BundleFileMeta f;
                f.relPath = jf.value("path", "");
                f.rawSize = jf.value("size", std::uint64_t{0});
                if (!f.relPath.empty())
                    mod.files.push_back(std::move(f));
            }
        }
        if (!mod.name.empty() && !mod.files.empty())
            m.mods.push_back(std::move(mod));
    }
    return m;
}

bool gatherManifest(const ProfileStore& store, const std::string& profileName, BundleManifest& out) {
    out = BundleManifest{};
    out.profileName = profileName;
    for (const auto& m : store.mods()) {
        BundleMod bm;
        bm.kind    = m.kind;
        bm.name    = m.name;
        bm.enabled = m.enabled;
        bm.stem    = m.stem;
        bm.subdir  = m.subdir;
        if (m.kind == ModKind::Ue4ss)
            for (const auto& rel : m.files)
                bm.files.push_back({ rel, 0 });
        else
            for (const auto& ext : m.exts)
                bm.files.push_back({ m.stem + ext, 0 });
        out.mods.push_back(std::move(bm));   // 1:1 with store.mods() so modIdx aligns
    }
    return true;
}

bool readModBytes(const ProfileStore& store, const BundleManifest& m,
                  std::size_t modIdx, std::vector<Bytes>& filesInOrder) {
    filesInOrder.clear();
    if (modIdx >= store.mods().size() || modIdx >= m.mods.size())
        return false;
    std::vector<std::pair<std::string, Bytes>> files;
    if (!store.readModFiles(store.mods()[modIdx].id, files))
        return false;
    for (auto& [rel, bytes] : files)
        filesInOrder.push_back(std::move(bytes));
    return filesInOrder.size() == m.mods[modIdx].files.size();
}

static bool stemIsSafe(const std::string& stem) {
    return !stem.empty() &&
           stem.find('/') == std::string::npos &&
           stem.find('\\') == std::string::npos &&
           stem.find(':') == std::string::npos;
}

bool writeMod(ProfileStore& store, const std::string& profileId,
              const BundleMod& mod, const std::vector<Bytes>& filesInOrder) {
    if (filesInOrder.size() != mod.files.size() || mod.files.empty() || !isSafeName(mod.name))
        return false;
    if (mod.kind == ModKind::Pak && !stemIsSafe(mod.stem))
        return false;

    ProfileMod spec;
    spec.kind    = mod.kind;
    spec.name    = mod.name;
    spec.enabled = mod.enabled;
    spec.stem    = mod.stem;
    spec.subdir  = mod.subdir == kContentMods ? kContentMods : kLogicMods;

    std::vector<std::pair<std::string, Bytes>> files;
    for (std::size_t i = 0; i < mod.files.size(); ++i) {
        if (!safeRel(mod.files[i].relPath))
            return false;
        files.emplace_back(mod.files[i].relPath, filesInOrder[i]);
    }
    return store.importModInto(profileId, spec, files) != 0;
}

bool exportProfile(const ProfileStore& store, const std::string& profileName,
                   const std::filesystem::path& dest, const std::function<void(float)>& onProgress) {
    BundleManifest manifest;
    if (!gatherManifest(store, profileName, manifest) || manifest.mods.empty())
        return false;

    std::ofstream os(dest, std::ios::binary | std::ios::trunc);
    if (!os)
        return false;

    os.write(kFileMagic.data(), kFileMagic.size());
    os.put(static_cast<char>(kFileVersion));

    const std::string json = manifestToJson(manifest);
    putU32(os, static_cast<std::uint32_t>(json.size()));
    os.write(json.data(), static_cast<std::streamsize>(json.size()));

    std::uint64_t totalFiles = 0;
    for (const auto& m : manifest.mods) totalFiles += m.files.size();
    putU32(os, static_cast<std::uint32_t>(totalFiles));
    if (totalFiles == 0) totalFiles = 1;

    std::uint64_t done = 0;
    for (std::size_t mi = 0; mi < manifest.mods.size(); ++mi) {
        std::vector<Bytes> files;
        if (!readModBytes(store, manifest, mi, files))
            return false;
        for (const Bytes& raw : files) {
            Bytes comp;
            if (!raw.empty()) {
                comp = lzmaCompress(raw, 3);   // smaller .s2profile export; ratio over speed
                if (comp.empty()) return false;
            }
            putU64(os, raw.size());
            putU64(os, comp.size());
            if (!comp.empty())
                os.write(reinterpret_cast<const char*>(comp.data()), static_cast<std::streamsize>(comp.size()));
            if (!os) return false;
            if (onProgress) onProgress(static_cast<float>(++done) / static_cast<float>(totalFiles));
        }
    }
    os.flush();
    return static_cast<bool>(os);
}

std::optional<std::string> importProfile(ProfileStore& store, const std::string& profileId,
                   const std::filesystem::path& src, const std::function<void(float)>& onProgress) {
    std::ifstream is(src, std::ios::binary);
    if (!is)
        return std::nullopt;

    std::array<char, 4> magic{};
    std::uint8_t version = 0;
    if (!readN(is, magic.data(), magic.size()) || magic != kFileMagic)
        return std::nullopt;
    if (!readN(is, &version, 1) || version != kFileVersion)
        return std::nullopt;

    std::uint32_t manifestLen = 0;
    if (!getU32(is, manifestLen) || manifestLen == 0 || manifestLen > (16u * 1024 * 1024))
        return std::nullopt;
    std::string json(manifestLen, '\0');
    if (!readN(is, json.data(), manifestLen))
        return std::nullopt;
    auto manifest = manifestFromJson(json);
    if (!manifest || manifest->mods.empty())
        return std::nullopt;

    std::uint32_t fileCount = 0;
    if (!getU32(is, fileCount))
        return std::nullopt;

    std::uint64_t totalFiles = 0;
    for (const auto& m : manifest->mods) totalFiles += m.files.size();
    if (fileCount != totalFiles)
        return std::nullopt;
    std::uint64_t denom = totalFiles == 0 ? 1 : totalFiles;

    std::uint64_t done = 0;
    for (const BundleMod& mod : manifest->mods) {
        std::vector<Bytes> files;
        for (std::size_t i = 0; i < mod.files.size(); ++i) {
            std::uint64_t rawSize = 0, compSize = 0;
            if (!getU64(is, rawSize) || !getU64(is, compSize) ||
                rawSize > kMaxFileBytes || compSize > kMaxFileBytes)
                return std::nullopt;
            Bytes raw;
            if (rawSize > 0) {
                Bytes comp(static_cast<std::size_t>(compSize));
                if (compSize > 0 && !readN(is, comp.data(), comp.size()))
                    return std::nullopt;
                if (!lzmaDecompress(comp, static_cast<std::size_t>(rawSize), raw))
                    return std::nullopt;
            }
            files.push_back(std::move(raw));
            if (onProgress) onProgress(static_cast<float>(++done) / static_cast<float>(denom));
        }
        if (!writeMod(store, profileId, mod, files))
            return std::nullopt;
    }
    return manifest->profileName;
}

}  // namespace core
