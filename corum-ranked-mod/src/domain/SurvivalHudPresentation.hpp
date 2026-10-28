#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corum::ranked {
inline int survivalRemainingAttempts(int serverUsed,int localUsed,int limit=6){return limit-std::clamp(std::max(serverUsed,localUsed),0,limit);}
inline std::optional<double> survivalComparisonScore(std::string_view phase,
    std::optional<double> cutoff,std::optional<double> first) {
    return phase=="FINAL" || phase=="TIEBREAK"?first:cutoff;
}
struct SurvivalStanding {
    std::string id, name;
    int rank = 0, attemptsUsed = 0, attemptsFinished = 0;
    double score = 0;
    bool passing = false, bot = false, waiting = false;
    std::string tier="UNRANKED";
    int accountId=0;
    double bestProgress=0,currentProgress=0;
    bool attemptActive=false,cleared=false,eliminated=false,qualified=false;
    std::string attemptId="";
};
struct SurvivalHudColor { unsigned char r,g,b; bool operator==(SurvivalHudColor const&) const = default; };
inline constexpr SurvivalHudColor SurvivalSilver{240,247,255};
inline SurvivalHudColor survivalRankColor(int rank, bool passing) {
    if (!passing) return {255,82,105};
    if (rank == 1) return {255,211,78};
    if (rank == 2) return SurvivalSilver;
    if (rank == 3) return {218,145,83};
    if (rank == 4 || rank == 5) return {79,230,250};
    return rank>0?SurvivalHudColor{90,239,133}:SurvivalHudColor{160,172,190};
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
inline std::size_t survivalHudLeaderCount(std::vector<SurvivalStanding> const& standings,int targetQualifiers) {
    // Qualification only controls the cutoff marker, never the number of rows.
    (void)targetQualifiers;
    return std::min<std::size_t>(standings.size(),10);
}
inline std::vector<SurvivalStanding> survivalHudRows(std::vector<SurvivalStanding> const& standings,
    std::string const& ownId, int targetQualifiers) {
    auto const count=survivalHudLeaderCount(standings,targetQualifiers);
    auto rows=std::vector<SurvivalStanding>(standings.begin(),standings.begin()+count);
    auto own=std::find_if(standings.begin(),standings.end(),[&](auto const& p){return p.id==ownId;});
    if (own!=standings.end() && std::none_of(rows.begin(),rows.end(),[&](auto const& p){return p.id==ownId;})) {
        rows.push_back(*own);
    }
    return rows;
}
inline std::size_t survivalHudCutoffBoundary(std::vector<SurvivalStanding> const& rows) {
    auto const firstOut=std::find_if(rows.begin(),rows.end(),[](auto const& p){return !p.passing;});
    return static_cast<std::size_t>(firstOut-rows.begin());
}
// Position within the full native fill, never its shrinking texture rectangle.
inline float survivalProgressMarkerX(double score,float width) {
    return static_cast<float>(std::clamp(std::isfinite(score)?score:0.0,0.0,100.0)/100.0)*width;
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
// Display only samples already confirmed by the server. A new attempt resets
// NOW; BEST is monotone. No extrapolation can create an unconfirmed record.
struct SurvivalProgressTween {
    SurvivalScoreTween best;
    double now=0,targetNow=0;
    std::uint64_t revision=0;
    std::string attempt;
    void observe(std::uint64_t next,std::string const& id,bool active,double score,double current){
        if(next<revision)return;
        revision=next;best.observe(score);
        if(!active){attempt.clear();now=targetNow=0;return;}
        if(attempt!=id){attempt=id;now=targetNow=0;}
        if(std::isfinite(current))targetNow=std::max(targetNow,std::clamp(current,0.,100.));
    }
    void advance(double dt){
        best.advance(dt);
        if(!std::isfinite(dt)||dt<=0)return;
        now+=(targetNow-now)*(1-std::exp(-std::min(dt,.25)*10));
        if(std::abs(targetNow-now)<.001)now=targetNow;
    }
};
}
