#pragma once
#include "TranslationCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
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

template<class Cards>
inline std::vector<std::size_t> achievementDisplayOrder(Cards const& cards) {
    std::vector<std::size_t> order;
    order.reserve(cards.size());
    // Stable partition by indices preserves the server's order and the exact
    // original card tags used by Claim/Title callbacks, across pagination.
    for (std::size_t i = 0; i < cards.size(); ++i) if (cards[i].claimableRewards > 0) order.push_back(i);
    for (std::size_t i = 0; i < cards.size(); ++i) if (cards[i].claimableRewards <= 0) order.push_back(i);
    return order;
}

inline std::string achievementScopeName(std::string_view scope, bool korean) {
    if (scope == "SOLO_RANKED") return corum::ranked::catalogText("inline.solo-ranked", korean, "Solo Ranked");
    if (scope == "SURVIVAL") return corum::ranked::catalogText("inline.survival", korean, "Survival");
    if (scope == "SHARED") return corum::ranked::catalogText("inline.shared", korean, "Shared");
    return corum::ranked::catalogText("inline.unclassified", korean, "Unclassified");
}

inline std::string achievementScopedName(std::string_view scope, std::string name, bool korean) {
    if (scope.empty()) return name;
    auto const prefix = "[" + achievementScopeName(scope, korean) + "] ";
    return name.starts_with(prefix) ? name : prefix + name;
}

template<class Card> inline std::string achievementCardName(Card const& card, bool korean) {
    if (card.concealed) return catalogText("achievement-hidden", korean, "HIDDEN");
    return catalogText("achievement." + card.id + ".name", korean,
        (korean ? card.nameKo : card.nameEn).c_str());
}

template<class Card> inline std::string achievementCardDescription(Card const& card, bool korean) {
    // Never look up the real local description of an unrevealed Hidden card.
    return catalogText("achievement." + card.id + (card.concealed ? ".hint" : ".description"), korean,
        (korean ? card.descriptionKo : card.descriptionEn).c_str());
}

template<class Cards>
inline std::vector<std::size_t> achievementDisplayOrderForScope(Cards const& cards, std::string_view scope) {
    auto order = achievementDisplayOrder(cards);
    std::erase_if(order, [&](std::size_t index) { return cards[index].scope != scope; });
    if constexpr(requires{cards[0].type;cards[0].rarity;cards[0].rewardAp;}){
        std::vector<std::size_t> singles;
        for(auto index:order)if(cards[index].type=="ONE_SHOT")singles.push_back(index);
        auto rarity=[](std::string const& value){return value=="COMMON"?1:value=="RARE"?2:value=="EPIC"?3:value=="LEGENDARY"?4:value=="MYTHIC"?5:6;};
        std::stable_sort(singles.begin(),singles.end(),[&](auto a,auto b){
            auto const left=rarity(cards[a].rarity),right=rarity(cards[b].rarity);
            return left!=right?left<right:cards[a].rewardAp<cards[b].rewardAp;
        });
        std::size_t next=0;for(auto& index:order)if(cards[index].type=="ONE_SHOT")index=singles[next++];
    }
    return order;
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
