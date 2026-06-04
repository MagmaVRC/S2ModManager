#include "ModName.h"
#include "Paths.h"
#include <cctype>
#include <vector>

namespace core {

namespace {

bool isAllDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string collapseSpaces(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (char c : s) {
        bool sp = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (sp) { if (!prevSpace && !out.empty()) out.push_back(' '); }
        else out.push_back(c);
        prevSpace = sp;
    }
    return trim(out);
}

}

ParsedModName parseModName(const std::string& rawStem) {
    const std::string cleaned = collapseSpaces(rawStem);
    ParsedModName fallback{ cleaned.empty() ? rawStem : cleaned, "", std::nullopt };

    // Strip an optional trailing "(N)" duplicate marker before matching.
    std::string s = rawStem;
    if (!s.empty() && s.back() == ')') {
        size_t open = s.rfind('(');
        if (open != std::string::npos && open + 1 < s.size() - 1
            && isAllDigits(s.substr(open + 1, s.size() - 1 - (open + 1))))
            s = trim(s.substr(0, open));
    }

    std::vector<std::string> tok;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '-') {
            tok.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (tok.size() < 4) return fallback;   // need name + modId + >=1 version + timestamp

    // Trailing token must be a plausible 9-10 digit unix timestamp.
    const std::string& ts = tok.back();
    if (ts.size() < 9 || ts.size() > 10 || !isAllDigits(ts)) return fallback;
    long long tsv = 0;
    try { tsv = std::stoll(ts); } catch (...) { return fallback; }
    if (tsv < 1'000'000'000LL || tsv > 4'000'000'000LL) return fallback;

    // Collect the run of pure-integer tokens immediately before the timestamp.
    int runStart = static_cast<int>(tok.size()) - 1;   // index of timestamp
    while (runStart - 1 >= 1 && isAllDigits(tok[runStart - 1]))
        --runStart;
    // tok[runStart .. size-2] are [modId, version...]; need at least the modId, and a non-empty name.
    if (runStart < 1) return fallback;

    std::string name;
    for (int i = 0; i < runStart; ++i) {
        if (i) name += '-';
        name += tok[i];
    }
    name = trim(name);
    if (name.empty()) return fallback;

    ParsedModName out;
    out.name = name;
    try { out.nexusId = std::stoi(tok[runStart]); } catch (...) { out.nexusId = std::nullopt; }

    std::string ver;
    for (int i = runStart + 1; i < static_cast<int>(tok.size()) - 1; ++i) {
        if (!ver.empty()) ver += '.';
        ver += tok[i];
    }
    out.version = ver;
    return out;
}

}
