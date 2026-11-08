#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace corum::ranked {

// Survival elimination effects. The round's 1st place plays its equipped
// effect on every eliminated player; DEFAULT is the plain fall.
enum class EliminationEffect { Default, Bolt, Slice, Shatter, Vortex };
inline constexpr std::array<EliminationEffect,5> kEliminationEffects{
    EliminationEffect::Default,EliminationEffect::Bolt,EliminationEffect::Slice,EliminationEffect::Shatter,EliminationEffect::Vortex};

inline std::string_view eliminationEffectCode(EliminationEffect effect) {
    switch(effect){
        case EliminationEffect::Bolt: return "BOLT";
        case EliminationEffect::Slice: return "SLICE";
        case EliminationEffect::Shatter: return "SHATTER";
        case EliminationEffect::Vortex: return "VORTEX";
        default: return "DEFAULT";
    }
}
inline EliminationEffect eliminationEffectFromCode(std::string_view code) {
    for(auto effect:kEliminationEffects)if(eliminationEffectCode(effect)==code)return effect;
    return EliminationEffect::Default;
}

struct SurvivalEffectPlayer {
    std::string id,name;
    int rank=0;
    double lp=0;
    EliminationEffect effect=EliminationEffect::Default;
};
struct SurvivalExecution {
    std::string executorId; // empty: no executor (default fall)
    EliminationEffect effect=EliminationEffect::Default;
};

inline std::string survivalEffectNameKey(std::string const& name) {
    std::string key;key.reserve(name.size());
    for(unsigned char ch:name)key.push_back(static_cast<char>(std::tolower(ch)));
    return key;
}

/** Executions in elimination `order` (victim ids). One 1st place executes
 * everyone; two tied 1st places alternate in that order starting with the
 * higher Survival LP (then name A→Z, case-insensitive), so an odd count gives
 * the first executor one more; three or more tied 1st places play DEFAULT. */
inline std::vector<SurvivalExecution> survivalEliminationExecutions(
    std::vector<SurvivalEffectPlayer> const& players,std::vector<std::string> const& order) {
    std::vector<SurvivalEffectPlayer> winners;
    for(auto const& player:players)if(player.rank==1)winners.push_back(player);
    std::stable_sort(winners.begin(),winners.end(),[](auto const& a,auto const& b){
        if(a.lp!=b.lp)return a.lp>b.lp;
        auto const an=survivalEffectNameKey(a.name),bn=survivalEffectNameKey(b.name);
        if(an!=bn)return an<bn;
        return a.id<b.id;
    });
    std::vector<SurvivalExecution> executions;executions.reserve(order.size());
    for(std::size_t i=0;i<order.size();++i){
        if(winners.empty() || winners.size()>2){executions.push_back({});continue;}
        auto const& executor=winners[winners.size()==1?0:i%2];
        executions.push_back({executor.id,executor.effect});
    }
    return executions;
}

// Effect picker: requirement text per effect (translation key + tier).
struct EliminationRequirementText { std::string_view key; std::string_view tier; };
inline EliminationRequirementText eliminationRequirement(EliminationEffect effect) {
    switch(effect){
        case EliminationEffect::Bolt: return {"elim-req-survival","SILVER"};
        case EliminationEffect::Slice: return {"elim-req-both","GOLD"};
        case EliminationEffect::Shatter: return {"elim-req-grade","BRONZE"};
        case EliminationEffect::Vortex: return {"elim-req-grade","GOLD"};
        default: return {"elim-req-none",""};
    }
}

/** Private achievement grade from the claimed AP total; mirrors the rules
 * package ACHIEVEMENT_AP_GRADES (4,965 AP achievable). */
struct AchievementApGrade { std::string_view tier; int floorAp=0; std::string_view nextTier; int nextAp=0; };
inline constexpr std::array<std::pair<std::string_view,int>,5> kAchievementApGrades{{
    {"RED",100},{"AQUA",400},{"BRONZE",1000},{"SILVER",2000},{"GOLD",3200}}};
inline AchievementApGrade achievementApGrade(int ap) {
    AchievementApGrade grade;int index=-1;
    for(std::size_t i=0;i<kAchievementApGrades.size();++i)if(ap>=kAchievementApGrades[i].second)index=static_cast<int>(i);
    if(index>=0){grade.tier=kAchievementApGrades[index].first;grade.floorAp=kAchievementApGrades[index].second;}
    if(index+1<static_cast<int>(kAchievementApGrades.size())){grade.nextTier=kAchievementApGrades[index+1].first;grade.nextAp=kAchievementApGrades[index+1].second;}
    return grade;
}
/** Grade progress (0..1) between the current grade floor and the next grade. */
inline float achievementGradeProgress(int ap,int floorAp,int nextAp) {
    if(nextAp<=floorAp)return 1.0f;
    return std::clamp(static_cast<float>(ap-floorAp)/static_cast<float>(nextAp-floorAp),0.0f,1.0f);
}

} // namespace corum::ranked
