#include "UpdateCheck.h"
#include "AppVersion.h"
#include "Http.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

namespace core {

namespace {

constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/MagmaVRC/S2ModManager/releases/latest";
constexpr const char* kReleasesListApi =
    "https://api.github.com/repos/MagmaVRC/S2ModManager/releases?per_page=30";

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

std::string trimVersionPrefix(std::string_view v) {
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
        v.remove_prefix(1);
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V'))
        v.remove_prefix(1);
    return std::string(v);
}

bool parsePart(std::string_view s, int& out) {
    if (s.empty())
        return false;
    int value = 0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last)
        return false;
    out = value;
    return true;
}

std::optional<SemVer> parseSemVer(std::string_view input) {
    std::string v = trimVersionPrefix(input);
    const std::size_t suffix = v.find_first_of("-+");
    if (suffix != std::string::npos)
        v.resize(suffix);

    SemVer out;
    int* parts[] = { &out.major, &out.minor, &out.patch };
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t dot = v.find('.', pos);
        std::string_view piece(v.data() + pos,
            (dot == std::string::npos ? v.size() : dot) - pos);
        if (!parsePart(piece, *parts[i]))
            return std::nullopt;
        if (dot == std::string::npos)
            return i == 2 ? std::optional<SemVer>(out) : std::nullopt;
        pos = dot + 1;
    }
    return pos >= v.size() ? std::optional<SemVer>(out) : std::nullopt;
}

bool newerThan(std::string_view latest, std::string_view current) {
    auto l = parseSemVer(latest);
    auto c = parseSemVer(current);
    if (!l || !c)
        return false;
    if (l->major != c->major) return l->major > c->major;
    if (l->minor != c->minor) return l->minor > c->minor;
    return l->patch > c->patch;
}

std::optional<AppRelease> releaseFromJson(const nlohmann::json& j) {
    if (!j.is_object() || j.value("draft", false))
        return std::nullopt;
    AppRelease release;
    release.version = j.value("tag_name", "");
    release.url = j.value("html_url", "");
    if (release.version.empty())
        return std::nullopt;
    if (release.url.empty())
        release.url = kAppReleasesUrl;
    return release;
}

}

std::optional<AppRelease> queryLatestAppRelease(bool includePrereleases) {
    if (!includePrereleases) {
        auto body = httpGet(kLatestReleaseApi);
        if (!body)
            return std::nullopt;
        nlohmann::json j = nlohmann::json::parse(*body, nullptr, false);
        if (j.is_discarded())
            return std::nullopt;
        return releaseFromJson(j);
    }

    // The /releases/latest endpoint never points at a prerelease, so scan the
    // full list and keep the highest semver among non-draft entries.
    auto body = httpGet(kReleasesListApi);
    if (!body)
        return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(*body, nullptr, false);
    if (j.is_discarded() || !j.is_array())
        return std::nullopt;

    std::optional<AppRelease> best;
    for (const auto& entry : j) {
        auto rel = releaseFromJson(entry);
        if (!rel)
            continue;
        if (!best || newerThan(rel->version, best->version))
            best = std::move(rel);
    }
    return best;
}

UpdateCheckResult checkForUpdates(bool includePrereleases) {
    UpdateCheckResult result;
    result.currentVersion = kAppVersion;

    auto latest = queryLatestAppRelease(includePrereleases);
    if (!latest) {
        result.message = "Couldn't reach GitHub or parse the latest release.";
        return result;
    }

    result.ok = true;
    result.latestVersion = latest->version;
    result.releaseUrl = latest->url;
    result.updateAvailable = newerThan(latest->version, kAppVersion);
    result.message = result.updateAvailable
        ? "A newer S2ModManager release is available."
        : "S2ModManager is up to date.";
    return result;
}

}
