#include "Config.h"
#include "Paths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace core {

namespace {
nlohmann::json paletteToJson(const Palette& p) {
    return {
        {"bg", colorToHex(p.bg)}, {"surface", colorToHex(p.surface)},
        {"surface2", colorToHex(p.surface2)}, {"surface3", colorToHex(p.surface3)},
        {"border", colorToHex(p.border)}, {"accent", colorToHex(p.accent)},
        {"accentHi", colorToHex(p.accentHi)}, {"accentDim", colorToHex(p.accentDim)},
        {"warn", colorToHex(p.warn)}, {"select", colorToHex(p.select)},
        {"selectHi", colorToHex(p.selectHi)}, {"text", colorToHex(p.text)},
        {"textHi", colorToHex(p.textHi)}, {"textDim", colorToHex(p.textDim)},
        {"ink", colorToHex(p.ink)},
    };
}

Palette paletteFromJson(const nlohmann::json& j, const Palette& def) {
    auto g = [&](const char* k, std::uint32_t d) {
        return j.contains(k) && j[k].is_string() ? hexToColor(j[k].get<std::string>(), d) : d;
    };
    return Palette{
        g("bg", def.bg), g("surface", def.surface), g("surface2", def.surface2),
        g("surface3", def.surface3), g("border", def.border), g("accent", def.accent),
        g("accentHi", def.accentHi), g("accentDim", def.accentDim), g("warn", def.warn),
        g("select", def.select), g("selectHi", def.selectHi),
        g("text", def.text), g("textHi", def.textHi), g("textDim", def.textDim), g("ink", def.ink),
    };
}
}

void to_json(nlohmann::json& j, const Config& c) {
    j = nlohmann::json{
        {"gamePath", c.gamePath},
        {"pakModsDir", c.pakModsDir},
        {"ue4ssModsDir", c.ue4ssModsDir},
        {"libraryPath", c.libraryPath},
        {"ue4ssInstalled", c.ue4ssInstalled},
        {"reshadeVersion", c.reshadeVersion},
        {"themeMode", c.themeMode},
        {"subnautica", paletteToJson(c.subnautica)},
        {"light", paletteToJson(c.light)},
        {"dark", paletteToJson(c.dark)},
        {"vsync", c.vsync},
        {"uiScale", c.uiScale},
        {"activeProfileName", c.activeProfileName},
        {"includePrereleases", c.includePrereleases},
        {"background", {
            {"enabled", c.background.enabled},
            {"darkImage", c.background.darkImage},
            {"lightImage", c.background.lightImage},
            {"subnauticaImage", c.background.subnauticaImage},
            {"blur", c.background.blur},
            {"dim", c.background.dim},
            {"panelOpacity", c.background.panelOpacity},
            {"driftAmount", c.background.driftAmount},
            {"driftSpeed", c.background.driftSpeed},
        }},
    };
}

void from_json(const nlohmann::json& j, Config& c) {
    c.gamePath          = j.value("gamePath", "");
    c.pakModsDir        = j.value("pakModsDir", "");
    c.ue4ssModsDir      = j.value("ue4ssModsDir", "");
    c.libraryPath       = j.value("libraryPath", "");
    c.ue4ssInstalled    = j.value("ue4ssInstalled", false);
    c.reshadeVersion    = j.value("reshadeVersion", "");
    c.themeMode         = j.value("themeMode", "subnautica");
    c.subnautica        = j.contains("subnautica") ? paletteFromJson(j["subnautica"], defaultSubnautica()) : defaultSubnautica();
    c.light             = j.contains("light") ? paletteFromJson(j["light"], defaultLight()) : defaultLight();
    c.dark              = j.contains("dark")  ? paletteFromJson(j["dark"],  defaultDark())  : defaultDark();
    c.vsync             = j.value("vsync", true);
    c.uiScale           = j.value("uiScale", 1.0f);
    c.activeProfileName = j.value("activeProfileName", "Vanilla");
    c.includePrereleases = j.value("includePrereleases", false);
    if (j.contains("background") && j["background"].is_object()) {
        const auto& b = j["background"];
        c.background.enabled         = b.value("enabled", true);
        c.background.darkImage       = b.value("darkImage", "");
        c.background.lightImage      = b.value("lightImage", "");
        c.background.subnauticaImage = b.value("subnauticaImage", "");
        c.background.blur            = b.value("blur", 0.35f);
        c.background.dim             = b.value("dim", 0.48f);
        c.background.panelOpacity    = b.value("panelOpacity", 0.72f);
        c.background.driftAmount     = b.value("driftAmount", 0.0f);
        c.background.driftSpeed      = b.value("driftSpeed", 1.0f);
    }
}

static void validate(Config& c) {
    auto clamp01 = [](float& v) { if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f; };
    c.uiScale = std::clamp(c.uiScale, 0.5f, 2.0f);
    if (c.themeMode != "dark" && c.themeMode != "light" && c.themeMode != "subnautica")
        c.themeMode = "subnautica";
    clamp01(c.background.blur);
    clamp01(c.background.dim);
    clamp01(c.background.panelOpacity);
    clamp01(c.background.driftAmount);
    c.background.driftSpeed = std::clamp(c.background.driftSpeed, 0.0f, 2.0f);
}

Config Config::load() {
    Config c;
    std::ifstream in(dataFile().parent_path() / L"settings.json", std::ios::binary);
    if (!in)
        return c;
    std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        c = nlohmann::json::parse(txt).get<Config>();
    } catch (const nlohmann::json::exception&) {
        return Config{};
    }
    validate(c);
    return c;
}

bool Config::save() const {
    std::ofstream out(dataFile().parent_path() / L"settings.json", std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << nlohmann::json(*this).dump(2);
    return out.good();
}

}
