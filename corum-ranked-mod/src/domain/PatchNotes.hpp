#pragma once
#include <matjson.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace corum::ranked {

// One release's player-facing notes, as served by /api/ranked/patch-notes
// (newest first), in English and Korean.
struct PatchNoteView {
    std::string version, date;
    std::vector<std::string> en, ko;
    [[nodiscard]] std::vector<std::string> const& lines(bool korean) const { return korean && !ko.empty() ? ko : en; }
};

// Malformed entries are skipped; at most 30 entries and 40 lines of 400
// bytes each are kept (the popup draws them all).
inline std::vector<PatchNoteView> patchNotesFromJson(matjson::Value const& value) {
    std::vector<PatchNoteView> notes;
    auto const text = [](matjson::Value const& list) {
        std::vector<std::string> lines;
        std::vector<matjson::Value> const items = list.isArray() ? list.asArray().unwrap() : std::vector<matjson::Value>{};
        for (auto const& line : items) {
            auto s = line.asString().unwrapOr("");
            if (s.empty() || lines.size() >= 40) continue;
            if (s.size() > 400) s.resize(400);
            lines.push_back(std::move(s));
        }
        return lines;
    };
    std::vector<matjson::Value> const entries = value["notes"].isArray() ? value["notes"].asArray().unwrap() : std::vector<matjson::Value>{};
    for (auto const& entry : entries) {
        if (notes.size() >= 30) break;
        PatchNoteView note{entry["version"].asString().unwrapOr(""), entry["date"].asString().unwrapOr(""), text(entry["en"]), text(entry["ko"])};
        if (note.version.empty() || note.version.size() > 40 || (note.en.empty() && note.ko.empty())) continue;
        notes.push_back(std::move(note));
    }
    return notes;
}

// "v0.6.0-beta.87" and "0.6.0-beta.87" name the same release.
inline bool samePatchVersion(std::string a, std::string b) {
    auto const trim = [](std::string& v) { if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1); };
    trim(a); trim(b);
    return !a.empty() && a == b;
}

} // namespace corum::ranked
