#include "App.h"
#include "Theme.h"
#include "../core/AppVersion.h"
#include "../core/Ue4ssInstall.h"
#include "../core/ReshadeInstall.h"
#include "../core/Archive.h"
#include "../core/ModConflicts.h"
#include "../core/Paths.h"
#include "../core/ModName.h"
#include "../core/Steam.h"
#include "../platform/Dialogs.h"
#include "../platform/Firewall.h"
#include "../platform/ImageDecode.h"
#include "../platform/ResourceExtract.h"
#include "../../resources/resource.h"
#include "../ui/Anim.h"
#include "../ui/Widgets.h"
#include "../ui/Icons.h"
#include "../ui/Colors.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>

namespace app {

using namespace ui;

namespace {
constexpr float kTopBarH  = 40.0f;
constexpr float kDetailsW = 300.0f;

ImU32 typeColor(core::ModKind t) { return t == core::ModKind::Pak ? colAccent : IM_COL32(178, 138, 255, 255); }
const char* typeLabel(core::ModKind t) { return t == core::ModKind::Pak ? "PAK" : "UE4SS"; }

ModEntry makeEntry(const core::ProfileMod& m) {
    core::ParsedModName p = core::parseModName(m.name);
    ModEntry e;
    e.name = m.name;
    e.displayName = p.name.empty() ? m.name : p.name;
    e.version = p.version;
    e.nexusId = p.nexusId;
    e.type = m.kind;
    e.enabled = m.enabled;
    e.modId = m.id;
    e.rowKey = std::to_string(m.id);
    return e;
}

bool colorRow(const char* label, std::uint32_t* packed) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(*packed);
    bool changed = ImGui::ColorEdit4(label, &v.x,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
    if (changed) *packed = ImGui::ColorConvertFloat4ToU32(v);
    return changed;
}

bool matchesSearch(const std::string& name, const char* query) {
    if (!query || !query[0]) return true;
    const std::size_t qlen = std::strlen(query);
    if (qlen > name.size()) return false;
    for (std::size_t i = 0; i <= name.size() - qlen; ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < qlen; ++j)
            if (std::tolower(static_cast<unsigned char>(name[i + j])) !=
                std::tolower(static_cast<unsigned char>(query[j]))) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}

float rowReveal(const char* name, bool present, float speed) {
    ImGuiID id = ImGui::GetID(name) ^ 0x51ED2700u;
    return ui::animTo(id, present ? 1.0f : 0.0f, speed);
}

void seedRowReveal(const char* name, float v) {
    ui::animSet(ImGui::GetID(name) ^ 0x51ED2700u, v);
}

// ---- Share modal building blocks -------------------------------------------

struct Card {
    ImDrawListSplitter split;
    ImVec2 tl{};
    float padX = 0.0f, padY = 0.0f, width = 0.0f;

    float inner() const { return width - padX * 2.0f; }

    void begin(float w, float padPxX, float padPxY) {
        width = w; padX = padPxX; padY = padPxY;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        split.Split(dl, 2);
        split.SetCurrentChannel(dl, 1);                 // content draws on top
        tl = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(tl.x + padX, tl.y + padY));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner());
    }
    void end(ImU32 bg, ImU32 border, float rounding) {
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 br(tl.x + width, ImGui::GetItemRectMax().y + padY);
        split.SetCurrentChannel(dl, 0);                 // background behind content
        dl->AddRectFilled(tl, br, bg, rounding);
        if (border) dl->AddRect(tl, br, border, rounding, 0, 1.0f);
        split.Merge(dl);
        ImGui::SetCursorScreenPos(ImVec2(tl.x, br.y));
        ImGui::Dummy(ImVec2(width, 0.0f));
    }
};

void drawHexField(ImDrawList* dl, ImVec2 origin, ImVec2 size, ImU32 col, float r) {
    if (r < 6.0f) return;
    const float w = 1.7320508f * r;      // sqrt(3) * r : flat-to-flat width
    const float vstep = 1.5f * r;        // row pitch
    int row = 0;
    for (float cy = origin.y; cy - r <= origin.y + size.y; cy += vstep, ++row) {
        float xoff = (row & 1) ? w * 0.5f : 0.0f;
        for (float cx = origin.x - xoff; cx - w <= origin.x + size.x; cx += w) {
            ImVec2 pts[6];
            for (int k = 0; k < 6; ++k) {
                float a = 1.0471975512f * static_cast<float>(k) - 1.5707963268f;   // 60*k - 90 deg, pointy top
                pts[k] = ImVec2(px(cx + r * std::cos(a)), px(cy + r * std::sin(a)));
            }
            dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, 1.0f);
        }
    }
}

void glassPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float s) {
    ImVec4 av = toVec(colAccent);
    ImVec4 t = av; t.w = 0.09f;  ImU32 sheenTop = ImGui::ColorConvertFloat4ToU32(t);
    t.w = 0.0f;                  ImU32 sheenBot = ImGui::ColorConvertFloat4ToU32(t);
    float sh = (b.y - a.y) * 0.45f;
    if (sh > 70.0f * s) sh = 70.0f * s;
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + sh), sheenTop, sheenTop, sheenBot, sheenBot);
    ImVec4 r = av; r.w = 0.16f;
    dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f),
                ImGui::ColorConvertFloat4ToU32(r), 0.0f, 0, 1.0f);
}

void drawShareGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    ImVec2 a(c.x + r, c.y - r);             // top-right node
    ImVec2 b(c.x - r, c.y - r * 0.45f);     // upper-left node
    ImVec2 d(c.x - r, c.y + r * 0.95f);     // lower-left node
    dl->AddLine(a, b, col, 1.6f);
    dl->AddLine(a, d, col, 1.6f);
    float nr = r * 0.46f;
    dl->AddCircleFilled(a, nr, col);
    dl->AddCircleFilled(b, nr, col);
    dl->AddCircleFilled(d, nr, col);
}

void segmentedControl(const char* id, int* current, const char* const* labels,
                      const bool* enabled, int count, float width, float s,
                      const ImTextureID* segIcons = nullptr) {
    float h = 30.0f * s;
    float r = 7.0f * s;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float segW = width / static_cast<float>(count);

    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), colSurface, r);

    ImGuiID baseId = ImGui::GetID(id);
    float hl = springTo(baseId, static_cast<float>(*current), 26.0f, 0.80f);
    ImVec2 ha(p.x + hl * segW + 2.0f * s, p.y + 2.0f * s);
    ImVec2 hb(ha.x + segW - 4.0f * s, p.y + h - 2.0f * s);
    dl->AddRectFilled(ha, hb, colAccent, r - 1.0f * s);

    for (int i = 0; i < count; ++i) {
        ImVec2 sa(p.x + static_cast<float>(i) * segW, p.y);
        ImGui::SetCursorScreenPos(sa);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(segW, h));
        bool en = !enabled || enabled[i];
        bool hovered = en && ImGui::IsItemHovered();
        if (en && ImGui::IsItemClicked()) *current = i;
        ImGui::PopID();

        float over = 1.0f - std::min(std::fabs(hl - static_cast<float>(i)), 1.0f);
        ImU32 col = en ? lerpColor(colTextDim, colTextHi, over) : colSurface3;
        if (hovered && over < 0.5f) col = colText;
        ImVec2 tsz = ImGui::CalcTextSize(labels[i]);
        ImTextureID ico = segIcons ? segIcons[i] : ImTextureID(0);
        if (ico) {
            float isz = std::floor(tsz.y + 2.0f * s);
            float gap = 7.0f * s;
            float gw = isz + gap + tsz.x;
            float gx = sa.x + (segW - gw) * 0.5f;
            ui::drawIconTex(dl, ico, ImVec2(px(gx), px(p.y + (h - isz) * 0.5f)),
                            ImVec2(px(gx) + isz, px(p.y + (h - isz) * 0.5f) + isz), col);
            textSnapped(dl, ImVec2(gx + isz + gap, p.y + (h - tsz.y) * 0.5f), col, labels[i]);
        } else {
            textSnapped(dl, ImVec2(sa.x + (segW - tsz.x) * 0.5f, p.y + (h - tsz.y) * 0.5f), col, labels[i]);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, 0.0f));
}

void statusDot(ImU32 dotCol, ImU32 textCol, const char* text, float s, float pulse) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
    float rad = 4.0f * s;
    if (pulse > 0.0f)
        dl->AddCircleFilled(ImVec2(p.x + rad, cy), rad + pulse * 3.5f * s, lerpColor(dotCol, colBg, 0.55f));
    dl->AddCircleFilled(ImVec2(p.x + rad, cy), rad, dotCol);
    ImGui::SetCursorScreenPos(ImVec2(p.x + rad * 2.0f + 9.0f * s, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec(textCol));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void statusIcon(ui::Icon ic, ImU32 iconCol, ImU32 textCol, const char* text, float s) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float isz = 16.0f * s;
    float cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
    if (ui::icons().tex(ic))
        ui::icons().draw(dl, ic, ImVec2(p.x, cy - isz * 0.5f), isz, iconCol);
    else
        dl->AddCircleFilled(ImVec2(p.x + 4.0f * s, cy), 4.0f * s, iconCol);
    ImGui::SetCursorScreenPos(ImVec2(p.x + isz + 9.0f * s, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec(textCol));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
}

App::App() : config_(core::Config::load()) {
    if (config_.activeProfileName.empty())
        config_.activeProfileName = "Vanilla";

    const std::size_t n = std::min(config_.gamePath.size(), gamePathBuffer_.size() - 1);
    std::copy_n(config_.gamePath.data(), n, gamePathBuffer_.data());
    uiScaleSetting_ = config_.uiScale;
    uiScaleCurrent_ = config_.uiScale;

    resolveGame();
    checkForUpdates(false);
}

App::~App() {
    if (store_) {
        for (const auto& [modId, desired] : togglePending_)
            store_->setEnabled(modId, desired);
        if (!togglePending_.empty())
            store_->flush();
        togglePending_.clear();
    }
    if (ue4ssThread_.joinable())
        ue4ssThread_.join();
    if (reshadeInstallThread_.joinable())
        reshadeInstallThread_.join();
    if (reshadeUninstallThread_.joinable())
        reshadeUninstallThread_.join();
    if (reshadeShaderThread_.joinable())
        reshadeShaderThread_.join();
    if (updateThread_.joinable())
        updateThread_.join();
    if (installThread_.joinable())
        installThread_.join();
}

void App::onScaleChanged(float dpiScale) {
    dpiScale_ = dpiScale;
    ui::setUiScale(dpiScale);
    uiScaleCurrent_ = config_.uiScale;
    ui::setUserScale(uiScaleCurrent_);
    loadFonts(dpiScale * config_.uiScale);
    applyTheme(config_.activePalette(), dpiScale);
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontScaleDpi  = dpiScale;
    st.FontScaleMain = uiScaleCurrent_;
}

void App::attachWindow(platform::Win32Window* win) {
    window_ = win;
    if (window_) {
        window_->setVSync(config_.vsync);
        window_->onFilesDropped([this](std::vector<std::filesystem::path> paths) {
            installMods(paths);
        });
        window_->onDragState([this](bool over) { dragOver_ = over; });
        window_->onBetweenFrames([this] {
            if (fontReloadPending_) {
                fontReloadPending_ = false;
                loadFonts(dpiScale_ * uiScaleCurrent_);
                window_->invalidateFontTexture();
            }
        });
        background_.setDevice(window_->device(), window_->context());
        ui::icons().init(window_->device(), window_->context());
        reloadBackground();
    }
}

void App::onShutdown() {
    ui::icons().release();
    background_.releaseDevice();
}

void App::applyActivePalette() {
    ui::applyPalette(config_.activePalette());
}

namespace {
std::filesystem::path defaultBgPath(bool dark) {
    return core::appConfigDir() / L"backgrounds" /
           (dark ? L"default-dark.webp" : L"default-light.webp");
}
}

void App::reloadBackground() {
    if (!backgroundDefaultsExtracted_) {
        platform::extractEmbeddedResource(IDR_BG_DARK, defaultBgPath(true));
        platform::extractEmbeddedResource(IDR_BG_LIGHT, defaultBgPath(false));
        backgroundDefaultsExtracted_ = true;
    }

    if (!config_.background.enabled) {
        background_.clearImage();
        return;
    }

    const bool dark = config_.themeMode == "dark";
    std::string custom = config_.activeBackgroundImage();
    std::filesystem::path path = custom.empty() ? defaultBgPath(dark)
                                                : core::pathFromUtf8(custom);

    auto img = platform::decodeImageFile(path);
    if (!img && !custom.empty())                       // custom image unreadable: fall back to default
        img = platform::decodeImageFile(defaultBgPath(dark));
    if (img && background_.setImage(*img)) {
        background_.setBlur(config_.background.blur);
        background_.setDim(config_.background.dim);
        background_.setDrift(config_.background.driftAmount, config_.background.driftSpeed);
    } else {
        background_.clearImage();
    }
}

bool App::backgroundActive() const {
    return config_.background.enabled && background_.hasImage();
}

ImU32 App::panelBg(ImU32 opaque) const {
    if (!backgroundActive())
        return opaque;
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(opaque);
    v.w *= config_.background.panelOpacity;
    return ImGui::ColorConvertFloat4ToU32(v);
}

void App::saveConfig() {
    config_.gamePath = gamePathBuffer_.data();
    config_.uiScale = uiScaleSetting_;
    if (store_)
        config_.activeProfileName = store_->activeName();
    saveDirty_ = true;
    saveTimer_ = 0.4f;
    if (window_)
        window_->setVSync(config_.vsync);
}

void App::resolveGame() {
    game_ = core::Game::resolve(config_.gamePath);
    gameReady_ = game_.valid();
    history_.clear();
    if (gameReady_) {
        if (config_.gamePath.empty()) {
            config_.gamePath = game_.rootUtf8();
            gamePathBuffer_.fill('\0');
            const std::size_t n = std::min(config_.gamePath.size(), gamePathBuffer_.size() - 1);
            std::copy_n(config_.gamePath.data(), n, gamePathBuffer_.data());
            (void)config_.save();
        }
        loadMods();
    } else {
        mods_.clear();
        selected_ = -1;
    }
}

void App::setGamePath(const std::string& path) {
    gamePathBuffer_.fill('\0');
    const std::size_t n = std::min(path.size(), gamePathBuffer_.size() - 1);
    std::copy_n(path.data(), n, gamePathBuffer_.data());
    saveConfig();
    resolveGame();
}

void App::detectGame() {
    core::Game g = core::Game::resolve("");
    if (g.valid()) {
        statusMessage_.clear();
        setGamePath(g.rootUtf8());
    } else {
        statusMessage_ = "Subnautica 2 not found via Steam — browse to it manually.";
    }
}

void App::browseGame() {
    if (auto picked = platform::pickFolder("Select your Subnautica 2 folder")) {
        setGamePath(*picked);
        statusMessage_ = gameReady_ ? "" : "That folder isn't a Subnautica 2 install (Subnautica2.exe not found).";
    }
}

void App::loadMods() {
    mods_.clear();
    selected_ = -1;
    sn2ModSettingsNeeded_ = false;
    togglePending_.clear();   // stale modIds from the previous profile/store
    if (!gameReady_)
        return;

    store_.emplace(game_.paths());
    store_->load(config_.activeProfileName);
    config_.activeProfileName = store_->activeName();
    for (const auto& m : store_->mods())
        mods_.push_back(makeEntry(m));
    sn2ModSettingsNeeded_ = anyModNeedsSn2ModSettings();
    conflictWarnings_ = modConflictWarnings();

    shaders_.emplace(game_.paths());
    shaders_->load();
}

void App::syncModsFromStore() {
    if (!store_)
        return;
    const int selId = (selected_ >= 0 && selected_ < static_cast<int>(mods_.size()))
                          ? mods_[selected_].modId : -1;
    mods_.clear();
    for (const auto& m : store_->mods())
        mods_.push_back(makeEntry(m));
    selected_ = -1;
    if (selId >= 0)
        for (int i = 0; i < static_cast<int>(mods_.size()); ++i)
            if (mods_[i].modId == selId) { selected_ = i; break; }
    sn2ModSettingsNeeded_ = anyModNeedsSn2ModSettings();
    conflictWarnings_ = modConflictWarnings();
}

void App::reorderInStore() {
    if (!store_)
        return;
    std::vector<int> ids;
    for (const auto& m : mods_)
        ids.push_back(m.modId);
    store_->applyOrder(ids);
}

void App::toast(std::string text, ImU32 color) {
    toasts_.push_back({ std::move(text), color });
    if (toasts_.size() > 5)
        toasts_.erase(toasts_.begin());
}

void App::setModEnabled(int index, bool enabled, bool record) {
    if (index < 0 || index >= static_cast<int>(mods_.size())) return;
    if (mods_[index].enabled == enabled) return;
    std::string name = mods_[index].name;
    auto apply = [this](const std::string& nm, bool en) {
        for (auto& m : mods_)
            if (m.name == nm) {
                m.enabled = en;
                togglePending_[m.modId] = en;
                break;
            }
    };
    apply(name, enabled);
    if (record) {
        bool prev = !enabled;
        history_.push({ std::format("{} {}", enabled ? "Enable" : "Disable", name),
                        [apply, name, prev]{ apply(name, prev); },
                        [apply, name, enabled]{ apply(name, enabled); } });
    }
}

void App::processToggles() {
    if (togglePending_.empty())
        return;
    ui::markAnimActive();
    if (!store_) { togglePending_.clear(); return; }
    for (const auto& [modId, desired] : togglePending_)
        store_->setEnabled(modId, desired);
    togglePending_.clear();
    store_->flush();
}

void App::moveMod(int from, int to, bool record) {
    int n = static_cast<int>(mods_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    std::string name = mods_[from].name;
    ModEntry moved = std::move(mods_[from]);
    mods_.erase(mods_.begin() + from);
    mods_.insert(mods_.begin() + to, std::move(moved));
    reorderInStore();
    if (record) {
        history_.push({ std::format("Move {}", name),
            [this, to, from]{ moveMod(to, from, false); },
            [this, from, to]{ moveMod(from, to, false); } });
    }
}

void App::uninstallByName(const std::vector<std::string>& names) {
    for (const auto& nm : names) {
        for (int i = 0; i < static_cast<int>(mods_.size()); ++i) {
            if (mods_[i].name != nm) continue;
            if (store_)
                store_->uninstall(mods_[i].modId);
            mods_.erase(mods_.begin() + i);
            break;
        }
    }
    selected_ = mods_.empty() ? -1 : std::min(selected_, static_cast<int>(mods_.size()) - 1);
}

void App::installMods(const std::vector<std::filesystem::path>& sources) {
    if (!gameReady_) { toast("Locate the game first.", colWarn); return; }
    if (installBusy_.load()) { toast("Still installing — hold on.", colWarn); return; }
    if (installThread_.joinable())
        installThread_.join();

    installBefore_.clear();
    for (const auto& m : mods_) installBefore_.push_back(m.name);

    std::error_code ec;
    installTempRoot_ = std::filesystem::temp_directory_path(ec) / "S2MM_ingest";
    const std::filesystem::path tempRoot = installTempRoot_;

    installDone_ = false;
    installBusy_ = true;
    installThread_ = std::thread([this, sources, tempRoot] {
        std::vector<StagedMod> staged;
        std::error_code ec;
        for (const auto& src : sources) {
            StagedMod sm;
            sm.name = core::narrow(src.stem().wstring());

            if (std::filesystem::is_directory(src, ec)) {
                sm.tree = src;
                sm.name = core::narrow(src.filename().wstring());
            } else if (core::isPakSibling(core::lowerExt(src))) {
                sm.scratch = tempRoot / core::pathFromUtf8(sm.name);
                std::filesystem::create_directories(sm.scratch, ec);
                const std::string stem = core::narrow(src.stem().wstring());
                for (auto sib = std::filesystem::directory_iterator(src.parent_path(), ec);
                     !ec && sib != std::filesystem::directory_iterator(); ++sib)
                    if (sib->is_regular_file(ec) && core::narrow(sib->path().stem().wstring()) == stem &&
                        core::isPakSibling(core::lowerExt(sib->path()))) {
                        std::filesystem::copy_file(sib->path(), sm.scratch / sib->path().filename(),
                            std::filesystem::copy_options::overwrite_existing, ec);
                        if (ec) sm.copyFailed = true;
                        ec.clear();
                    }
                sm.tree = sm.scratch;
            } else {
                sm.scratch = tempRoot / core::pathFromUtf8(sm.name);
                std::filesystem::remove_all(sm.scratch, ec);
                if (!core::extract(src, sm.scratch, nullptr)) {
                    std::filesystem::remove_all(sm.scratch, ec);
                    sm.extractFailed = true;
                    sm.failName = core::narrow(src.filename().wstring());
                    sm.scratch.clear();
                } else {
                    sm.tree = sm.scratch;
                }
            }
            staged.push_back(std::move(sm));
        }
        staged_ = std::move(staged);
        installDone_ = true;
    });
}

void App::pollInstall() {
    if (installBusy_.load())
        ui::markAnimActive();
    if (!installDone_.exchange(false))
        return;
    if (installThread_.joinable())
        installThread_.join();

    int installedCount = 0, failed = 0;
    std::error_code ec;
    for (auto& sm : staged_) {
        if (sm.extractFailed) {
            toast(std::format("Couldn't read '{}'.", sm.failName), colWarn);
            ++failed;
            continue;
        }
        const int added = store_ ? store_->installFrom(sm.tree, sm.name) : 0;
        if (sm.copyFailed)
            toast(std::format("Some files of '{}' couldn't be copied.", sm.name), colWarn);
        installedCount += added;
        if (!sm.scratch.empty())
            std::filesystem::remove_all(sm.scratch, ec);
        if (added == 0)
            ++failed;
    }
    staged_.clear();
    if (!installTempRoot_.empty())
        std::filesystem::remove_all(installTempRoot_, ec);
    installBusy_ = false;

    if (installedCount > 0) {
        syncModsFromStore();
        toast(std::format("Installed {} mod{}.", installedCount,
                          installedCount == 1 ? "" : "s"), colAccent);

        std::vector<std::string> newly;
        for (const auto& m : mods_)
            if (std::find(installBefore_.begin(), installBefore_.end(), m.name) == installBefore_.end())
                newly.push_back(m.name);
        if (!newly.empty()) {
            history_.push({ std::format("Install {} mod{}", newly.size(), newly.size() == 1 ? "" : "s"),
                [this, newly]{ uninstallByName(newly); },
                [this]{ toast("Re-install not supported from undo history.", colWarn); } });
        }
    } else if (failed > 0) {
        toast("Nothing installable found.", colWarn);
    }
}

void App::addModsClicked() {
    if (!gameReady_) { toast("Locate the game first.", colWarn); return; }
    auto picked = platform::pickArchives("Select mod archives to install");
    if (picked.empty())
        return;
    std::vector<std::filesystem::path> paths;
    for (const auto& p : picked)
        paths.push_back(core::pathFromUtf8(p));
    installMods(paths);
}

void App::launchClicked() {
    if (!gameReady_) { toast("Locate the game first.", colWarn); return; }
    if (core::launchGame())
        toast("Launching Subnautica 2 via Steam...", colAccent);
    else
        toast("Couldn't launch the game. Is Steam running?", colWarn);
}

bool App::sn2ModSettingsMissing() const {
    if (!gameReady_ || !game_.ue4ssInstalled() || !sn2ModSettingsNeeded_)
        return false;
    std::error_code ec;
    return !std::filesystem::exists(game_.paths().ue4ssMods / "SN2ModSettings", ec);
}

bool App::anyModNeedsSn2ModSettings() const {
    if (!store_)
        return false;
    constexpr std::string_view kNeedle = "./ue4ss/mods/sn2modsettings/";   // compared lowercased
    auto lower = [](const std::string& rel) {
        std::string s = rel;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    for (const auto& m : store_->mods()) {
        if (m.kind != core::ModKind::Ue4ss)
            continue;
        std::vector<std::pair<std::string, core::Bytes>> files;
        if (!store_->readModFiles(m.id, files))
            continue;
        for (const auto& [rel, bytes] : files) {
            if (bytes.empty() || !lower(rel).ends_with(".lua"))
                continue;
            std::string low(bytes.size(), '\0');
            std::transform(bytes.begin(), bytes.end(), low.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (low.find(kNeedle) != std::string::npos)
                return true;
        }
    }
    return false;
}

std::vector<std::string> App::modConflictWarnings() const {
    std::vector<std::string> out;
    if (!store_)
        return out;

    for (const auto& conflict : core::detectModConflicts(store_->mods())) {
        std::string joined;
        for (std::size_t i = 0; i < conflict.names.size(); ++i) {
            if (i)
                joined += (i + 1 == conflict.names.size()) ? " and " : ", ";
            joined += '"' + conflict.names[i] + '"';
        }
        if (conflict.kind == core::ModConflictKind::Pak)
            out.push_back(std::format("PAK conflict: {} share a \"{}\" pak — only one will load.",
                                      joined, conflict.target));
        else
            out.push_back(std::format("UE4SS conflict: {} share the \"{}\" Mods folder — only one can materialize cleanly.",
                                      joined, conflict.target));
    }
    return out;
}

namespace {
bool ue4ssBusy(Ue4ssStage s) {
    return s == Ue4ssStage::Querying || s == Ue4ssStage::Downloading || s == Ue4ssStage::Extracting;
}
}

void App::installUe4ss() {
    if (!gameReady_) { toast("Locate the game first.", colWarn); return; }
    if (ue4ssBusy(ue4ssStage_.load()))
        return;
    if (ue4ssThread_.joinable())
        ue4ssThread_.join();

    ue4ssDone_ = false;
    ue4ssProgress_ = 0.0f;
    ue4ssStage_ = Ue4ssStage::Querying;
    const core::GamePaths paths = game_.paths();
    ue4ssThread_ = std::thread([this, paths] {
        auto result = core::install(paths, [this](core::InstallPhase ph, float frac) {
            ue4ssProgress_.store(frac);
            switch (ph) {
                case core::InstallPhase::Querying:    ue4ssStage_ = Ue4ssStage::Querying;    break;
                case core::InstallPhase::Downloading: ue4ssStage_ = Ue4ssStage::Downloading; break;
                case core::InstallPhase::Extracting:  ue4ssStage_ = Ue4ssStage::Extracting;  break;
            }
        });
        { std::lock_guard<std::mutex> lk(ue4ssMsgMutex_); ue4ssMsg_ = result.message; }
        ue4ssStage_ = result.ok ? Ue4ssStage::Done : Ue4ssStage::Failed;
        ue4ssDone_ = true;
    });
}

void App::pollUe4ssInstall() {
    if (ue4ssBusy(ue4ssStage_.load()))
        ui::markAnimActive();
    if (!ue4ssDone_.exchange(false))
        return;
    if (ue4ssThread_.joinable())
        ue4ssThread_.join();

    std::string msg;
    { std::lock_guard<std::mutex> lk(ue4ssMsgMutex_); msg = ue4ssMsg_; }
    if (ue4ssStage_.load() == Ue4ssStage::Done) {
        game_.invalidateCache();
        loadMods();
        toast(msg.empty() ? "UE4SS installed." : msg, colAccent);
    } else {
        toast(msg.empty() ? "UE4SS install failed." : msg, colWarn);
    }
}

namespace {
bool reshadeStageBusy(ReshadeStage s) {
    return s == ReshadeStage::Querying || s == ReshadeStage::Downloading ||
           s == ReshadeStage::Installing || s == ReshadeStage::Verifying ||
           s == ReshadeStage::Uninstalling;
}
}

bool App::reshadeWorkerActive() const {
    return reshadeStageBusy(reshadeInstallStage_.load()) ||
           reshadeStageBusy(reshadeUninstallStage_.load()) ||
           reshadeShaderBusy_.load();
}

void App::installReshade() {
    if (!gameReady_) { toast("Locate the game first.", colWarn); return; }
    if (reshadeWorkerActive())
        return;
    if (reshadeInstallThread_.joinable())
        reshadeInstallThread_.join();

    reshadeInstallDone_ = false;
    reshadeInstallProgress_ = 0.0f;
    reshadeInstallStage_ = ReshadeStage::Querying;
    const core::GamePaths paths = game_.paths();
    reshadeInstallThread_ = std::thread([this, paths] {
        auto result = core::reshadeInstall(paths, [this](core::ReshadePhase ph, float frac) {
            reshadeInstallProgress_.store(frac);
            switch (ph) {
                case core::ReshadePhase::Querying:    reshadeInstallStage_ = ReshadeStage::Querying;    break;
                case core::ReshadePhase::Downloading: reshadeInstallStage_ = ReshadeStage::Downloading; break;
                case core::ReshadePhase::Installing:  reshadeInstallStage_ = ReshadeStage::Installing;  break;
                case core::ReshadePhase::Verifying:   reshadeInstallStage_ = ReshadeStage::Verifying;   break;
            }
        });
        {
            std::lock_guard<std::mutex> lk(reshadeMsgMutex_);
            reshadeInstallMsg_ = result.message;
            reshadeInstallVersion_ = result.version;
        }
        reshadeInstallStage_ = result.ok ? ReshadeStage::Done : ReshadeStage::Failed;
        reshadeInstallDone_ = true;
    });
}

void App::pollReshadeInstall() {
    if (reshadeStageBusy(reshadeInstallStage_.load()))
        ui::markAnimActive();
    if (!reshadeInstallDone_.exchange(false))
        return;
    if (reshadeInstallThread_.joinable())
        reshadeInstallThread_.join();

    std::string msg, version;
    {
        std::lock_guard<std::mutex> lk(reshadeMsgMutex_);
        msg = reshadeInstallMsg_;
        version = reshadeInstallVersion_;
    }
    game_.invalidateCache();
    if (reshadeInstallStage_.load() == ReshadeStage::Done) {
        config_.reshadeVersion = version;
        saveConfig();
        if (shaders_)
            shaders_->ensureRecursiveSearchPaths();
        if (store_)
            store_->reapplyReshadePresets();   // re-point a preset chosen before ReShade existed
        toast(msg.empty() ? "ReShade installed." : msg, colAccent);
    } else {
        toast(msg.empty() ? "ReShade install failed." : msg, colWarn);
    }
}

void App::uninstallReshade() {
    if (!gameReady_)
        return;
    if (reshadeWorkerActive())
        return;
    if (reshadeUninstallThread_.joinable())
        reshadeUninstallThread_.join();

    reshadeUninstallDone_ = false;
    reshadeUninstallStage_ = ReshadeStage::Uninstalling;
    const core::GamePaths paths = game_.paths();
    reshadeUninstallThread_ = std::thread([this, paths] {
        auto result = core::reshadeUninstall(paths);
        { std::lock_guard<std::mutex> lk(reshadeMsgMutex_); reshadeUninstallMsg_ = result.message; }
        reshadeUninstallStage_ = result.ok ? ReshadeStage::Done : ReshadeStage::Failed;
        reshadeUninstallDone_ = true;
    });
}

void App::pollReshadeUninstall() {
    if (reshadeStageBusy(reshadeUninstallStage_.load()))
        ui::markAnimActive();
    if (!reshadeUninstallDone_.exchange(false))
        return;
    if (reshadeUninstallThread_.joinable())
        reshadeUninstallThread_.join();

    std::string msg;
    { std::lock_guard<std::mutex> lk(reshadeMsgMutex_); msg = reshadeUninstallMsg_; }
    game_.invalidateCache();
    if (reshadeUninstallStage_.load() == ReshadeStage::Done) {
        config_.reshadeVersion.clear();
        saveConfig();
        if (shaders_)
            shaders_->load();   // reconcile manifest against the now-empty reshade-shaders tree
    }
    toast(msg.empty() ? "ReShade uninstalled." : msg, colAccent);
}

void App::installStandardShaders(int branch) {
    if (!gameReady_ || !shaders_)
        return;
    if (reshadeWorkerActive())
        return;
    if (reshadeShaderThread_.joinable())
        reshadeShaderThread_.join();

    reshadeShaderDone_ = false;
    reshadeShaderProgress_ = 0.0f;
    reshadeShaderBusy_ = true;
    const core::StandardBranch b = branch == 1 ? core::StandardBranch::Latest : core::StandardBranch::Slim;
    reshadeShaderThread_ = std::thread([this, b] {
        auto result = shaders_->installStandard(b, [this](core::ShaderPhase, float frac) {
            reshadeShaderProgress_.store(frac);
        });
        { std::lock_guard<std::mutex> lk(reshadeMsgMutex_); reshadeShaderMsg_ = result.message; }
        reshadeShaderBusy_ = false;
        reshadeShaderDone_ = true;
    });
}

void App::importShaderPack() {
    if (!gameReady_ || !shaders_ || reshadeWorkerActive())
        return;
    auto picked = platform::pickFile("Choose a shader pack (.zip)", L"Shader pack", L"*.zip");
    if (!picked)
        return;
    if (reshadeShaderThread_.joinable())
        reshadeShaderThread_.join();

    reshadeShaderDone_ = false;
    reshadeShaderProgress_ = 0.0f;
    reshadeShaderBusy_ = true;
    const std::filesystem::path source = core::pathFromUtf8(*picked);
    const std::string name = core::narrow(source.stem().wstring());
    reshadeShaderThread_ = std::thread([this, source, name] {
        auto result = shaders_->importPack(source, name, [this](core::ShaderPhase, float frac) {
            reshadeShaderProgress_.store(frac);
        });
        { std::lock_guard<std::mutex> lk(reshadeMsgMutex_); reshadeShaderMsg_ = result.message; }
        reshadeShaderBusy_ = false;
        reshadeShaderDone_ = true;
    });
}

void App::pollReshadeShader() {
    if (reshadeShaderBusy_.load())
        ui::markAnimActive();
    if (!reshadeShaderDone_.exchange(false))
        return;
    if (reshadeShaderThread_.joinable())
        reshadeShaderThread_.join();

    std::string msg;
    { std::lock_guard<std::mutex> lk(reshadeMsgMutex_); msg = reshadeShaderMsg_; }
    toast(msg.empty() ? "Shader pack updated." : msg, colAccent);
}

void App::checkForUpdates(bool notifyWhenCurrent) {
    if (updateBusy_.load())
        return;
    if (updateThread_.joinable())
        updateThread_.join();

    updateDone_ = false;
    updateBusy_ = true;
    updateNotifyWhenCurrent_ = notifyWhenCurrent;
    const bool includePre = config_.includePrereleases;
    updateThread_ = std::thread([this, includePre] {
        core::UpdateCheckResult result = core::checkForUpdates(includePre);
        { std::lock_guard<std::mutex> lk(updateMutex_); updateResult_ = std::move(result); }
        updateBusy_ = false;
        updateDone_ = true;
    });
}

void App::pollUpdateCheck() {
    if (updateBusy_.load())
        ui::markAnimActive();
    if (!updateDone_.exchange(false))
        return;
    if (updateThread_.joinable())
        updateThread_.join();

    core::UpdateCheckResult result;
    { std::lock_guard<std::mutex> lk(updateMutex_); result = updateResult_; }
    if (!result.ok) {
        if (updateNotifyWhenCurrent_.load())
            toast(result.message.empty() ? "Update check failed." : result.message, colWarn);
        return;
    }
    if (result.updateAvailable) {
        toast(std::format("Update available: {}", result.latestVersion), colAccent);
    } else if (updateNotifyWhenCurrent_.load()) {
        toast("S2ModManager is up to date.", colAccent);
    }
}

void App::render(int displayW, int displayH) {
    pollUe4ssInstall();
    pollReshadeInstall();
    pollReshadeUninstall();
    pollReshadeShader();
    pollUpdateCheck();
    pollInstall();
    pollProfileShare();
    processToggles();

    dropAnim_.to(dragOver_ ? 1.0f : 0.0f);
    float dropA = dropAnim_.update(ImGui::GetIO().DeltaTime);
    if (dropA > 0.001f) ui::markAnimActive();

    if (scaleAnimT_ >= 0.0f) {
        constexpr float kScaleAnimDur = 0.25f;   // 250ms
        scaleAnimT_ += ImGui::GetIO().DeltaTime;
        float t = std::min(scaleAnimT_ / kScaleAnimDur, 1.0f);
        uiScaleCurrent_ = scaleAnimFrom_ + (scaleAnimTo_ - scaleAnimFrom_) * ui::easeInOutCubic(t);
        ui::setUserScale(uiScaleCurrent_);
        ImGui::GetStyle().FontScaleMain = uiScaleCurrent_;
        ui::markAnimActive();
        if (t >= 1.0f) {
            scaleAnimT_ = -1.0f;
            uiScaleCurrent_ = scaleAnimTo_;
            ui::setUserScale(uiScaleCurrent_);
            ImGui::GetStyle().FontScaleMain = uiScaleCurrent_;
        }
    }

    if (!ImGui::GetIO().WantTextInput) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        bool shift = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !shift) {
            std::string l = history_.undo();
            if (!l.empty()) toast(std::format("Undid: {}", l), colAccent);
        }
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (shift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
            std::string l = history_.redo();
            if (!l.empty()) toast(std::format("Redid: {}", l), colAccent);
        }
    }

    if (saveDirty_) {
        ui::markAnimActive();
        saveTimer_ -= ImGui::GetIO().DeltaTime;
        if (saveTimer_ <= 0.0f || !ImGui::IsAnyItemActive()) {
            saveDirty_ = false;
            (void)config_.save();
        }
    }

    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())
        ui::markAnimActive();

    renderToasts();

    float w = static_cast<float>(displayW);
    float h = static_cast<float>(displayH);
    float s = uiScale();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    if (backgroundActive())
        flags |= ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar();

    if (backgroundActive())
        background_.draw(ImVec2(w, h));

    if (config_.themeMode == "subnautica") {
        ImVec4 hc = toVec(colAccent); hc.w = backgroundActive() ? 0.11f : 0.07f;
        drawHexField(ImGui::GetWindowDrawList(), ImVec2(0.0f, 0.0f), ImVec2(w, h),
                     ImGui::ColorConvertFloat4ToU32(hc), 46.0f * s);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

    if (!gameReady_) {
        renderOnboarding();
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec(panelBg(colSurface)));
    ImGui::BeginChild("##topbar", ImVec2(0.0f, kTopBarH * s), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderTopBar();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    settingsAnim_.to(settingsOpen_ ? 1.0f : 0.0f);
    float a = settingsAnim_.update(ImGui::GetIO().DeltaTime);
    if (settingsOpen_ && ImGui::IsKeyPressed(ImGuiKey_Escape))
        settingsOpen_ = false;

    renderBanners();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec(panelBg(colBg)));
    ImGui::BeginChild("##list", ImVec2(w - kDetailsW * s, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderModList();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec(panelBg(colSurface)));
    ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    renderDetails();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (a > 0.001f)
        renderSettingsView(a);
    else if (window_)
        window_->setBackdropBlur(nullptr, 0.0f);

    renderShareModal();

    ImGui::PopStyleVar();
    ImGui::End();

    if (dropA > 0.001f) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float s2 = uiScale();
        auto withA = [&](ImU32 c, float m) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c); v.w *= dropA * m;
            return ImGui::ColorConvertFloat4ToU32(v);
        };
        fg->AddRectFilled(ImVec2(0, 0), disp, withA(colBg, 0.72f));
        float m = 28.0f * s2;
        float phase = -static_cast<float>(ImGui::GetTime()) * 36.0f;
        ui::dashedRect(fg, ImVec2(m, m), ImVec2(disp.x - m, disp.y - m),
                       withA(colAccent, 1.0f), 10.0f * s2, 2.0f * s2, phase);
        ImGui::PushFont(fonts().title);
        const char* msg = "Drop to install";
        ImVec2 ms = ImGui::CalcTextSize(msg);
        ui::textSnapped(fg, ImVec2((disp.x - ms.x) * 0.5f, (disp.y - ms.y) * 0.5f),
                        withA(colTextHi, 1.0f), msg);
        ImGui::PopFont();
    }
}

void App::renderOnboarding() {
    float s = uiScale();
    ImVec2 c0 = ImGui::GetWindowPos();
    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cx = w * 0.5f;
    float top = h * 0.30f;

    if (ImTextureID logo = ui::icons().tex(ui::Icon::Logo)) {
        float is = 60.0f * s;
        ui::icons().draw(dl, ui::Icon::Logo, ImVec2(c0.x + cx - is * 0.5f, c0.y + top - is * 0.5f), is,
                         IM_COL32(255, 255, 255, 255));
    } else {
        dl->AddCircleFilled(ImVec2(c0.x + cx, c0.y + top), 10.0f * s, colAccent);
    }

    ImGui::PushFont(fonts().title);
    const char* title = "Locate Subnautica 2";
    ImVec2 ts = ImGui::CalcTextSize(title);
    textSnapped(dl, ImVec2(c0.x + cx - ts.x * 0.5f, c0.y + top + 38.0f * s), colTextHi, title);
    ImGui::PopFont();

    const char* sub = "Find your install so the manager can read and write mods.";
    ImVec2 ss = ImGui::CalcTextSize(sub);
    textSnapped(dl, ImVec2(c0.x + cx - ss.x * 0.5f, c0.y + top + 72.0f * s), colTextDim, sub);

    float bw = 220.0f * s, bh = 34.0f * s, gap = 10.0f * s;
    float by = top + 108.0f * s;
    ImGui::SetCursorPos(ImVec2(cx - bw * 0.5f, by));
    if (ui::primaryButton("Detect via Steam", ImVec2(bw, bh)))
        detectGame();
    ImGui::SetCursorPos(ImVec2(cx - bw * 0.5f, by + bh + gap));
    if (ui::ghostButton("Browse to folder...", ImVec2(bw, bh)))
        browseGame();

    if (!statusMessage_.empty()) {
        ImVec2 es = ImGui::CalcTextSize(statusMessage_.c_str());
        textSnapped(dl, ImVec2(c0.x + cx - es.x * 0.5f, c0.y + by + 2.0f * (bh + gap) + 4.0f * s),
                    colWarn, statusMessage_.c_str());
    }

    ImGui::SetCursorPos(ImVec2(0.0f, h - 2.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void App::renderToasts() {
    if (toasts_.empty())
        return;

    ui::markAnimActive();

    float s = uiScale();
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const float margin = 14.0f * s, padX = 12.0f * s, padY = 8.0f * s, gap = 8.0f * s;
    float y = disp.y - margin;

    for (int i = static_cast<int>(toasts_.size()) - 1; i >= 0; --i) {
        Toast& t = toasts_[i];
        t.age += dt;
        t.ttl -= dt;
        if (t.ttl <= 0.0f) {
            toasts_.erase(toasts_.begin() + i);
            continue;
        }
        float fadeIn = t.age < 0.18f ? t.age / 0.18f : 1.0f;
        float fadeOut = t.ttl < 0.4f ? t.ttl / 0.4f : 1.0f;
        float alpha = ui::easeOutCubic(fadeIn < fadeOut ? fadeIn : fadeOut);
        float slideIn = (1.0f - ui::easeOutCubic(fadeIn)) * 24.0f * s;

        ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
        float cardW = ts.x + padX * 2.0f;
        float cardH = ts.y + padY * 2.0f;
        float x1 = disp.x - margin - cardW;
        float y1 = y - cardH;

        auto withA = [&](ImU32 c, float mul) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
            v.w *= alpha * mul;
            return ImGui::ColorConvertFloat4ToU32(v);
        };

        float ty = y1 + slideIn;
        dl->AddRectFilled(ImVec2(x1, ty), ImVec2(x1 + cardW, ty + cardH), withA(colSurface3, 0.97f), 5.0f * s);
        dl->AddRect(ImVec2(x1, ty), ImVec2(x1 + cardW, ty + cardH), withA(t.color, 1.0f), 5.0f * s, 0, 1.5f);
        dl->AddRectFilled(ImVec2(x1, ty), ImVec2(x1 + 3.0f * s, ty + cardH), withA(t.color, 1.0f), 5.0f * s);
        dl->AddText(ImVec2(px(x1 + padX), px(ty + padY)), withA(colTextHi, 1.0f), t.text.c_str());

        y = y1 - gap;
    }
}

void App::renderTopBar() {
    float s = uiScale();
    float barH = kTopBarH * s;
    ImVec2 o = ImGui::GetWindowPos();
    float w = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (config_.themeMode == "subnautica")
        glassPanel(dl, o, ImVec2(o.x + w, o.y + barH), s);
    dl->AddRectFilled(ImVec2(o.x, o.y + barH - 1.0f), ImVec2(o.x + w, o.y + barH), colBorder);

    float titleX = o.x + 36.0f * s;
    if (ImTextureID logo = ui::icons().tex(ui::Icon::Logo)) {
        float ls = 24.0f * s;
        ui::icons().draw(dl, ui::Icon::Logo, ImVec2(o.x + 11.0f * s, o.y + (barH - ls) * 0.5f), ls,
                         IM_COL32(255, 255, 255, 255));
    } else {
        dl->AddCircleFilled(ImVec2(o.x + 20.0f * s, o.y + barH * 0.5f), 5.0f * s, colAccent);
        titleX = o.x + 34.0f * s;
    }

    ImGui::PushFont(fonts().semibold);
    float titleH = ImGui::GetTextLineHeight();
    textSnapped(dl, ImVec2(titleX, o.y + (barH - titleH) * 0.5f), colTextHi, "S2 Mod Manager");
    ImGui::PopFont();

    float bh = 26.0f * s;
    float launchW = 98.0f * s, gearW = 90.0f * s, shareW = 80.0f * s, profW = 140.0f * s, gapX = 8.0f * s;
    float x = w - launchW - 10.0f * s;
    ImGui::SetCursorPos(ImVec2(x, (barH - bh) * 0.5f));
    ImGui::BeginDisabled(!game_.ue4ssInstalled());
    if (ui::primaryButton("Launch", ui::icons().tex(ui::Icon::Play), ImVec2(launchW, bh)))
        launchClicked();
    ImGui::EndDisabled();

    x -= gearW + gapX;
    ImGui::SetCursorPos(ImVec2(x, (barH - bh) * 0.5f));
    if (ui::ghostButton("Settings", ui::icons().tex(ui::Icon::Settings), ImVec2(gearW, bh)))
        settingsOpen_ = !settingsOpen_;

    x -= shareW + gapX;
    ImGui::SetCursorPos(ImVec2(x, (barH - bh) * 0.5f));
    ImGui::BeginDisabled(!game_.ue4ssInstalled());
    if (ui::ghostButton("Share", ui::icons().tex(ui::Icon::Share), ImVec2(shareW, bh)))
        shareModalOpen_ = true;
    ImGui::EndDisabled();

    x -= profW + gapX;
    ImGui::SetCursorPos(ImVec2(x, (barH - bh) * 0.5f));
    renderProfileCombo(profW);
}

void App::renderBanners() {
    float s = uiScale();

    struct Banner { std::string text; ImU32 color; std::string url; };
    std::vector<Banner> banners;
    {
        core::UpdateCheckResult update;
        { std::lock_guard<std::mutex> lk(updateMutex_); update = updateResult_; }
        if (update.ok && update.updateAvailable)
            banners.push_back({ std::format("Update available: {} (you have {}). Click to view the release.",
                                            update.latestVersion, update.currentVersion),
                                colAccent,
                                update.releaseUrl.empty() ? core::kAppReleasesUrl : update.releaseUrl });
    }
    if (sn2ModSettingsMissing())
        banners.push_back({ "An installed mod needs SN2ModSettings for its in-game options menu, "
                            "but it isn't installed. Add it like any other UE4SS mod.", colWarn });
    for (const auto& c : conflictWarnings_)
        banners.push_back({ c, colWarn });
    if (banners.empty())
        return;

    const float bh = 34.0f * s;
    for (std::size_t i = 0; i < banners.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginChild("##banner", ImVec2(0.0f, bh), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetWindowPos();
        float w = ImGui::GetWindowWidth();

        ImVec4 tint = toVec(banners[i].color); tint.w = 0.16f;
        dl->AddRectFilled(o, ImVec2(o.x + w, o.y + bh), ImGui::ColorConvertFloat4ToU32(tint));
        dl->AddRectFilled(o, ImVec2(o.x + 3.0f * s, o.y + bh), banners[i].color);
        dl->AddLine(ImVec2(o.x, o.y + bh - 1.0f), ImVec2(o.x + w, o.y + bh - 1.0f), colBorder);

        float lineH = ImGui::GetTextLineHeight();
        textSnapped(dl, ImVec2(o.x + 14.0f * s, o.y + (bh - lineH) * 0.5f), colTextHi, banners[i].text.c_str());

        if (!banners[i].url.empty()) {
            ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
            if (ImGui::InvisibleButton("##bannerLink", ImVec2(w, bh)))
                platform::openUrl(banners[i].url);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

void App::renderProfileCombo(float width) {
    float s = uiScale();
    if (!store_)
        return;
    ImVec2 comboPos = ImGui::GetCursorScreenPos();
    ImDrawList* pdl = ImGui::GetWindowDrawList();   // parent window list (combo popup uses its own)
    float comboH = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, 10000.0f));
    ImGui::BeginDisabled(reshadeWorkerActive());   // switching writes ReShade files the worker also touches
    if (ImGui::BeginCombo("##profile", store_->activeName().c_str(), ImGuiComboFlags_NoArrowButton)) {
        const std::vector<core::ProfileInfo> profiles = store_->profiles();
        const bool canDelete = profiles.size() > 1;
        std::string toActivate, toDelete;
        for (const auto& p : profiles) {
            ImGui::PushID(p.id.c_str());
            bool sel = (p.id == store_->activeId());
            float selW = canDelete ? width - 58.0f * s : 0.0f;
            if (ImGui::Selectable(p.name.c_str(), sel, 0, ImVec2(selW, 0.0f)))
                toActivate = p.id;
            if (canDelete) {
                ImGui::SameLine(0.0f, 6.0f * s);
                if (ImGui::SmallButton("x"))
                    toDelete = p.id;
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Selectable("+  New profile")) {
            auto exists = [&](const std::string& nm) {
                for (const auto& p : profiles) if (p.name == nm) return true;
                return false;
            };
            std::string newName;
            int suffix = static_cast<int>(profiles.size());
            do { newName = std::format("Modded {}", suffix++); } while (exists(newName));
            toActivate = store_->createProfile(newName);
        }
        if (ImGui::Selectable("Duplicate current"))
            toActivate = store_->duplicateProfile(store_->activeId(), store_->activeName() + " copy");
        ImGui::EndCombo();

        if (!toDelete.empty()) {
            store_->deleteProfile(toDelete);
            history_.clear();
            loadMods();
            saveConfig();
        } else if (!toActivate.empty() && toActivate != store_->activeId()) {
            store_->activate(toActivate);
            history_.clear();
            loadMods();
            saveConfig();
        }
    }
    ImGui::EndDisabled();

    if (ui::icons().tex(ui::Icon::ChevronDown)) {
        float cs = 13.0f * s;
        ui::icons().draw(pdl, ui::Icon::ChevronDown,
                         ImVec2(comboPos.x + width - cs - 9.0f * s, comboPos.y + (comboH - cs) * 0.5f),
                         cs, colTextDim);
    }
}

void App::renderShareModal() {
    float s = uiScale();

    if (!shareModalOpen_ && shareModalWasOpen_) {
        shareService_.stop();
        receiveKeyBuffer_.fill('\0');
        if (store_ && !receivingProfileId_.empty() && receivingProfileId_ != store_->activeId()) {
            store_->deleteProfile(receivingProfileId_);
            loadMods();
        }
        receivingProfileId_.clear();
    }
    shareModalWasOpen_ = shareModalOpen_;

    shareAnim_.to(shareModalOpen_ ? 1.0f : 0.0f);
    float a = shareAnim_.update(ImGui::GetIO().DeltaTime);
    if (a < 0.001f)
        return;
    if (shareModalOpen_ && ImGui::IsKeyPressed(ImGuiKey_Escape))
        shareModalOpen_ = false;
    float e = ui::easeOutCubic(a);

    ImVec2 full = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##shareOverlay", full, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImDrawList* odl = ImGui::GetWindowDrawList();
    if (window_)
        window_->setBackdropBlur(odl, a);
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec4 dv = toVec(colBg);
        int da = static_cast<int>(a * 0.34f * 255.0f);
        dv.w = (da < 1 ? 1 : da) / 255.0f;
        odl->AddRectFilled(wp, ImVec2(wp.x + full.x, wp.y + full.y), ImGui::ColorConvertFloat4ToU32(dv));
    }
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    if (ImGui::InvisibleButton("##shareScrim", full))
        shareModalOpen_ = false;

    const float W = 452.0f * s;
    const float winPad = 18.0f * s;
    const float cardW = W + winPad * 2.0f;
    float cardX = std::max(16.0f * s, (full.x - cardW) * 0.5f);
    float cardTop = full.y * 0.12f + (1.0f - e) * 16.0f * s;
    ImGui::SetCursorPos(ImVec2(cardX, cardTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec(panelBg(colSurface)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(winPad, 16.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 8.0f * s));
    ImGui::BeginChild("##shareCard", ImVec2(cardW, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    if (shareService_.busy())
        ui::markAnimActive();

    const core::ShareState st = shareService_.state();
    const bool busy = shareService_.busy();
    const bool hosting = busy && shareService_.mode() == core::ShareMode::Host;
    const bool receiving = busy && shareService_.mode() == core::ShareMode::Receive;
    const bool fileBusy = busy && (shareService_.mode() == core::ShareMode::Export ||
                                   shareService_.mode() == core::ShareMode::Import);
    const std::string status = shareService_.statusMessage();

    const float padX = 14.0f * s, padY = 12.0f * s;
    const float innerW = W - padX * 2.0f;
    const float rounding = 8.0f * s;
    const ImU32 colOk = IM_COL32(120, 196, 120, 255);
    const double now = ImGui::GetTime();
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 4.0f);

    ImGui::Dummy(ImVec2(W, 0.0f));

    // ---- Header: glyph + title + active-profile subtitle ------------------
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float icR = 14.0f * s;
        ImVec2 ic(p.x + icR, p.y + icR);
        dl->AddCircleFilled(ic, icR, colAccentDim);
        if (ImTextureID sh = ui::icons().tex(ui::Icon::Share)) {
            float is = icR * 1.15f;
            ui::drawIconTex(dl, sh, ImVec2(px(ic.x - is * 0.5f), px(ic.y - is * 0.5f)),
                            ImVec2(px(ic.x - is * 0.5f) + is, px(ic.y - is * 0.5f) + is), colAccentHi);
        } else {
            drawShareGlyph(dl, ImVec2(ic.x - 1.0f * s, ic.y), icR * 0.5f, colAccentHi);
        }

        float tx = p.x + icR * 2.0f + 12.0f * s;
        ImGui::PushFont(fonts().title);
        textSnapped(dl, ImVec2(tx, p.y - 1.0f * s), colTextHi, "Share Profile");
        ImGui::PopFont();
        std::string sub = store_ ? std::format("Active profile: {}", store_->activeName())
                                 : "No profile selected";
        textSnapped(dl, ImVec2(tx, p.y + 16.0f * s), colTextDim, sub.c_str());

        float xsz = 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - xsz, p.y - 2.0f * s));
        ImTextureID xico = ui::icons().tex(ui::Icon::Close);
        bool closeClick = xico ? ui::iconButton("##shareclose", xico, ImVec2(xsz, xsz), colTextDim, false)
                               : ui::ghostButton("X", ImVec2(xsz, xsz));
        if (closeClick)
            shareModalOpen_ = false;

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + icR * 2.0f + 4.0f * s));
    }
    ImGui::Spacing();

    // ---- Segmented tab control -------------------------------------------
    if (hosting) shareTab_ = 0;
    else if (receiving) shareTab_ = 1;
    else if (fileBusy) shareTab_ = 2;

    const char* tabs[] = { "Send", "Receive", "File" };
    const bool tabEnabled[3] = {
        !(receiving || fileBusy),
        !(hosting || fileBusy),
        !(hosting || receiving),
    };
    ImTextureID segIcons[3] = {
        ui::icons().tex(ui::Icon::Upload),
        ui::icons().tex(ui::Icon::Download),
        ui::icons().tex(ui::Icon::HardDrive),
    };
    segmentedControl("##sharetabs", &shareTab_, tabs, tabEnabled, 3, W, s, segIcons);
    ImGui::Spacing();

    // ---- Cross-fade the active tab's body in on switch -------------------
    ImGuiStorage* ss = ImGui::GetStateStorage();
    ImGuiID fadeId = ImGui::GetID("##sharefade");
    ImGuiID lastId = ImGui::GetID("##sharelasttab");
    if (ss->GetInt(lastId, -1) != shareTab_) {
        ui::animSet(fadeId, 0.0f);
        ss->SetInt(lastId, shareTab_);
    }
    float ease = ui::easeOutCubic(ui::animTo(fadeId, 1.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ease);

    if (shareTab_ == 0) {
        // ---- SEND ---------------------------------------------------------
        if (!hosting) {
            Card c; c.begin(W, padX, padY);
            ImGui::TextDisabled("CONNECTION METHOD");
            ImGui::Spacing();
            ImGui::RadioButton("UPnP  (automatic)", &hostPortMode_, 0);
            ImGui::SameLine(0.0f, 18.0f * s);
            ImGui::RadioButton("Manual port forward", &hostPortMode_, 1);
            ImGui::Spacing();
            if (hostPortMode_ == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
                ImGui::TextWrapped("A connection key is generated automatically once UPnP maps a port on your router.");
                ImGui::PopStyleColor();
            } else {
                if (hostPortBuffer_[0] == '\0')
                    std::memcpy(hostPortBuffer_.data(), "50000", 6);
                ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
                ImGui::TextWrapped("Forward this TCP port to this PC on your router, then generate a key.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::SetNextItemWidth(110.0f * s);
                ImGui::InputText("##port", hostPortBuffer_.data(), hostPortBuffer_.size(),
                                 ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine(0.0f, 8.0f * s);
                ImGui::TextDisabled("TCP port");
                ImGui::SetNextItemWidth(innerW);
                ImGui::InputTextWithHint("##ip", "Public IP (auto-detect if blank)",
                                         hostIpBuffer_.data(), hostIpBuffer_.size());
            }
            c.end(colSurface, colBorder, rounding);

            ImGui::Spacing();
            ImGui::BeginDisabled(!store_.has_value());
            if (ui::primaryButton("Generate connection key", ui::icons().tex(ui::Icon::Key), ImVec2(W, 34.0f * s)) && store_) {
                std::thread([] { platform::ensureInboundAllowed(L"S2ModManager P2P Share"); }).detach();
                core::HostOptions opts;
                opts.useUpnp = (hostPortMode_ == 0);
                if (!opts.useUpnp) {
                    opts.fixedPort = static_cast<std::uint16_t>(std::atoi(hostPortBuffer_.data()));
                    opts.ipOverride = std::string(hostIpBuffer_.data());
                }
                shareService_.startHosting(game_.paths(), *store_, store_->activeName(), opts);
            }
            ImGui::EndDisabled();
        } else {
            std::string key = shareService_.connectionKey();
            Card c; c.begin(W, padX, padY);
            if (!key.empty()) {
                ImGui::TextDisabled("CONNECTION KEY");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
                ImGui::TextWrapped("Send this key to your peer so they can connect:");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::SetNextItemWidth(innerW);
                ImGui::InputText("##key", key.data(), key.size() + 1, ImGuiInputTextFlags_ReadOnly);
                ImGui::Spacing();
                bool copied = (now - keyCopiedAt_) < 1.3;
                ImTextureID copyIco = ui::icons().tex(copied ? ui::Icon::Check : ui::Icon::Copy);
                if (ui::ghostButton(copied ? "Copied!" : "Copy to clipboard", copyIco, ImVec2(178.0f * s, 28.0f * s))) {
                    ImGui::SetClipboardText(key.c_str());
                    keyCopiedAt_ = now;
                }
                if (copied) ui::markAnimActive();
            } else {
                statusDot(colWarn, colText, "Mapping a port on your router...", s, pulse);
            }
            c.end(colSurface, colBorder, rounding);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
            ImGui::TextWrapped("Peer can't connect? Allow this app through Windows Firewall (run as admin once).");
            ImGui::PopStyleColor();
        }
    } else if (shareTab_ == 1) {
        // ---- RECEIVE ------------------------------------------------------
        Card c; c.begin(W, padX, padY);
        ImGui::TextDisabled("CONNECTION KEY");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
        ImGui::TextWrapped("Paste a key from a peer to pull their profile into a new local profile.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::SetNextItemWidth(innerW);
        ImGui::InputTextWithHint("##recvkey", "Paste connection key...",
                                 receiveKeyBuffer_.data(), receiveKeyBuffer_.size(),
                                 receiving ? ImGuiInputTextFlags_ReadOnly : 0);
        c.end(colSurface, colBorder, rounding);

        ImGui::Spacing();
        ImGui::BeginDisabled(receiving || !store_.has_value() || receiveKeyBuffer_[0] == '\0');
        if (ui::primaryButton(receiving ? "Connecting..." : "Connect & receive",
                              ui::icons().tex(ui::Icon::Link), ImVec2(W, 34.0f * s)) && store_) {
            receivingProfileId_ = store_->createProfile("Receiving...");
            shareService_.startReceiving(game_.paths(), *store_,
                                         std::string(receiveKeyBuffer_.data()), receivingProfileId_);
        }
        ImGui::EndDisabled();
    } else {
        // ---- FILE ---------------------------------------------------------
        Card c; c.begin(W, padX, padY);
        ImGui::TextDisabled("PROFILE FILE");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextDim));
        ImGui::TextWrapped("Save the active profile to a .s2profile file, or import one a friend sent you.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        float bw = (innerW - 10.0f * s) * 0.5f;
        ImGui::BeginDisabled(busy || !store_.has_value());
        if (ui::primaryButton("Export...", ui::icons().tex(ui::Icon::FileDown), ImVec2(bw, 30.0f * s)) && store_) {
            std::string def = store_->activeName() + ".s2profile";
            if (auto path = platform::saveFile("Export profile", def.c_str(),
                                               L"S2 Profile", L"*.s2profile", L"s2profile"))
                shareService_.startExport(*store_, store_->activeName(), *path);
        }
        ImGui::SameLine(0.0f, 10.0f * s);
        if (ui::ghostButton("Import...", ui::icons().tex(ui::Icon::FileUp), ImVec2(bw, 30.0f * s)) && store_) {
            if (auto path = platform::pickFile("Import profile", L"S2 Profile", L"*.s2profile")) {
                receivingProfileId_ = store_->createProfile("Importing...");
                shareService_.startImport(*store_, *path, receivingProfileId_);
            }
        }
        ImGui::EndDisabled();
        c.end(colSurface, colBorder, rounding);
    }

    // ---- Shared status + progress strip ----------------------------------
    const bool showProgress = (st == core::ShareState::Transferring);
    if (showProgress || st == core::ShareState::Failed || st == core::ShareState::Done ||
        hosting || receiving || !status.empty()) {
        ImGui::Spacing();
        Card c; c.begin(W, padX, 10.0f * s);
        if (st == core::ShareState::Failed) {
            statusIcon(ui::Icon::Alert, colWarn, colText, status.empty() ? "Transfer failed." : status.c_str(), s);
        } else if (showProgress) {
            statusDot(colAccentHi, colText, status.empty() ? "Transferring..." : status.c_str(), s, pulse);
            ImGui::Spacing();
            ui::progressBar("##shareprog", shareService_.progress(), ImVec2(innerW, 8.0f * s));
        } else if (st == core::ShareState::Done) {
            statusIcon(ui::Icon::CircleCheck, colOk, colText, status.empty() ? "Done." : status.c_str(), s);
        } else if (hosting) {
            statusDot(colAccentHi, colText, status.empty() ? "Waiting for a peer to connect..." : status.c_str(), s, pulse);
        } else if (receiving) {
            statusDot(colAccentHi, colText, status.empty() ? "Connecting..." : status.c_str(), s, pulse);
        } else {
            statusDot(colTextDim, colTextDim, status.c_str(), s, 0.0f);
        }
        c.end(lerpColor(colSurface, colBg, 0.4f), 0, rounding);
    }

    // ---- Stop / cancel an active transfer --------------------------------
    if (hosting || receiving) {
        ImGui::Spacing();
        if (ui::ghostButton(hosting ? "Stop sharing" : "Cancel", ImVec2(W, 28.0f * s)))
            shareService_.stop();
    }

    ImGui::PopStyleVar();   // tab cross-fade alpha

    ImGui::EndChild();      // ##shareCard
    ImGui::PopStyleVar(3);  // window padding + child rounding + item spacing
    ImGui::PopStyleColor(); // card surface
    ImGui::EndChild();      // ##shareOverlay
    ImGui::PopStyleColor(); // overlay bg
}

void App::pollProfileShare() {
    if (shareService_.busy())
        ui::markAnimActive();

    if (auto imported = shareService_.takeImportedProfile()) {
        if (store_ && !receivingProfileId_.empty()) {
            const std::string name = *imported;
            auto exists = [&](const std::string& nm) {
                for (const auto& p : store_->profiles())
                    if (p.id != receivingProfileId_ && p.name == nm) return true;
                return false;
            };
            std::string unique = name;
            for (int n = 2; exists(unique); ++n)
                unique = std::format("{} ({})", name, n);
            store_->renameProfile(receivingProfileId_, unique);
            store_->activate(receivingProfileId_);
            loadMods();
            saveConfig();
            toast(std::format("Imported profile '{}'", unique), colAccent);
        }
        receivingProfileId_.clear();
    }
}

void App::renderModList() {
    float s = uiScale();
    ImVec2 c0 = ImGui::GetWindowPos();
    float w = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lineH = ImGui::GetTextLineHeight();
    float frameH = ImGui::GetFrameHeight();
    const float pad = 12.0f * s;

    if (config_.themeMode == "subnautica")
        glassPanel(dl, c0, ImVec2(c0.x + w, c0.y + ImGui::GetWindowHeight()), s);

    auto typeMatches = [&](core::ModKind t) {
        return typeFilter_ == 0 || (typeFilter_ == 1 && t == core::ModKind::Pak)
                                || (typeFilter_ == 2 && t == core::ModKind::Ue4ss);
    };
    auto rowShown = [&](int i) {
        return (matchesSearch(mods_[i].name, searchBuf_.data())
                || matchesSearch(mods_[i].displayName, searchBuf_.data()))
               && typeMatches(mods_[i].type);
    };
    const bool filtered = searchBuf_[0] != '\0' || typeFilter_ != 0;

    ImGui::PushFont(fonts().title);
    textSnapped(dl, ImVec2(c0.x + pad, c0.y + 12.0f * s), colTextHi, "Mods");
    ImGui::PopFont();

    float bw = 98.0f * s, bh = 24.0f * s;
    ImGui::SetCursorPos(ImVec2(w - bw - pad, 10.0f * s));
    ImGui::BeginDisabled(!game_.ue4ssInstalled());
    if (ui::primaryButton("Add mods", ui::icons().tex(ui::Icon::Plus), ImVec2(bw, bh)))
        addModsClicked();
    ImGui::EndDisabled();

    if (game_.ue4ssInstalled()) {
        if (!settingsOpen_ && !shareModalOpen_ && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
            && ImGui::IsKeyPressed(ImGuiKey_F))
            focusSearch_ = true;
        float searchW = 200.0f * s;
        ImGui::SetCursorPos(ImVec2(w - bw - pad - searchW - 8.0f * s, 10.0f * s));
        ImGui::SetNextItemWidth(searchW);
        if (focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
        const float searchIcon = 14.0f * s;
        ImVec2 fieldPos = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(searchIcon + 11.0f * s, ImGui::GetStyle().FramePadding.y));
        ImGui::InputTextWithHint("##search", "Search mods", searchBuf_.data(), searchBuf_.size());
        ImGui::PopStyleVar();
        if (ui::icons().tex(ui::Icon::Search))
            ui::icons().draw(dl, ui::Icon::Search,
                             ImVec2(fieldPos.x + 8.0f * s, fieldPos.y + (ImGui::GetFrameHeight() - searchIcon) * 0.5f),
                             searchIcon, colTextDim);
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
            searchBuf_.fill('\0');
    }

    installCount_.to(static_cast<float>(mods_.size()));
    int shown = static_cast<int>(installCount_.update(ImGui::GetIO().DeltaTime) + 0.5f);
    int enabledN = 0; for (const auto& md : mods_) if (md.enabled) ++enabledN;
    int matchN = 0; for (int i = 0; i < static_cast<int>(mods_.size()); ++i) if (rowShown(i)) ++matchN;
    std::string sub = filtered
        ? std::format("showing {} of {}, {} enabled", matchN, mods_.size(), enabledN)
        : std::format("{} installed, {} enabled, profile: {}", shown, enabledN,
                      store_ ? store_->activeName() : std::string());
    textSnapped(dl, ImVec2(c0.x + pad, c0.y + 40.0f * s), colTextDim, sub.c_str());

    const float headerH = 22.0f * s;
    const float listTop = 86.0f * s, rowH = 28.0f * s, rowGap = 4.0f * s, pitch = rowH + rowGap;

    const float colFolderW = 92.0f * s, colLoadW = 74.0f * s, colTypeW = 58.0f * s, colVerW = 50.0f * s;
    const float nameX = 78.0f * s;
    auto colX = [&](float contentW) {
        struct Cols { float ver, type, load, folder, nameMax; } c;
        c.folder = contentW - colFolderW;
        c.load   = c.folder - colLoadW;
        c.type   = c.load - colTypeW;
        c.ver    = c.type - colVerW;
        c.nameMax = c.ver - 8.0f * s;
        return c;
    };

    std::unordered_map<int, std::string> subdirOf;
    std::unordered_map<int, int> pakRank;
    int pakSeq = 0;
    if (store_)
        for (const auto& pm : store_->mods())
            subdirOf[pm.id] = pm.subdir;
    for (const auto& md : mods_)
        if (md.type == core::ModKind::Pak && md.enabled) pakRank[md.modId] = ++pakSeq;

    if (!game_.ue4ssInstalled()) {
        const Ue4ssStage stage = ue4ssStage_.load();
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + listTop + 6.0f * s), colWarn,
                    "UE4SS isn't installed yet.");
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + listTop + 26.0f * s), colTextDim,
                    "It's required to load mods. Install it to get started.");
        ImGui::SetCursorPos(ImVec2(pad, listTop + 54.0f * s));
        if (ue4ssBusy(stage)) {
            const char* phase = stage == Ue4ssStage::Querying ? "Contacting GitHub..."
                              : stage == Ue4ssStage::Downloading ? "Downloading UE4SS..."
                              : "Extracting...";
            ui::progressBar("##ue4ssdl", ue4ssProgress_.load(), ImVec2(220.0f * s, ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::TextColored(toVec(colTextDim), "%s", phase);
        } else if (ui::primaryButton("Install UE4SS", ImVec2(150.0f * s, 30.0f * s))) {
            installUe4ss();
        }
        ImGui::SetCursorPos(ImVec2(0.0f, listTop + 100.0f * s));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        return;
    }

    if (mods_.empty()) {
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + listTop + 6.0f * s), colTextDim, "No mods installed.");

        float dzX = c0.x + pad, dzY = c0.y + listTop + 34.0f * s;
        float dzW = w - 2.0f * pad, dzH = 92.0f * s;
        ImU32 dzCol = ui::lerpColor(colBorder, colAccent, dropAnim_.value);
        dl->AddRect(ImVec2(dzX, dzY), ImVec2(dzX + dzW, dzY + dzH), dzCol, 4.0f * s, 0, 1.5f);
        const char* hint = "Drag a mod folder or archive here, or click Add mods";
        ImVec2 hs = ImGui::CalcTextSize(hint);
        textSnapped(dl, ImVec2(dzX + (dzW - hs.x) * 0.5f, dzY + (dzH - lineH) * 0.5f), colTextDim, hint);

        ImGui::SetCursorPos(ImVec2(0.0f, listTop + 34.0f * s + dzH + 12.0f * s));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        return;
    }

    std::vector<int> visible;
    visible.reserve(mods_.size());
    for (int i = 0; i < static_cast<int>(mods_.size()); ++i)
        if (rowShown(i))
            visible.push_back(i);

    {
        ImGui::SetCursorPos(ImVec2(pad, 58.0f * s));
        if (ui::ghostButton("Enable all", ImVec2(90.0f * s, 22.0f * s)))
            for (int idx : visible) setModEnabled(idx, true);
        ImGui::SameLine(0.0f, 6.0f * s);
        if (ui::ghostButton("Disable all", ImVec2(90.0f * s, 22.0f * s)))
            for (int idx : visible) setModEnabled(idx, false);

        ImGui::SameLine(0.0f, 16.0f * s);
        auto seg = [&](const char* label, int val) {
            const bool active = typeFilter_ == val;
            const ImVec2 sz(0.0f, 22.0f * s);
            if (active ? ui::primaryButton(label, sz) : ui::ghostButton(label, sz))
                typeFilter_ = active ? 0 : val;
        };
        seg("All", 0);   ImGui::SameLine(0.0f, 4.0f * s);
        seg("PAK", 1);   ImGui::SameLine(0.0f, 4.0f * s);
        seg("UE4SS", 2);
    }

    bool typing = ImGui::GetIO().WantTextInput;
    if (!settingsOpen_ && !shareModalOpen_ && !typing) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !mods_.empty())
            selected_ = selected_ < 0 ? 0 : std::min(selected_ + 1, static_cast<int>(mods_.size()) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !mods_.empty())
            selected_ = selected_ <= 0 ? 0 : selected_ - 1;
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && selected_ >= 0 && selected_ < static_cast<int>(mods_.size()))
            setModEnabled(selected_, !mods_[selected_].enabled);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selected_ >= 0 && selected_ < static_cast<int>(mods_.size())) {
            pendingUninstall_ = selected_;
            requestUninstallConfirm_ = true;
        }
    }

    {
        ImGui::PushFont(fonts().label);
        const float labelH = ImGui::GetTextLineHeight();
        const float hx = c0.x + pad, hy = c0.y + listTop + (headerH - labelH) * 0.5f;
        const float hw = w - 2.0f * pad;
        auto hc = colX(hw);
        textSnapped(dl, ImVec2(hx + nameX, hy), colTextDim, "NAME");
        textSnapped(dl, ImVec2(hx + hc.ver, hy), colTextDim, "VER");
        textSnapped(dl, ImVec2(hx + hc.type, hy), colTextDim, "TYPE");
        textSnapped(dl, ImVec2(hx + hc.load, hy), colTextDim, "LOAD");
        textSnapped(dl, ImVec2(hx + hc.folder, hy), colTextDim, "FOLDER");
        ImGui::PopFont();
        dl->AddLine(ImVec2(c0.x + pad, c0.y + listTop + headerH - 1.0f),
                    ImVec2(c0.x + pad + hw, c0.y + listTop + headerH - 1.0f), colBorder);
    }

    ImGui::SetCursorPos(ImVec2(0.0f, listTop + headerH));
    ImGui::BeginChild("##rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    ImDrawList* rdl = ImGui::GetWindowDrawList();
    ImVec2 rc0 = ImGui::GetWindowPos();
    float rw = ImGui::GetWindowWidth();
    float scrollY = ImGui::GetScrollY();
    float rowW = rw - 2.0f * pad;

    const int n = static_cast<int>(mods_.size());
    const float oy = rc0.y - scrollY;   // screen y of content slot 0

    for (const auto& m : mods_)
        if (!knownRows_.contains(m.modId))
            seedRowReveal(std::to_string(m.modId).c_str(), 0.0f);

    if (!settingsOpen_ && !shareModalOpen_ && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))
        selected_ = -1;

    const bool dragging = dragIndex_ >= 0 && dragIndex_ < n;
    if (dragging) ui::markAnimActive();

    // Floating row position + landing slot while dragging.
    float floatY = 0.0f;
    int tgt = dragIndex_;
    if (dragging) {
        float maxY = oy + (n - 1) * pitch;
        floatY = ImGui::GetMousePos().y - dragGrabOffsetY_;
        if (floatY < oy) floatY = oy;
        if (floatY > maxY) floatY = maxY;
        tgt = static_cast<int>((floatY - oy) / pitch + 0.5f);
        if (tgt < 0) tgt = 0;
        if (tgt > n - 1) tgt = n - 1;
    }

    auto slotOf = [&](int i) -> int {
        if (!dragging || i == dragIndex_) return i;
        int p = i > dragIndex_ ? i - 1 : i;
        return p >= tgt ? p + 1 : p;
    };

    if (dragging) {
        float gy = oy + tgt * pitch;
        rdl->AddRect(ImVec2(rc0.x + pad, gy), ImVec2(rc0.x + pad + rowW, gy + rowH),
                     colAccentDim, 5.0f * s, 0, 1.5f);
    }

    // Pass 1: non-dragged rows.
    const bool filtering = filtered;
    int vis = 0;
    for (int i = 0; i < n; ++i) {
        if (filtering && !rowShown(i)) continue;
        const std::string& rowKey = mods_[i].rowKey;
        ImGuiID yId = ImGui::GetID(rowKey.c_str());
        float reveal = rowReveal(rowKey.c_str(), true, 16.0f);
        float targetY = filtering ? (vis * pitch) : (slotOf(i) * pitch);
        ++vis;
        if (dragging && i == dragIndex_) { animSet(yId, floatY - oy); continue; }

        float cy = springTo(yId, targetY, 22.0f, 0.72f);
        float ry = oy + cy;
        float lry = cy;   // window-local row top (vs. ry, the screen-space top)
        ImVec2 p(rc0.x + pad, ry);
        bool isSel = (selected_ == i);

        ImGui::PushID(i);
        if (reveal < 0.999f)
            rdl->PushClipRect(ImVec2(rc0.x, ry), ImVec2(rc0.x + rw, ry + rowH * reveal), true);
        bool hov = !dragging && !settingsOpen_ && !shareModalOpen_ && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + rowW, p.y + rowH));
        float hv = animTo(ImGui::GetID("rowh"), hov ? 1.0f : 0.0f, 16.0f);
        float indent = hv * 4.0f * s;

        const ImU32 tcol = typeColor(mods_[i].type);
        const float rowR = 5.0f * s;
        ImVec2 pb(p.x + rowW, p.y + rowH);
        if (isSel) {
            ImVec4 gv = toVec(colSelect); gv.w = 0.16f;
            rdl->AddRectFilled(ImVec2(p.x - 3.0f * s, p.y - 3.0f * s), ImVec2(pb.x + 3.0f * s, pb.y + 3.0f * s),
                               ImGui::ColorConvertFloat4ToU32(gv), rowR + 3.0f * s);
        }
        ImU32 base = isSel ? lerpColor(colSurface2, colSelect, 0.18f)
                           : lerpColor(colSurface, colSurface2, hv * 0.75f);
        rdl->AddRectFilled(p, pb, base, rowR);
        if (isSel) {
            rdl->AddRectFilled(p, ImVec2(p.x + 3.0f * s, p.y + rowH), colSelect, rowR, ImDrawFlags_RoundCornersLeft);
            rdl->AddRect(p, pb, lerpColor(colSelect, colSelectHi, 0.4f), rowR, 0, 1.5f);
        }

        ImU32 gripCol = isSel ? colTextHi : lerpColor(colTextDim, colText, hv);
        if (ImTextureID grip = ui::icons().tex(ui::Icon::Grip)) {
            float gs = 16.0f * s;
            ui::icons().draw(rdl, ui::Icon::Grip, ImVec2(p.x + 6.0f * s + indent, ry + (rowH - gs) * 0.5f), gs, gripCol);
        } else {
            for (int k = 0; k < 3; ++k) {
                float gy = px(ry + rowH * 0.5f - 3.0f * s + k * 3.0f * s);
                rdl->AddLine(ImVec2(px(p.x + 8.0f * s + indent), gy), ImVec2(px(p.x + 17.0f * s + indent), gy),
                             gripCol, 1.0f);
            }
        }
        ImGui::SetCursorPos(ImVec2(pad + 4.0f * s + indent, lry + (rowH - 20.0f * s) * 0.5f));
        ImGui::InvisibleButton("grip", ImVec2(18.0f * s, 20.0f * s));
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && dragIndex_ < 0 && !filtering) {
            dragIndex_ = i;
            dragGrabOffsetY_ = ImGui::GetMousePos().y - ry;
        }

        if (togglePending_.contains(mods_[i].modId)) {
            const float cx = p.x + 28.0f * s + indent + frameH * 0.95f;
            ui::spinner(ImVec2(cx, ry + rowH * 0.5f), frameH * 0.32f, 2.0f * s,
                        isSel ? colTextHi : colAccent);
        } else {
            ImGui::SetCursorPos(ImVec2(pad + 28.0f * s + indent, lry + (rowH - frameH) * 0.5f));
            if (ui::toggleSwitch("tog", &mods_[i].enabled)) {
                mods_[i].enabled = !mods_[i].enabled;
                setModEnabled(i, !mods_[i].enabled);
            }
        }

        ImGui::SetCursorPos(ImVec2(pad + nameX, lry));
        if (ImGui::InvisibleButton("sel", ImVec2(rowW - nameX, rowH)))
            selected_ = i;

        const ModEntry& e = mods_[i];
        const auto c = colX(rowW);
        const float ty = ry + (rowH - lineH) * 0.5f;
        const ImU32 nameCol = isSel ? colTextHi : (e.enabled ? colText : colTextDim);
        const ImU32 secCol  = isSel ? colTextHi : colText;
        const ImU32 dimCol  = isSel ? colTextHi : colTextDim;

        rdl->PushClipRect(ImVec2(p.x + nameX, ry), ImVec2(p.x + c.nameMax, ry + rowH), true);
        textSnapped(rdl, ImVec2(p.x + nameX + indent, ty), nameCol, e.displayName.c_str());
        rdl->PopClipRect();

        if (e.version.empty()) textSnapped(rdl, ImVec2(p.x + c.ver, ty), dimCol, "—");
        else                   textSnapped(rdl, ImVec2(p.x + c.ver, ty), secCol, e.version.c_str());

        const char* tlabel = typeLabel(e.type);
        ui::pill(ImVec2(p.x + c.type, ty - 2.0f * s), tlabel, tcol);

        if (e.type == core::ModKind::Pak && e.enabled) {
            auto it = pakRank.find(e.modId);
            std::string ld = std::format("#{:03}", it != pakRank.end() ? it->second : 0);
            textSnapped(rdl, ImVec2(p.x + c.load, ty), secCol, ld.c_str());
        } else {
            textSnapped(rdl, ImVec2(p.x + c.load, ty), dimCol, "—");
        }

        const char* folder = e.type == core::ModKind::Ue4ss ? "Mods"
                           : (subdirOf.count(e.modId) ? subdirOf[e.modId].c_str() : core::kLogicMods);
        textSnapped(rdl, ImVec2(p.x + c.folder, ty), dimCol, folder);
        if (reveal < 0.999f) rdl->PopClipRect();
        ImGui::PopID();
    }

    // Pass 2: dragged row, lifted on top.
    if (dragging) {
        float lift = animTo(ImGui::GetID("draglift"), 1.0f, 18.0f);
        float ex = lift * 2.0f * s;
        ImVec2 a(rc0.x + pad - ex, floatY - ex);
        ImVec2 b(rc0.x + pad + rowW + ex, floatY + rowH + ex);
        for (int sIdx = 3; sIdx >= 1; --sIdx) {
            float o = sIdx * 2.5f * s * lift;
            rdl->AddRectFilled(ImVec2(a.x, a.y + o), ImVec2(b.x, b.y + o),
                               IM_COL32(0, 0, 0, 26), 3.0f * s);
        }
        const ModEntry& dm = mods_[dragIndex_];
        const ImU32 dtcol = typeColor(dm.type);
        const float dragR = 5.0f * s;
        rdl->AddRectFilled(a, b, lerpColor(colSurface2, colAccent, 0.16f), dragR);
        rdl->AddRectFilled(a, ImVec2(a.x + 3.0f * s, b.y), colAccent, dragR, ImDrawFlags_RoundCornersLeft);
        rdl->AddRect(a, b, lerpColor(colAccent, colAccentHi, 0.4f), dragR, 0, 1.5f);
        if (ImTextureID grip = ui::icons().tex(ui::Icon::Grip)) {
            float gs = 16.0f * s;
            ui::icons().draw(rdl, ui::Icon::Grip, ImVec2(rc0.x + pad + 6.0f * s, floatY + (rowH - gs) * 0.5f), gs, colTextHi);
        } else {
            for (int k = 0; k < 3; ++k) {
                float gy = px(floatY + rowH * 0.5f - 3.0f * s + k * 3.0f * s);
                rdl->AddLine(ImVec2(px(rc0.x + pad + 8.0f * s), gy), ImVec2(px(rc0.x + pad + 17.0f * s), gy),
                             colTextHi, 1.0f);
            }
        }
        const auto dc = colX(rowW);
        textSnapped(rdl, ImVec2(rc0.x + pad + nameX, floatY + (rowH - lineH) * 0.5f), colTextHi, dm.displayName.c_str());
        const char* dlabel = typeLabel(dm.type);
        ui::pill(ImVec2(rc0.x + pad + dc.type, floatY + (rowH - lineH) * 0.5f - 2.0f * s), dlabel, dtcol);

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (tgt != dragIndex_) {
                std::string selName = (selected_ >= 0 && selected_ < n) ? mods_[selected_].name : std::string();
                moveMod(dragIndex_, tgt, true);
                if (!selName.empty())
                    for (int k = 0; k < static_cast<int>(mods_.size()); ++k)
                        if (mods_[k].name == selName) { selected_ = k; break; }
            }
            dragIndex_ = -1;
            animSet(ImGui::GetID("draglift"), 0.0f);
        }
    }

    const float contentH = vis * pitch;

    ImGui::SetCursorPos(ImVec2(pad, contentH + 12.0f * s));
    if (store_ && ImGui::CollapsingHeader("Built-in UE4SS mods")) {
        for (const auto& b : store_->builtins()) {
            ImGui::SetCursorPosX(pad + 8.0f * s);
            bool on = b.enabled;
            ImGui::PushID(b.name.c_str());
            if (ui::toggleSwitch("t", &on))
                store_->setBuiltinEnabled(b.name, on);
            ImGui::SameLine(0.0f, 8.0f * s);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(toVec(on ? colText : colTextDim), "%s", b.name.c_str());
            ImGui::PopID();
        }
    }

    const float tailY = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(pad, tailY + 4.0f * s));
    if (ImGui::InvisibleButton("listempty", ImVec2(rowW, 40.0f * s)) && !dragging)
        selected_ = -1;

    ImGui::SetCursorPos(ImVec2(0.0f, tailY + 48.0f * s));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    knownRows_.clear();
    for (const auto& m : mods_) knownRows_.insert(m.modId);

    ImGui::EndChild();
}

void App::renderDetails() {
    float s = uiScale();
    ImVec2 c0 = ImGui::GetWindowPos();
    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lineH = ImGui::GetTextLineHeight();
    const float pad = 12.0f * s;

    if (config_.themeMode == "subnautica")
        glassPanel(dl, c0, ImVec2(c0.x + w, c0.y + h), s);
    dl->AddRectFilled(ImVec2(c0.x, c0.y), ImVec2(c0.x + 1.0f, c0.y + h), colBorder);

    if (shareService_.busy() && !shareService_.isHost()) {
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + 16.0f * s), colTextDim, "Receiving profile...");
        return;
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(mods_.size())) {
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + 16.0f * s), colTextDim, "Select a mod to see details.");
        ImGui::SetCursorPos(ImVec2(0.0f, 44.0f * s));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        return;
    }

    ModEntry& m = mods_[selected_];
    const core::ProfileMod* pm = nullptr;
    if (store_)
        for (const auto& x : store_->mods())
            if (x.id == m.modId) { pm = &x; break; }

    int rank = 0, pakTotal = 0;
    for (int i = 0; i < static_cast<int>(mods_.size()); ++i)
        if (mods_[i].type == core::ModKind::Pak && mods_[i].enabled) { ++pakTotal; if (i == selected_) rank = pakTotal; }

    auto section = [&](const char* label) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f * s));
        float y = ImGui::GetCursorPosY();
        std::string up(label);
        for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ImGui::PushFont(fonts().label);
        float lh = ImGui::GetTextLineHeight();
        float tw = ImGui::CalcTextSize(up.c_str()).x;
        textSnapped(dl, ImVec2(c0.x + pad, c0.y + y), colTextDim, up.c_str());
        ImGui::PopFont();
        float ry = c0.y + y + lh * 0.5f;
        dl->AddLine(ImVec2(c0.x + pad + tw + 8.0f * s, ry), ImVec2(c0.x + w - pad, ry), colBorder);
        ImGui::SetCursorPos(ImVec2(pad, y + lh + 6.0f * s));
    };
    auto kv = [&](const char* key, const std::string& val) {
        float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(pad, y));
        ImGui::TextColored(toVec(colTextDim), "%s", key);
        ImGui::PushFont(fonts().semibold);
        ImVec2 ts = ImGui::CalcTextSize(val.c_str());
        ImGui::SetCursorPos(ImVec2(w - pad - ts.x, y));
        ImGui::TextColored(toVec(colTextHi), "%s", val.c_str());
        ImGui::PopFont();
        float ly = c0.y + y + ImGui::GetTextLineHeight() + 6.0f * s;
        dl->AddLine(ImVec2(c0.x + pad, ly), ImVec2(c0.x + w - pad, ly), colBorder);
        ImGui::SetCursorPos(ImVec2(pad, y + ImGui::GetTextLineHeight() + 12.0f * s));
    };
    auto humanSize = [](std::uintmax_t b) {
        const char* u[] = { "B", "KB", "MB", "GB" };
        double v = static_cast<double>(b); int i = 0;
        while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
        return i ? std::format("{:.1f} {}", v, u[i]) : std::format("{} B", b);
    };

    ImGui::SetCursorPos(ImVec2(pad, 16.0f * s));
    ImGui::PushFont(fonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, toVec(colTextHi));
    ImGui::PushTextWrapPos(w - pad);
    ImGui::TextUnformatted(m.displayName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    std::string sub = m.enabled ? "Enabled" : "Disabled";
    if (m.type == core::ModKind::Pak && m.enabled) sub += std::format(", load #{:03}", rank);
    ImGui::SetCursorPosX(pad);
    ImGui::TextColored(toVec(colTextDim), "%s", sub.c_str());
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    ImVec2 wp = ImGui::GetWindowPos();
    ui::pill(ImVec2(wp.x + pad, wp.y + ImGui::GetCursorPosY()), typeLabel(m.type), typeColor(m.type));
    ImGui::Dummy(ImVec2(0.0f, lineH + 8.0f * s));

    section("Details");
    kv("Version", m.version.empty() ? "—" : m.version);
    if (m.nexusId) kv("Nexus mod ID", std::to_string(*m.nexusId));

    if (m.type == core::ModKind::Pak) {
        std::string subdir = pm ? pm->subdir : std::string(core::kLogicMods);
        float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(pad, y + 3.0f * s));
        ImGui::TextColored(toVec(colTextDim), "Paks folder");
        int cur = subdir == core::kContentMods ? 1 : 0;
        const char* folders[] = { "LogicMods", "~mods" };
        ImGui::SetCursorPos(ImVec2(w * 0.42f, y));
        ImGui::SetNextItemWidth(w - pad - w * 0.42f);
        if (ImGui::Combo("##paksub", &cur, folders, 2) && store_)
            store_->setSubdir(m.modId, cur == 1 ? core::kContentMods : core::kLogicMods);
        ImGui::SetCursorPosX(pad);
        ImGui::Dummy(ImVec2(0.0f, 5.0f * s));
    }

    section("Load order");
    {
        float y = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(pad, y + 2.0f * s));
        ImGui::TextColored(toVec(colText), "%s",
            (m.type == core::ModKind::Pak && m.enabled) ? std::format("#{:03} of {:03}", rank, pakTotal).c_str() : "—");
        const ImVec2 btn(30.0f * s, 22.0f * s);
        ImTextureID upIco = ui::icons().tex(ui::Icon::ChevronUp);
        ImTextureID dnIco = ui::icons().tex(ui::Icon::ChevronDown);
        ImGui::SetCursorPos(ImVec2(w - pad - 64.0f * s, y));
        bool upClick = upIco ? ui::iconButton("##lo_up", upIco, btn, colText)
                             : ui::ghostButton("Up", btn);
        if (upClick && selected_ > 0) {
            moveMod(selected_, selected_ - 1, true);
            --selected_;
        }
        ImGui::SameLine(0.0f, 4.0f * s);
        bool dnClick = dnIco ? ui::iconButton("##lo_dn", dnIco, btn, colText)
                             : ui::ghostButton("Dn", btn);
        if (dnClick && selected_ < static_cast<int>(mods_.size()) - 1) {
            moveMod(selected_, selected_ + 1, true);
            ++selected_;
        }
        ImGui::SetCursorPosX(pad);
        ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    }

    if (pm) {
        const core::GamePaths& paths = game_.paths();
        std::vector<std::pair<std::string, std::optional<std::uintmax_t>>> files;
        if (pm->kind == core::ModKind::Pak) {
            const std::filesystem::path dir = pm->subdir == core::kContentMods ? paths.pakMods : paths.logicMods;
            for (const auto& ext : pm->exts) {
                std::string fname = m.enabled ? std::format("{:03}_{}{}", rank, pm->stem, ext) : (pm->stem + ext);
                std::error_code ec;
                std::filesystem::path full = dir / core::pathFromUtf8(fname);
                std::optional<std::uintmax_t> sz;
                if (m.enabled && std::filesystem::exists(full, ec)) sz = std::filesystem::file_size(full, ec);
                files.push_back({ fname, sz });
            }
        } else {
            const std::filesystem::path dir = paths.ue4ssMods / core::pathFromUtf8(pm->name);
            for (const auto& rel : pm->files) {
                std::error_code ec;
                std::filesystem::path full = dir / core::pathFromUtf8(rel);
                std::optional<std::uintmax_t> sz;
                if (m.enabled && std::filesystem::exists(full, ec)) sz = std::filesystem::file_size(full, ec);
                files.push_back({ rel, sz });
            }
        }
        section(std::format("Files ({})", files.size()).c_str());
        const int cap = 6;
        const float chipH = lineH + 12.0f * s;
        const float iconX = 11.0f * s;          // left inset reserved for the file glyph (Phase 2)
        const float textX = 32.0f * s;          // name starts past the glyph
        for (int i = 0; i < static_cast<int>(files.size()) && i < cap; ++i) {
            float y = ImGui::GetCursorPosY();
            ImVec2 a(c0.x + pad, c0.y + y), b(c0.x + w - pad, a.y + chipH);
            dl->AddRectFilled(a, b, panelBg(colSurface2), 7.0f * s);
            float cy = a.y + (chipH - lineH) * 0.5f;

            const ui::Icon fic = pm->kind == core::ModKind::Pak ? ui::Icon::Package : ui::Icon::File;
            const float fis = 15.0f * s;
            if (ui::icons().tex(fic))
                ui::icons().draw(dl, fic, ImVec2(a.x + 9.0f * s, a.y + (chipH - fis) * 0.5f), fis, colTextDim);
            else
                dl->AddCircleFilled(ImVec2(a.x + iconX, a.y + chipH * 0.5f), 3.5f * s, colTextDim);

            std::string szText = files[i].second ? humanSize(*files[i].second) : std::string();
            float szW = szText.empty() ? 0.0f : ImGui::CalcTextSize(szText.c_str()).x;
            dl->PushClipRect(ImVec2(a.x + textX, a.y), ImVec2(b.x - szW - 14.0f * s, b.y), true);
            textSnapped(dl, ImVec2(a.x + textX, cy), colText, files[i].first.c_str());
            dl->PopClipRect();
            if (!szText.empty())
                textSnapped(dl, ImVec2(b.x - 10.0f * s - szW, cy), colTextDim, szText.c_str());

            ImGui::SetCursorPos(ImVec2(pad, y + chipH + 5.0f * s));
        }
        if (static_cast<int>(files.size()) > cap) {
            ImGui::SetCursorPosX(pad);
            ImGui::TextColored(toVec(colTextDim), "+%d more", static_cast<int>(files.size()) - cap);
        }
    }

    section("Raw filename");
    {
        float y = ImGui::GetCursorPosY();
        const float boxH = lineH + 12.0f * s;
        ImVec2 a(c0.x + pad, c0.y + y), b(c0.x + w - pad, a.y + boxH);
        dl->AddRectFilled(a, b, panelBg(colSurface2), 7.0f * s);
        float cy = a.y + (boxH - lineH) * 0.5f;
        dl->PushClipRect(ImVec2(a.x + 10.0f * s, a.y), ImVec2(b.x - 10.0f * s, b.y), true);
        textSnapped(dl, ImVec2(a.x + 10.0f * s, cy), colTextDim, m.name.c_str());
        dl->PopClipRect();
        ImGui::SetCursorPos(ImVec2(pad, y + boxH));
    }

    ImGui::SetCursorPos(ImVec2(pad, h - 44.0f * s));
    float halfW = (w - 2.0f * pad - 8.0f * s) * 0.5f;
    if (ui::ghostButton("Open folder", ui::icons().tex(ui::Icon::FolderOpen), ImVec2(halfW, 30.0f * s)) && pm) {
        const core::GamePaths& paths = game_.paths();
        std::filesystem::path dir = pm->kind == core::ModKind::Pak
            ? (pm->subdir == core::kContentMods ? paths.pakMods : paths.logicMods)
            : (paths.ue4ssMods / core::pathFromUtf8(pm->name));
        platform::openInExplorer(core::narrow(dir.wstring()));
    }
    ImGui::SameLine(0.0f, 8.0f * s);
    if (ui::dangerButton("Uninstall", ImVec2(halfW, 30.0f * s))) {
        pendingUninstall_ = selected_;
        ImGui::OpenPopup("Confirm Uninstall");
    }
    if (requestUninstallConfirm_) {
        requestUninstallConfirm_ = false;
        ImGui::OpenPopup("Confirm Uninstall");
    }
    if (ImGui::BeginPopupModal("Confirm Uninstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pendingUninstall_ >= 0 && pendingUninstall_ < static_cast<int>(mods_.size())) {
            ImGui::Text("Uninstall '%s'?", mods_[pendingUninstall_].name.c_str());
            ImGui::Text("This permanently removes the mod's files.");
            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::Button("Uninstall", ImVec2(120, 0))) {
                const int victimIdx = pendingUninstall_;
                const std::string vname = mods_[victimIdx].name;
                const int vmodId = mods_[victimIdx].modId;
                if (store_) {
                    auto stash = std::make_shared<core::ProfileStore::Stashed>();
                    if (store_->stash(vmodId, *stash)) {
                        mods_.erase(mods_.begin() + victimIdx);
                        selected_ = mods_.empty() ? -1 : std::min(selected_, static_cast<int>(mods_.size()) - 1);
                        history_.push({ std::format("Uninstall {}", vname),
                            [this, stash]{ if (store_) { store_->restore(*stash); syncModsFromStore(); } },
                            [this, vname, stash]{
                                if (!store_) return;
                                for (auto it = mods_.begin(); it != mods_.end(); ++it)
                                    if (it->name == vname) {
                                        (void)store_->stash(it->modId, *stash);
                                        mods_.erase(it);
                                        break;
                                    }
                            } });
                    }
                }
                pendingUninstall_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                pendingUninstall_ = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SetCursorPos(ImVec2(0.0f, h - 8.0f * s));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void App::renderSettingsView(float a) {
    float s = uiScale();

    ImVec2 full = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##settings", full, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* odl = ImGui::GetWindowDrawList();
    if (window_)
        window_->setBackdropBlur(odl, a);
    {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec4 dv = toVec(colBg);
        int da = static_cast<int>(a * 0.34f * 255.0f);
        dv.w = (da < 1 ? 1 : da) / 255.0f;
        odl->AddRectFilled(wp, ImVec2(wp.x + full.x, wp.y + full.y), ImGui::ColorConvertFloat4ToU32(dv));
    }

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    if (ImGui::InvisibleButton("##settingsScrim", full))
        settingsOpen_ = false;

    float w = ImGui::GetWindowWidth();
    float h = ImGui::GetWindowHeight();
    float colW = std::min(620.0f * s, w - 32.0f * s);
    float x = std::max(16.0f * s, (w - colW) * 0.5f);
    float e = ui::easeOutCubic(a);
    float slide = (1.0f - e) * 16.0f * s;

    auto fieldLabel = [&](const char* t) {
        std::string up(t);
        for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        ImGui::PushFont(fonts().label);
        ImGui::TextColored(toVec(colTextDim), "%s", up.c_str());
        ImGui::PopFont();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);

    float top = 22.0f * s + slide;
    ImGui::SetCursorPos(ImVec2(x, top));
    if (ui::ghostButton("Back", ui::icons().tex(ui::Icon::ChevronLeft), ImVec2(90.0f * s, 28.0f * s)))
        settingsOpen_ = false;
    {
        ImGui::PushFont(fonts().title);
        float th = ImGui::GetTextLineHeight();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec4 tc = toVec(colTextHi); tc.w *= e;
        textSnapped(ImGui::GetWindowDrawList(),
                    ImVec2(wp.x + x + 102.0f * s, wp.y + top + (28.0f * s - th) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(tc), "Settings");
        ImGui::PopFont();
    }

    float cardTop = top + 38.0f * s;
    float cardH = std::max(120.0f * s, h - cardTop - 22.0f * s);
    ImGui::SetCursorPos(ImVec2(x, cardTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec(panelBg(colSurface)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * s, 16.0f * s));
    ImGui::BeginChild("##settingsCol", ImVec2(colW, cardH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

    if (ImGui::BeginTabBar("##settingsTabs")) {
        if (ImGui::BeginTabItem("General")) {
            fieldLabel("Game folder");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##game", "path to Subnautica 2", gamePathBuffer_.data(), gamePathBuffer_.size())) {
                saveConfig();
                resolveGame();
            }
            if (ui::ghostButton("Detect via Steam", ImVec2(150.0f * s, 28.0f * s)))
                detectGame();
            ImGui::SameLine();
            if (ui::ghostButton("Browse...", ImVec2(100.0f * s, 28.0f * s)))
                browseGame();
            if (!statusMessage_.empty())
                ImGui::TextColored(toVec(colTextDim), "%s", statusMessage_.c_str());

            ImGui::Dummy(ImVec2(0, 8));
            fieldLabel("Updates");
            core::UpdateCheckResult update;
            { std::lock_guard<std::mutex> lk(updateMutex_); update = updateResult_; }
            ImGui::TextColored(toVec(colTextDim), "Current version: %s", core::kAppVersion);
            if (updateBusy_.load()) {
                ImGui::TextColored(toVec(colTextDim), "Checking GitHub...");
            } else if (update.ok && update.updateAvailable) {
                ImGui::TextColored(toVec(colAccent), "Update available: %s", update.latestVersion.c_str());
            } else if (update.ok) {
                ImGui::TextColored(toVec(colTextDim), "Latest release: %s", update.latestVersion.c_str());
            } else if (!update.message.empty()) {
                ImGui::TextColored(toVec(colWarn), "%s", update.message.c_str());
            }

            if (ImGui::Checkbox("Include prereleases", &config_.includePrereleases)) {
                saveConfig();
                checkForUpdates(false);
            }

            ImGui::BeginDisabled(updateBusy_.load());
            if (ui::ghostButton("Check for updates", ImVec2(150.0f * s, 28.0f * s)))
                checkForUpdates(true);
            ImGui::EndDisabled();
            if (update.ok && update.updateAvailable) {
                ImGui::SameLine();
                if (ui::primaryButton("Open release", ImVec2(130.0f * s, 28.0f * s)))
                    platform::openUrl(update.releaseUrl.empty() ? core::kAppReleasesUrl : update.releaseUrl);
            }

            ImGui::Dummy(ImVec2(0, 8));
            fieldLabel("UE4SS");
            const Ue4ssStage stage = ue4ssStage_.load();
            if (ue4ssBusy(stage)) {
                const char* phase = stage == Ue4ssStage::Querying ? "Contacting GitHub..."
                                  : stage == Ue4ssStage::Downloading ? "Downloading..."
                                  : "Extracting...";
                ui::progressBar("##ue4ssdl2", ue4ssProgress_.load(),
                                ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
                ImGui::TextColored(toVec(colTextDim), "%s", phase);
            } else if (game_.ue4ssInstalled()) {
                ImGui::TextColored(toVec(colAccent), "Installed");
            } else {
                ImGui::TextColored(toVec(colWarn), "Not installed");
                if (ui::primaryButton("Install UE4SS", ImVec2(150.0f * s, 28.0f * s)))
                    installUe4ss();
            }

            ImGui::Dummy(ImVec2(0, 8));
            fieldLabel("ReShade");
            const ReshadeStage rstage = reshadeInstallStage_.load();
            const bool reshadeOn = game_.reshadeInstalled();
            const float availW = ImGui::GetContentRegionAvail().x;
            if (reshadeStageBusy(rstage)) {
                const char* phase = rstage == ReshadeStage::Querying ? "Contacting reshade.me..."
                                  : rstage == ReshadeStage::Downloading ? "Downloading..."
                                  : rstage == ReshadeStage::Installing ? "Running installer..."
                                  : "Verifying...";
                ui::progressBar("##reshadedl", reshadeInstallProgress_.load(),
                                ImVec2(availW, ImGui::GetFrameHeight()));
                ImGui::TextColored(toVec(colTextDim), "%s", phase);
            } else if (reshadeUninstallStage_.load() == ReshadeStage::Uninstalling) {
                ImGui::TextColored(toVec(colTextDim), "Uninstalling...");
            } else if (reshadeOn) {
                if (config_.reshadeVersion.empty())
                    ImGui::TextColored(toVec(colAccent), "Installed");
                else
                    ImGui::TextColored(toVec(colAccent), "Installed (%s)", config_.reshadeVersion.c_str());
                ImGui::SameLine();
                if (ui::ghostButton("Uninstall", ImVec2(110.0f * s, 24.0f * s)))
                    ImGui::OpenPopup("Uninstall ReShade?");

                // Shader packs.
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::TextColored(toVec(colTextDim), "Shader packs");
                if (reshadeShaderBusy_.load()) {
                    ui::progressBar("##reshadeshaders", reshadeShaderProgress_.load(),
                                    ImVec2(availW, ImGui::GetFrameHeight()));
                    ImGui::TextColored(toVec(colTextDim), "Working...");
                } else {
                    if (shaders_) {
                        std::string packToRemove;
                        std::string packToToggle;
                        bool toggleTo = false;
                        for (const auto& pk : shaders_->packs()) {
                            ImGui::PushID(pk.name.c_str());
                            bool en = pk.enabled;
                            if (ImGui::Checkbox("##en", &en)) { packToToggle = pk.name; toggleTo = en; }
                            ImGui::SameLine();
                            ImGui::TextColored(toVec(colText), "%s (%d effects)", pk.name.c_str(), pk.effectCount);
                            ImGui::SameLine(availW - 24.0f * s);
                            if (ImGui::SmallButton("x"))
                                packToRemove = pk.name;
                            ImGui::PopID();
                        }
                        if (shaders_->packs().empty())
                            ImGui::TextColored(toVec(colTextDim), "None installed. Presets need shaders to show.");
                        if (!packToToggle.empty())
                            shaders_->setEnabled(packToToggle, toggleTo);
                        if (!packToRemove.empty())
                            shaders_->uninstall(packToRemove);
                    }
                    static int branch = 0;
                    ImGui::SetNextItemWidth(110.0f * s);
                    const char* branches[] = { "slim", "latest" };
                    ImGui::Combo("##shaderbranch", &branch, branches, 2);
                    ImGui::SameLine();
                    if (ui::ghostButton("Install standard", ImVec2(140.0f * s, 24.0f * s)))
                        installStandardShaders(branch);
                    ImGui::SameLine();
                    if (ui::ghostButton("Import pack...", ImVec2(130.0f * s, 24.0f * s)))
                        importShaderPack();
                }

                // Presets (active profile).
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::TextColored(toVec(colTextDim), "Presets (%s)",
                                   store_ ? store_->activeName().c_str() : "");
                if (store_) {
                    int active = store_->activePresetId();
                    int presetToSelect = 0;
                    int presetToRemove = 0;
                    for (const auto& pr : store_->presets()) {
                        ImGui::PushID(pr.id);
                        bool sel = (pr.id == active);
                        if (ImGui::RadioButton("##sel", sel))
                            presetToSelect = pr.id;
                        ImGui::SameLine();
                        ImGui::TextColored(toVec(colText), "%s", pr.name.c_str());
                        ImGui::SameLine(availW - 24.0f * s);
                        if (ImGui::SmallButton("x"))
                            presetToRemove = pr.id;
                        ImGui::PopID();
                    }
                    if (store_->presets().empty())
                        ImGui::TextColored(toVec(colTextDim), "No presets imported.");
                    if (presetToSelect)
                        store_->setActivePreset(presetToSelect);
                    if (presetToRemove)
                        store_->uninstallPreset(presetToRemove);
                    if (ui::ghostButton("Import preset...", ImVec2(150.0f * s, 24.0f * s))) {
                        if (auto picked = platform::pickFile("Choose a ReShade preset", L"ReShade preset", L"*.ini"))
                            store_->installPreset(core::pathFromUtf8(*picked));
                    }
                }

                if (ImGui::BeginPopupModal("Uninstall ReShade?", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(toVec(colText),
                        "Remove ReShade, its shaders and managed presets from the game?");
                    ImGui::Dummy(ImVec2(0, 4));
                    if (ui::dangerButton("Uninstall", ImVec2(120.0f * s, 28.0f * s))) {
                        uninstallReshade();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ui::ghostButton("Cancel", ImVec2(100.0f * s, 28.0f * s)))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            } else {
                ImGui::TextColored(toVec(colWarn), "Not installed");
                if (ui::primaryButton("Install ReShade", ImVec2(150.0f * s, 28.0f * s)))
                    installReshade();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Appearance")) {
            int mode = config_.themeMode == "light" ? 2 : (config_.themeMode == "dark" ? 1 : 0);
            fieldLabel("Theme");
            if (ImGui::RadioButton("Subnautica", &mode, 0)) { config_.themeMode = "subnautica"; applyActivePalette(); reloadBackground(); saveConfig(); }
            ImGui::SameLine();
            if (ImGui::RadioButton("Minimal dark", &mode, 1)) { config_.themeMode = "dark"; applyActivePalette(); reloadBackground(); saveConfig(); }
            ImGui::SameLine();
            if (ImGui::RadioButton("Minimal light", &mode, 2)) { config_.themeMode = "light"; applyActivePalette(); reloadBackground(); saveConfig(); }

            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::Checkbox("Enable vsync", &config_.vsync))
                saveConfig();

            ImGui::Dummy(ImVec2(0, 6));
            ImGui::SeparatorText("Background");
            core::BackgroundConfig& bg = config_.background;
            if (ImGui::Checkbox("Enable background image", &bg.enabled)) {
                reloadBackground();
                saveConfig();
            }
            ImGui::BeginDisabled(!bg.enabled);
            {
                bool isDark = config_.themeMode != "light";
                std::string& imgPath = config_.activeBackgroundImage();
                std::string label = imgPath.empty()
                    ? std::format("Image ({}): built-in default", isDark ? "dark" : "light")
                    : std::format("Image ({}): {}", isDark ? "dark" : "light", imgPath);
                ImGui::TextColored(toVec(colTextDim), "%s", label.c_str());
                if (ui::ghostButton("Choose image...", ImVec2(150.0f * s, 28.0f * s))) {
                    auto picked = platform::pickFile("Choose background image",
                                                     L"Images", L"*.png;*.jpg;*.jpeg;*.webp");
                    if (picked) { imgPath = *picked; reloadBackground(); saveConfig(); }
                }
                ImGui::SameLine();
                if (ui::ghostButton("Use default", ImVec2(120.0f * s, 28.0f * s))) {
                    imgPath.clear();
                    reloadBackground();
                    saveConfig();
                }

                ImGui::Dummy(ImVec2(0, 4));
                ImGui::TextColored(toVec(colTextDim), "Blur");
                ImGui::SetNextItemWidth(180.0f * s);
                if (ImGui::SliderFloat("##bgblur", &bg.blur, 0.0f, 1.0f, "%.2f"))
                    background_.setBlur(bg.blur);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    saveConfig();

                ImGui::TextColored(toVec(colTextDim), "Darkening");
                ImGui::SetNextItemWidth(180.0f * s);
                if (ImGui::SliderFloat("##bgdim", &bg.dim, 0.0f, 1.0f, "%.2f"))
                    background_.setDim(bg.dim);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    saveConfig();

                ImGui::TextColored(toVec(colTextDim), "Panel opacity");
                ImGui::SetNextItemWidth(180.0f * s);
                ImGui::SliderFloat("##bgpanel", &bg.panelOpacity, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    saveConfig();

                ImGui::TextColored(toVec(colTextDim), "Drift (slow motion)");
                ImGui::SetNextItemWidth(180.0f * s);
                if (ImGui::SliderFloat("##bgdrift", &bg.driftAmount, 0.0f, 1.0f, "%.2f"))
                    background_.setDrift(bg.driftAmount, bg.driftSpeed);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    saveConfig();

                ImGui::BeginDisabled(bg.driftAmount <= 0.001f);
                ImGui::TextColored(toVec(colTextDim), "Drift speed");
                ImGui::SetNextItemWidth(180.0f * s);
                if (ImGui::SliderFloat("##bgdriftspeed", &bg.driftSpeed, 0.0f, 2.0f, "%.2f"))
                    background_.setDrift(bg.driftAmount, bg.driftSpeed);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    saveConfig();
                ImGui::EndDisabled();
            }
            ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(0, 4));
            fieldLabel("UI scale");
            ImGui::SetNextItemWidth(180.0f * s);
            ImGui::SliderFloat("##uiscale", &uiScaleSetting_, 0.8f, 1.5f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit() && uiScaleSetting_ != uiScaleCurrent_) {
                scaleAnimFrom_ = uiScaleCurrent_;
                scaleAnimTo_ = uiScaleSetting_;
                scaleAnimT_ = 0.0f;
                config_.uiScale = uiScaleSetting_;
                saveConfig();
            }

            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextColored(toVec(colTextDim), "Palette — %s", config_.themeMode.c_str());
            core::Palette& p = config_.activePalette();
            bool ch = false;
            auto grid = [&](const char* id, std::initializer_list<std::pair<const char*, std::uint32_t*>> items) {
                if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame)) {
                    for (const auto& it : items) {
                        ImGui::TableNextColumn();
                        ch |= colorRow(it.first, it.second);
                    }
                    ImGui::EndTable();
                }
            };
            ImGui::SeparatorText("Surfaces");
            grid("##gSurf", { {"Background", &p.bg}, {"Surface", &p.surface},
                              {"Surface raised", &p.surface2}, {"Surface high", &p.surface3},
                              {"Border", &p.border} });
            ImGui::SeparatorText("Accent");
            grid("##gAcc", { {"Accent", &p.accent}, {"Accent bright", &p.accentHi},
                             {"Accent dim", &p.accentDim}, {"Warning", &p.warn},
                             {"Select", &p.select}, {"Select bright", &p.selectHi} });
            ImGui::SeparatorText("Text");
            grid("##gText", { {"Text", &p.text}, {"Text bright", &p.textHi},
                              {"Text dim", &p.textDim}, {"Ink", &p.ink} });
            if (ch) { applyActivePalette(); saveConfig(); }

            ImGui::Dummy(ImVec2(0, 6));
            if (ui::ghostButton("Reset this theme", ImVec2(150.0f * s, 26.0f * s))) {
                p = config_.themeMode == "light" ? core::defaultLight()
                  : config_.themeMode == "dark"  ? core::defaultDark()
                  : core::defaultSubnautica();
                applyActivePalette();
                saveConfig();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();                 // ##settingsCol (card)
    ImGui::PopStyleVar(2);             // WindowPadding + ChildRounding
    ImGui::PopStyleColor();            // card surface
    ImGui::PopStyleVar();              // Alpha
    ImGui::EndChild();                 // ##settings
    ImGui::PopStyleColor();            // bg
}

}
