#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace corum::ranked {
inline std::string rankTierBase(std::string tier) {
    std::transform(tier.begin(),tier.end(),tier.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
    if(tier=="MASTER" || tier=="CHAMPION")return "GOLD";
    if(!tier.empty() && tier.back()>='1' && tier.back()<='3')tier.pop_back();
    return tier;
}
inline bool goldRankTier(std::string const& tier){return rankTierBase(tier)=="GOLD";}
inline std::string rankTierTrackBase(std::string const& tier) {
    auto const base=rankTierBase(tier);
    return base=="AQUA"?"RED":base=="BRONZE"?"AQUA":base=="SILVER"?"BRONZE":base=="GOLD"?"SILVER":"";
}
inline int rankTierOrder(std::string const& tier) {
    static std::array<std::string,15> const order {"RED3","RED2","RED1","AQUA3","AQUA2","AQUA1","BRONZE3","BRONZE2","BRONZE1","SILVER3","SILVER2","SILVER1","GOLD","MASTER","CHAMPION"};
    auto const found=std::find(order.begin(),order.end(),tier);
    if(found!=order.end())return static_cast<int>(found-order.begin());
    static std::array<std::string,5> const bases {"RED","AQUA","BRONZE","SILVER","GOLD"};
    auto const base=std::find(bases.begin(),bases.end(),tier);
    return base==bases.end()?-1:static_cast<int>(base-bases.begin())*3;
}
inline std::string rankTierDisplay(std::string tier) {
    if(tier.size()>1 && tier.back()>='1' && tier.back()<='3') {
        auto const division=tier.back();tier.pop_back();
        return tier+(division=='3'?" III":division=='2'?" II":" I");
    }
    return tier;
}
inline int silverHaloLevel(std::string const& tier) {
    return tier=="SILVER3"?0:tier=="SILVER2"?1:rankTierBase(tier)=="SILVER"?2:0;
}
} // namespace corum::ranked
