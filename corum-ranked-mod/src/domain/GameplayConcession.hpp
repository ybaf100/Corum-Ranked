#pragma once
#include <string>
#include <unordered_set>

namespace corum::ranked {
// An authenticated concession is a terminal command for that gameplay scene.
// Unlike a deadline, it must survive later state polls and round advancement.
class GameplayConcessionJournal {
    std::string m_match;
    std::unordered_set<std::string> m_contexts;
    bool m_wholeMatch = false;
    static std::string context(int round, int deathmatch) {
        return deathmatch > 0 ? "D:" + std::to_string(deathmatch) : "R:" + std::to_string(round);
    }
public:
    void bind(std::string const& match) {
        if (match == m_match) return;
        m_match = match; m_contexts.clear(); m_wholeMatch = false;
    }
    bool observe(std::string const& match, std::string const& scope, int round, int deathmatch) {
        if (match.empty() || (scope != "ROUND" && scope != "MATCH") ||
            deathmatch < 0 || (deathmatch == 0 && round <= 0)) return false;
        bind(match);
        m_contexts.insert(context(round, deathmatch));
        m_wholeMatch = m_wholeMatch || scope == "MATCH";
        return true;
    }
    [[nodiscard]] bool conceded(std::string const& match, int round, int deathmatch) const {
        return !match.empty() && match == m_match &&
            (m_wholeMatch || m_contexts.contains(context(round, deathmatch)));
    }
};
}
