#pragma once

#include <charconv>
#include <compare>
#include <map>
#include <string_view>
#include <vector>
#include <algorithm>

namespace corum::ranked {

// GD 2.2 stores dependencies in comma-separated native level fields. Reject
// partial/overflowing tokens rather than downloading an unrelated truncated ID.
inline bool appendRoundResourceIds(std::vector<int>& ids, std::string_view text) {
    while (!text.empty()) {
        auto end = text.find(',');
        auto token = text.substr(0, end);
        int id = 0;
        auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
        if (token.empty() || parsed.ec != std::errc() || parsed.ptr != token.data() + token.size() || id <= 0)
            return false;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
        if (text.empty()) return false;
    }
    return true;
}

enum class RoundResourceKind { SongInfo, Song, Sfx };
struct RoundResourceKey {
    RoundResourceKind kind;
    int id;
    auto operator<=>(RoundResourceKey const&) const = default;
};
struct RoundResourceRetry {
    unsigned attempts = 0;
    double nextAt = 0;
    bool available(double now) const { return attempts < 3 && now >= nextAt; }
    void started() { ++attempts; }
    void failed(double now) { nextAt = now + 2.0 * attempts; }
};

inline bool roundPrepareState(std::string_view state) {
    return state == "ROUND_PREPARE" || state == "DEATHMATCH_PREPARE" || state == "MAP_PREPARE";
}
inline bool roundAcquireState(std::string_view state) {
    return roundPrepareState(state) || state == "ROUND_PLAYING" || state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" || state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING" || state == "MAP_PLAYING";
}
// This preserves the old 10s Ranked / 5s Placement READY timing. The server's
// acknowledged PLAYING state is still required before constructing gameplay.
inline bool roundReadyDue(std::string_view state, bool placement, double phaseSeconds, bool resourcesReady) {
    return roundPrepareState(state) && resourcesReady && phaseSeconds >= (placement ? 5.0 : 10.0);
}

} // namespace corum::ranked
