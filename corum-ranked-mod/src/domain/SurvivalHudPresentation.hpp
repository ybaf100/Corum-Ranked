#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace corum::ranked {
struct SurvivalStanding {
    std::string id, name;
    int rank = 0, attemptsUsed = 0, attemptsFinished = 0;
    double score = 0;
    bool passing = false, bot = false, waiting = false;
    std::string tier="UNRANKED";
};
struct SurvivalHudColor { unsigned char r,g,b; bool operator==(SurvivalHudColor const&) const = default; };
inline SurvivalHudColor survivalRankColor(int rank, bool passing) {
    if (!passing) return {255,82,105};
    if (rank == 1) return {255,211,78};
    if (rank == 2) return {210,219,233};
    if (rank == 3) return {218,145,83};
    if (rank == 4 || rank == 5) return {79,230,250};
    return {160,172,190};
}
inline SurvivalHudColor survivalResultRankColor(int rank, int lpDelta) {
    if (lpDelta < 0) return {255,82,105};
    if (rank >= 1 && rank <= 3) return survivalRankColor(rank,true);
    return lpDelta > 0 ? SurvivalHudColor{90,239,133} : SurvivalHudColor{160,172,190};
}
inline std::string survivalOrdinal(int rank) {
    auto const tail=rank%100;
    return std::to_string(rank)+(tail>=11 && tail<=13 ? "th" : rank%10==1 ? "st" : rank%10==2 ? "nd" : rank%10==3 ? "rd" : "th");
}
inline std::vector<SurvivalStanding> survivalHudRows(std::vector<SurvivalStanding> const& standings,
    std::string const& ownId, bool full) {
    if (full) return standings;
    auto rows=std::vector<SurvivalStanding>(standings.begin(),standings.begin()+std::min<std::size_t>(3,standings.size()));
    auto own=std::find_if(standings.begin(),standings.end(),[&](auto const& p){return p.id==ownId;});
    if (own!=standings.end() && std::none_of(rows.begin(),rows.end(),[&](auto const& p){return p.id==ownId;})) {
        if(own->rank<=3 && !rows.empty())rows.back()=*own;
        else rows.push_back(*own);
    }
    return rows;
}
// A presentation-only filter; ranks and cutoff always use authoritative scores.
struct SurvivalScoreTween {
    double displayed=0,target=0;
    void observe(double score) { if(std::isfinite(score))target=std::max(target,std::clamp(score,0.0,100.0)); }
    void advance(double dt) {
        if (!std::isfinite(dt) || dt<=0) return;
        displayed+=(target-displayed)*(1-std::exp(-std::min(dt,.25)*8));
        if(std::abs(target-displayed)<.001)displayed=target;
    }
};
}
