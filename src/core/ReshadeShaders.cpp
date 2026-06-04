#include "ReshadeShaders.h"
#include "Archive.h"
#include "Http.h"
#include "Paths.h"
#include "ReshadeIni.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <fstream>

namespace core {

namespace {

// Shallowest directory under 'tree' that directly contains a .fx file.
std::optional<std::filesystem::path> findShaderRoot(const std::filesystem::path& tree) {
    std::error_code ec;
    std::optional<std::filesystem::path> best;
    std::size_t bestDepth = (std::numeric_limits<std::size_t>::max)();
    for (auto it = std::filesystem::recursive_directory_iterator(tree, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec) || lowerExt(it->path()) != ".fx")
            continue;
        const std::filesystem::path dir = it->path().parent_path();
        const std::size_t depth = static_cast<std::size_t>(std::distance(dir.begin(), dir.end()));
        if (depth < bestDepth) { bestDepth = depth; best = dir; }
    }
    return best;
}

void copyTree(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    std::filesystem::create_directories(to, ec);
    std::filesystem::copy(from, to,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
}

int countFx(const std::filesystem::path& dir) {
    std::error_code ec;
    int n = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it)
        if (it->is_regular_file(ec) && lowerExt(it->path()) == ".fx")
            ++n;
    return n;
}

bool isSafePackName(const std::string& n) {
    return !n.empty() && isSafeName(n);
}

}  // namespace

ReshadeShaders::ReshadeShaders(GamePaths paths) : paths_(std::move(paths)) {}

std::filesystem::path ReshadeShaders::shadersDir() const { return paths_.binWin64 / "reshade-shaders" / "Shaders"; }
std::filesystem::path ReshadeShaders::texturesDir() const { return paths_.binWin64 / "reshade-shaders" / "Textures"; }
std::filesystem::path ReshadeShaders::disabledDir() const { return paths_.binWin64 / "reshade-shaders" / ".disabled"; }
std::filesystem::path ReshadeShaders::manifestFile() const { return appConfigDir() / L"reshade-packs.json"; }

std::string ReshadeShaders::rootKey() const {
    return narrow(paths_.root.lexically_normal().wstring());
}

bool ReshadeShaders::load() {
    readManifest();
    std::error_code ec;
    packs_.erase(std::remove_if(packs_.begin(), packs_.end(), [&](const ShaderPack& p) {
        const std::filesystem::path live = p.enabled ? shadersDir() / pathFromUtf8(p.name)
                                                     : disabledDir() / "Shaders" / pathFromUtf8(p.name);
        return !std::filesystem::exists(live, ec);
    }), packs_.end());
    loaded_ = true;
    writeManifest();
    return true;
}

bool ReshadeShaders::readManifest() {
    packs_.clear();
    std::ifstream in(manifestFile(), std::ios::binary);
    if (!in)
        return false;
    std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nlohmann::json j = nlohmann::json::parse(txt, nullptr, false);
    if (!j.is_object() || !j.contains("roots"))
        return false;
    const std::string key = rootKey();
    if (!j["roots"].contains(key) || !j["roots"][key].is_array())
        return false;
    for (const auto& e : j["roots"][key]) {
        ShaderPack p;
        p.name = e.value("name", "");
        p.source = e.value("source", "");
        p.enabled = e.value("enabled", true);
        p.effectCount = e.value("effectCount", 0);
        p.builtin = e.value("builtin", false);
        if (!p.name.empty())
            packs_.push_back(std::move(p));
    }
    return true;
}

bool ReshadeShaders::writeManifest() const {
    nlohmann::json j;
    {
        std::ifstream in(manifestFile(), std::ios::binary);
        if (in) {
            std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            j = nlohmann::json::parse(txt, nullptr, false);
        }
    }
    if (!j.is_object())
        j = nlohmann::json::object();
    if (!j.contains("roots") || !j["roots"].is_object())
        j["roots"] = nlohmann::json::object();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : packs_)
        arr.push_back({ {"name", p.name}, {"source", p.source}, {"enabled", p.enabled},
                        {"effectCount", p.effectCount}, {"builtin", p.builtin} });
    j["roots"][rootKey()] = arr;

    std::ofstream out(manifestFile(), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << j.dump(2);
    return out.good();
}

std::string ReshadeShaders::uniqueName(const std::string& desired) const {
    std::string base = desired;
    if (!isSafePackName(base))
        base = "Pack";
    std::string name = base;
    int n = 2;
    auto taken = [&](const std::string& cand) {
        for (const auto& p : packs_)
            if (p.name == cand)
                return true;
        return false;
    };
    while (taken(name))
        name = base + " " + std::to_string(n++);
    return name;
}

bool ReshadeShaders::ensureRecursiveSearchPaths() {
    std::error_code ec;
    std::filesystem::create_directories(shadersDir(), ec);
    std::filesystem::create_directories(texturesDir(), ec);
    return ReshadeIni::ensureSearchPaths(paths_.binWin64 / "ReShade.ini");
}

ShaderResult ReshadeShaders::installTree(const std::filesystem::path& shaderRoot, const std::string& name,
                                         const std::string& source) {
    std::error_code ec;
    const std::filesystem::path dstShaders = shadersDir() / pathFromUtf8(name);
    copyTree(shaderRoot, dstShaders, ec);
    if (ec)
        return { false, "Couldn't copy shader files into the game folder.", "" };

    // A sibling "Textures" folder ships the pack's texture assets.
    for (auto it = std::filesystem::directory_iterator(shaderRoot.parent_path(), ec);
         !ec && it != std::filesystem::directory_iterator(); ++it) {
        if (it->is_directory(ec)) {
            std::string fn = narrow(it->path().filename().wstring());
            std::transform(fn.begin(), fn.end(), fn.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (fn == "textures") {
                std::error_code tec;
                copyTree(it->path(), texturesDir() / pathFromUtf8(name), tec);
            }
        }
    }

    ShaderPack pack;
    pack.name = name;
    pack.source = source;
    pack.enabled = true;
    pack.effectCount = countFx(dstShaders);
    pack.builtin = (source == "crosire:slim" || source == "crosire:latest");

    packs_.erase(std::remove_if(packs_.begin(), packs_.end(),
                                [&](const ShaderPack& p) { return p.name == name; }),
                 packs_.end());
    packs_.push_back(pack);
    writeManifest();
    return { true, std::format("Installed {} ({} effects).", name, pack.effectCount), name };
}

ShaderResult ReshadeShaders::installStandard(StandardBranch branch,
                                             const std::function<void(ShaderPhase, float)>& onProgress) {
    auto report = [&](ShaderPhase p, float f) { if (onProgress) onProgress(p, f); };
    ensureRecursiveSearchPaths();

    const char* branchName = branch == StandardBranch::Latest ? "latest" : "slim";
    const std::string url = std::string("https://github.com/crosire/reshade-shaders/archive/refs/heads/")
                          + branchName + ".zip";

    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) /
        (std::wstring(L"S2MM_reshade_shaders_") + (branch == StandardBranch::Latest ? L"latest.zip" : L"slim.zip"));

    report(ShaderPhase::Downloading, 0.0f);
    if (!downloadFile(url, tmp, [&](float f) { report(ShaderPhase::Downloading, f); }))
        return { false, "Couldn't download the standard shader pack.", "" };

    report(ShaderPhase::Extracting, 0.0f);
    const std::filesystem::path stage = std::filesystem::temp_directory_path(ec) / L"S2MM_reshade_shaders_extract";
    std::filesystem::remove_all(stage, ec);
    const bool extracted = extract(tmp, stage, nullptr);
    std::filesystem::remove(tmp, ec);
    if (!extracted) {
        std::filesystem::remove_all(stage, ec);
        return { false, "Couldn't extract the shader pack.", "" };
    }

    report(ShaderPhase::Installing, 0.0f);
    auto root = findShaderRoot(stage);
    if (!root) {
        std::filesystem::remove_all(stage, ec);
        return { false, "No shaders (.fx) were found in the download.", "" };
    }
    ShaderResult r = installTree(*root, uniqueName("Standard"), std::string("crosire:") + branchName);
    std::filesystem::remove_all(stage, ec);
    return r;
}

ShaderResult ReshadeShaders::importPack(const std::filesystem::path& source, const std::string& displayName,
                                        const std::function<void(ShaderPhase, float)>& onProgress) {
    auto report = [&](ShaderPhase p, float f) { if (onProgress) onProgress(p, f); };
    ensureRecursiveSearchPaths();

    std::error_code ec;
    std::filesystem::path tree = source;
    std::filesystem::path stage;
    if (std::filesystem::is_regular_file(source, ec)) {
        report(ShaderPhase::Extracting, 0.0f);
        stage = std::filesystem::temp_directory_path(ec) / L"S2MM_reshade_import_extract";
        std::filesystem::remove_all(stage, ec);
        if (!extract(source, stage, nullptr)) {
            std::filesystem::remove_all(stage, ec);
            return { false, "Couldn't extract the shader archive.", "" };
        }
        tree = stage;
    }

    report(ShaderPhase::Installing, 0.0f);
    auto root = findShaderRoot(tree);
    if (!root) {
        if (!stage.empty()) std::filesystem::remove_all(stage, ec);
        return { false, "No shaders (.fx) were found to import.", "" };
    }
    ShaderResult r = installTree(*root, uniqueName(displayName), "import:" + displayName);
    if (!stage.empty()) std::filesystem::remove_all(stage, ec);
    return r;
}

bool ReshadeShaders::setEnabled(const std::string& name, bool enabled) {
    auto it = std::find_if(packs_.begin(), packs_.end(), [&](const ShaderPack& p) { return p.name == name; });
    if (it == packs_.end() || it->enabled == enabled)
        return false;

    std::error_code ec;
    const std::filesystem::path liveS = shadersDir() / pathFromUtf8(name);
    const std::filesystem::path liveT = texturesDir() / pathFromUtf8(name);
    const std::filesystem::path offS = disabledDir() / "Shaders" / pathFromUtf8(name);
    const std::filesystem::path offT = disabledDir() / "Textures" / pathFromUtf8(name);

    auto move = [&](const std::filesystem::path& from, const std::filesystem::path& to) {
        if (!std::filesystem::exists(from, ec))
            return;
        std::filesystem::create_directories(to.parent_path(), ec);
        std::filesystem::rename(from, to, ec);
    };

    if (enabled) {
        move(offS, liveS);
        move(offT, liveT);
    } else {
        move(liveS, offS);
        move(liveT, offT);
    }
    it->enabled = enabled;
    writeManifest();
    return true;
}

bool ReshadeShaders::uninstall(const std::string& name) {
    auto it = std::find_if(packs_.begin(), packs_.end(), [&](const ShaderPack& p) { return p.name == name; });
    if (it == packs_.end())
        return false;
    std::error_code ec;
    std::filesystem::remove_all(shadersDir() / pathFromUtf8(name), ec);
    std::filesystem::remove_all(texturesDir() / pathFromUtf8(name), ec);
    std::filesystem::remove_all(disabledDir() / "Shaders" / pathFromUtf8(name), ec);
    std::filesystem::remove_all(disabledDir() / "Textures" / pathFromUtf8(name), ec);
    packs_.erase(it);
    writeManifest();
    return true;
}

}
