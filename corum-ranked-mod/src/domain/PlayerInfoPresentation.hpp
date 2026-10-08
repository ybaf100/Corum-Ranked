#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <string_view>

namespace corum::ranked {
// GD's DemonInfoPopup has five classic and five platformer difficulty
// counters, followed by weekly and gauntlet counts. Do not count those twice.
inline std::optional<int> extremeDemonCount(std::string_view text) {
    std::array<int, 12> counts {};
    std::size_t start = 0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        auto end = text.find(',', start);
        if (end == std::string_view::npos) end = text.size();
        auto const part = text.substr(start, end - start);
        auto const parsed = std::from_chars(part.data(), part.data() + part.size(), counts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || counts[i] < 0) return std::nullopt;
        if (end == text.size()) {
            if (i != counts.size() - 1) return std::nullopt;
            break;
        }
        if (i == counts.size() - 1) return std::nullopt;
        start = end + 1;
    }
    if (counts[4] > 2'147'483'647 - counts[9]) return std::nullopt;
    return counts[4] + counts[9];
}
} // namespace corum::ranked
