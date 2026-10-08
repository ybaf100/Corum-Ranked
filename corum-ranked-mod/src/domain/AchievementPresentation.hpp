#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace corum::ranked {

enum class AchievementDisplayColor {
    Gray,
    Bronze,
    Silver,
    Gold,
    Diamond,
};

enum class AchievementClaimState {
    Locked,
    Unclaimed,
    Claimed,
};

inline std::string achievementStageLabel(int level) {
    static constexpr char const* labels[] = {"", "I", "II", "III", "IV", "V"};
    return level >= 1 && level <= 5 ? labels[level] : "";
}

inline AchievementDisplayColor achievementDisplayColor(int level) {
    switch (level) {
        case 1: return AchievementDisplayColor::Gray;
        case 2: return AchievementDisplayColor::Bronze;
        case 3: return AchievementDisplayColor::Silver;
        case 4: return AchievementDisplayColor::Gold;
        case 5: return AchievementDisplayColor::Diamond;
        default: return AchievementDisplayColor::Gray;
    }
}

inline double achievementGauge(
    double metricValue,
    double currentThreshold,
    std::optional<double> nextThreshold,
    bool maxed
) {
    if (maxed || !nextThreshold) return 1.0;
    auto const span = std::max(1.0, *nextThreshold - currentThreshold);
    return std::clamp((metricValue - currentThreshold) / span, 0.0, 1.0);
}

inline std::string achievementBadgeText(int count) {
    if (count <= 0) return "";
    if (count > 99) return "99+";
    return std::to_string(count);
}

inline bool achievementTitleLevelSelectable(int level, int highestUnlockedLevel) {
    return level >= 1 && level <= 5 && level <= highestUnlockedLevel;
}

inline AchievementClaimState achievementClaimState(
    bool unlocked,
    int currentLevel,
    int claimableRewards
) {
    if (claimableRewards > 0) return AchievementClaimState::Unclaimed;
    if (unlocked || currentLevel > 0) return AchievementClaimState::Claimed;
    return AchievementClaimState::Locked;
}

class AchievementToastQueue final {
public:
    bool enqueue(std::string key) {
        if (key.empty() || m_seen.contains(key)) return false;
        m_seen.insert(key);
        m_pending.push_back(std::move(key));
        return true;
    }

    [[nodiscard]] bool empty() const { return m_pending.empty(); }
    [[nodiscard]] std::size_t size() const { return m_pending.size(); }

    std::optional<std::string> pop() {
        if (m_pending.empty()) return std::nullopt;
        auto value = std::move(m_pending.front());
        m_pending.pop_front();
        return value;
    }

private:
    std::deque<std::string> m_pending;
    std::unordered_set<std::string> m_seen;
};

} // namespace corum::ranked
