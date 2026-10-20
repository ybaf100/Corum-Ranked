#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
namespace corum::ranked {
struct AchievementRgb {float r,g,b;};
inline constexpr std::array<AchievementRgb,7> AchievementPastels{{
 {255,184,198},{255,213,176},{255,242,179},{189,240,209},{185,223,255},{211,194,255},{243,196,235}}};
inline int achievementRarityTier(std::string_view rarity) {
 if(rarity=="MYTHIC")return 5;if(rarity=="LEGENDARY")return 4;if(rarity=="EPIC")return 3;if(rarity=="RARE")return 2;
 return rarity=="COMMON"?1:0;
}
inline AchievementRgb achievementTierColor(int tier) {
 switch(tier){case 1:return {178,186,201};case 2:return {87,187,255};case 3:return {198,121,255};case 4:return {255,176,70};case 5:return AchievementPastels[0];default:return {92,100,116};}
}
inline AchievementRgb achievementPastelAt(double position,double seconds) {
 auto phase=position+seconds/3.;phase-=std::floor(phase);phase*=AchievementPastels.size();
 auto const i=static_cast<std::size_t>(phase);auto const t=static_cast<float>(phase-i);
 auto const a=AchievementPastels[i],b=AchievementPastels[(i+1)%AchievementPastels.size()];
 return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};
}
struct AchievementPending {int amount=0,rewards=0,tier=0;};
template<class Cards>AchievementPending achievementPending(Cards const& cards,std::string_view scope){
 AchievementPending result;
 for(auto const& card:cards){if(scope!="ALL" && card.scope!=scope)continue;
  for(auto const& reward:card.unlocks){
   if(reward.unlockedAt.empty() || !reward.claimedAt.empty() || reward.rewardAp<0)continue;
   auto const tier=card.type=="PROGRESSIVE"?reward.level.value_or(0):achievementRarityTier(card.rarity);
   if(tier<1 || tier>5)continue;
   result.amount+=reward.rewardAp;++result.rewards;result.tier=std::max(result.tier,tier);
  }
 }return result;
}
template<class Card>int achievementCardTier(Card const& card){
 return card.type=="PROGRESSIVE"?std::clamp(card.currentLevel,0,5):(card.unlocked?achievementRarityTier(card.rarity):0);
}
}
