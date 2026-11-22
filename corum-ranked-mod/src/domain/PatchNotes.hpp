#pragma once
#include <matjson.hpp>
#include <algorithm>
#include <array>
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

// Release order of "0.6.0-beta.87" (a leading v is ignored): major, minor,
// patch, then the beta number (a release without one sorts after its betas).
inline std::array<long,4> patchVersionOrder(std::string version) {
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    std::array<long,4> order{-1, -1, -1, 1L << 30};
    std::size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        std::size_t used = 0;
        try { order[static_cast<std::size_t>(i)] = std::stol(version.substr(at), &used); } catch (...) { return {-1, -1, -1, -1}; }
        at += used;
        if (i < 2) { if (at >= version.size() || version[at] != '.') return {-1, -1, -1, -1}; ++at; }
    }
    if (at < version.size()) {
        auto const beta = version.find("beta.", at);
        if (beta == std::string::npos) return order;
        try { order[3] = std::stol(version.substr(beta + 5)); } catch (...) { order[3] = -1; }
    }
    return order;
}

// The releases after `from` up to and including `to`, newest first; only the
// newest `limit` of them. An unknown `from` keeps everything up to `to`.
inline std::vector<PatchNoteView> patchNotesBetween(std::vector<PatchNoteView> const& notes, std::string const& from, std::string const& to, std::size_t limit = 5) {
    auto const low = patchVersionOrder(from), high = patchVersionOrder(to);
    std::vector<PatchNoteView> result;
    for (auto const& note : notes) {
        auto const order = patchVersionOrder(note.version);
        if (order[0] < 0 || (high[0] >= 0 && order > high) || (low[0] >= 0 && order <= low)) continue;
        result.push_back(note);
    }
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return patchVersionOrder(a.version) > patchVersionOrder(b.version); });
    if (result.size() > limit) result.resize(limit);
    return result;
}

// The update screen's version list: the releases being installed (newest
// first, at most `limit`), then the installed one. `fresh` counts the new.
struct UpdatePatchVersions { std::vector<PatchNoteView> list; std::size_t fresh = 0; };
inline UpdatePatchVersions updatePatchVersions(std::vector<PatchNoteView> const& notes, std::string const& from, std::string const& to, std::size_t limit = 12) {
    UpdatePatchVersions result;
    result.list = patchNotesBetween(notes, from, to, limit);
    result.fresh = result.list.size();
    auto const installed = patchVersionOrder(from);
    if (installed[0] >= 0)
        for (auto const& note : notes) if (patchVersionOrder(note.version) == installed) { result.list.push_back(note); break; }
    return result;
}
// "0.6.0-beta.91" -> "beta.91" for the narrow version column.
inline std::string updatePatchShortVersion(std::string const& version) {
    auto const dash = version.rfind('-');
    return dash == std::string::npos || dash + 1 >= version.size() ? version : version.substr(dash + 1);
}

} // namespace corum::ranked
