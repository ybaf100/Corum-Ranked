#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace corum::ranked {

// One slot of a promotion series as the lobby shows it.
enum class SeriesMark {
    Win,      // a won game
    Loss,     // a lost game
    NeedWin,  // still to play, and a win the series still needs (green outline)
    Open,     // still to play, not needed
};

// The series slots in order. The first games still to play are marked
// NeedWin, as many as wins are still missing, so the player sees what is left
// to win: 3 of 5 with L L - - - shows L L G G G, L W L - - shows L W L G G,
// W W L - - shows W W L G -. winsRequired 0 (no win count, e.g. Survival's
// cutoff series) marks none.
inline std::vector<SeriesMark> seriesMarks(int games, int winsRequired, std::vector<std::string> const& results) {
    games = std::clamp(games, 1, 9);
    auto const wins = static_cast<int>(std::count(results.begin(), results.end(), std::string("WIN")));
    auto needed = std::max(0, winsRequired - wins);
    std::vector<SeriesMark> marks;
    marks.reserve(static_cast<std::size_t>(games));
    for (int i = 0; i < games; ++i) {
        if (i < static_cast<int>(results.size())) marks.push_back(results[static_cast<std::size_t>(i)] == "WIN" ? SeriesMark::Win : SeriesMark::Loss);
        else if (needed > 0) { marks.push_back(SeriesMark::NeedWin); --needed; }
        else marks.push_back(SeriesMark::Open);
    }
    return marks;
}

} // namespace corum::ranked
