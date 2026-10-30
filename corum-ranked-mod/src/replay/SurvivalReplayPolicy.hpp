#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <matjson.hpp>
namespace corum::ranked::replay {
inline constexpr int FormatVersion=1;
inline constexpr std::size_t MaxFileBytes=128*1024*1024,MaxRecords=512000,MaxBlockRecords=256,MaxPending=64;
inline std::int64_t score(double progress,bool tiebreak){
    if(!std::isfinite(progress)||progress<0||progress>100)return -1;
    // Same nonnegative milli-percent rounding as Survival rules; ordinary rounds truncate.
    if(!tiebreak)return static_cast<std::int64_t>(std::trunc(progress));
    std::string text=matjson::Value(progress).dump(matjson::NO_INDENTATION),digits;int exponent=0,fraction=0;auto e=text.find_first_of("eE");
    if(e!=std::string::npos){exponent=std::stoi(text.substr(e+1));text.resize(e);}
    auto dot=text.find('.');if(dot!=std::string::npos)fraction=static_cast<int>(text.size()-dot-1);
    for(char c:text)if(c>='0'&&c<='9')digits+=c;
    auto shift=exponent+3-fraction;
    if(shift>=0){if(digits.size()+shift>7)return -1;digits.append(shift,'0');return std::stoll(digits);}
    auto keep=static_cast<int>(digits.size())+shift;if(keep<0)return 0;
    auto base=keep==0?0:std::stoll(digits.substr(0,keep));return base+(digits[keep]>='5'?1:0);
}
struct Receipt {
    std::string matchId,roundKey,attemptId,participantId,geometryHash;
    int mapGeneration=0,levelId=0,mapVersion=0;
    double progress=0,roundBest=0;
    bool ended=false,cleared=false,accepted=false;
    std::string bestAttemptId{};
};
inline bool improves(double candidate,double previous,bool tiebreak){return score(candidate,tiebreak)>score(previous,tiebreak);}
// Rewind never reverses triggers; work is spread over render frames.
struct SeekPolicy {
    static constexpr std::size_t MaxFramesPerPump=96;
    static constexpr double MaxPumpMillis=3;
    static bool resetRequired(double current,double target){return target<current;}
};
}
