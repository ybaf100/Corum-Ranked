#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
namespace corum::ranked {
enum class SurvivalOutcomeChoice { Undecided, Observe, PersonalResult };
enum class SurvivalFlowStage { Lobby, Intro, Selecting, Preparing, EntryCountdown, Playing, RoundResult,
    EliminatedChoice, Observing, PersonalResult, Ceremony, Cancelled };
inline SurvivalFlowStage survivalFlowStage(std::string const& state,std::string const& gate,bool eliminated,SurvivalOutcomeChoice choice){
    if(state.empty())return SurvivalFlowStage::Lobby;
    if(state=="CANCELLED")return SurvivalFlowStage::Cancelled;
    if(eliminated && choice==SurvivalOutcomeChoice::PersonalResult)return SurvivalFlowStage::PersonalResult;
    if(state=="FINAL_RESULT")return SurvivalFlowStage::Ceremony;
    if(eliminated)return choice==SurvivalOutcomeChoice::Observe?SurvivalFlowStage::Observing:SurvivalFlowStage::EliminatedChoice;
    if(gate=="INTRO")return SurvivalFlowStage::Intro;
    if(state=="MAP_SELECTION")return SurvivalFlowStage::Selecting;
    if(state=="ROUND_RESULT")return SurvivalFlowStage::RoundResult;
    if(state=="PLAYING")return SurvivalFlowStage::Playing;
    if(state=="ENTERING" || state=="COUNTDOWN")return SurvivalFlowStage::EntryCountdown;
    return SurvivalFlowStage::Preparing;
}
struct SurvivalReadyFrame {int digit=0;double scale=1,opacity=1;};
inline SurvivalReadyFrame survivalReadyFrame(std::optional<std::int64_t> remaining){
    if(!remaining || *remaining<=0 || *remaining>5000)return {};
    auto const digit=static_cast<int>((*remaining+999)/1000);
    auto const age=1-(*remaining-(digit-1)*1000)/1000.;
    return {digit,1.20-.20*std::clamp(age/.65,0.,1.),std::min(std::clamp(age/.09,0.,1.),std::clamp((1-age)/.24,0.,1.))};
}
inline int survivalWatchLock(std::int64_t serverNow,std::int64_t eliminatedAt){
    return static_cast<int>((std::clamp<std::int64_t>(eliminatedAt+5000-serverNow,0,5000)+999)/1000);
}
inline bool survivalRankHaloEligible(std::string const& state,bool eliminated,bool observer,int officialRank){
    return state=="PLAYING" && !eliminated && !observer && officialRank==1;
}
}
