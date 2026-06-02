#include "Game.h"
#include "Steam.h"
#include "Paths.h"
#include <windows.h>

namespace core {

namespace {
GamePaths derive(const std::filesystem::path& root) {
    GamePaths p;
    p.root = root;
    p.project = root / "Subnautica2";
    p.binWin64 = p.project / "Binaries" / "Win64";
    p.ue4ss = p.binWin64 / "ue4ss";
    p.ue4ssMods = p.ue4ss / "Mods";
    p.pakMods = p.project / "Content" / "Paks" / "~mods";
    p.logicMods = p.project / "Content" / "Paks" / "LogicMods";
    return p;
}
}

Game Game::resolve(const std::string& overrideRoot) {
    Game g;
    std::filesystem::path root;
    if (!overrideRoot.empty())
        root = pathFromUtf8(overrideRoot);
    else if (auto detected = findSubnautica2())
        root = *detected;

    std::error_code ec;
    if (!root.empty() && std::filesystem::exists(root / "Subnautica2.exe", ec)) {
        g.paths_ = derive(root);
        g.valid_ = true;
    }
    return g;
}

bool Game::ue4ssInstalled() const {
    if (!valid_) return false;
    if (cachedUe4ss_ >= 0) return cachedUe4ss_ != 0;
    std::error_code ec;
    cachedUe4ss_ = (std::filesystem::exists(paths_.ue4ss, ec) &&
                    std::filesystem::exists(paths_.binWin64 / "dwmapi.dll", ec)) ? 1 : 0;
    return cachedUe4ss_ != 0;
}

bool Game::reshadeInstalled() const {
    if (!valid_) return false;
    if (cachedReShade_ >= 0) return cachedReShade_ != 0;
    std::error_code ec;
    cachedReShade_ = (std::filesystem::exists(paths_.binWin64 / "dxgi.dll", ec) &&
                      std::filesystem::exists(paths_.binWin64 / "ReShade.ini", ec)) ? 1 : 0;
    return cachedReShade_ != 0;
}

std::string Game::rootUtf8() const {
    return narrow(paths_.root.wstring());
}

}
