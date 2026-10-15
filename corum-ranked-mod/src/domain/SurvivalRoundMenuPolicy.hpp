#pragma once
#include <string_view>

namespace corum::ranked {
enum class SurvivalMenuMusic { Round, Final, Win, Lose };
inline SurvivalMenuMusic survivalMenuMusic(std::string_view phase,bool finalResult,bool won,
    bool eliminated,bool presentingResult,std::string_view completedPhase,bool ownCompleted,bool qualified) {
    if(finalResult)return won?SurvivalMenuMusic::Win:SurvivalMenuMusic::Lose;
    if(eliminated || (presentingResult && ownCompleted && !qualified))return SurvivalMenuMusic::Lose;
    if(phase=="FINAL" || phase=="TIEBREAK" ||
        (presentingResult && ownCompleted && qualified && completedPhase=="ROUND_3"))return SurvivalMenuMusic::Final;
    return SurvivalMenuMusic::Round;
}
inline bool survivalCanPrepareNext(std::string_view state,bool ownEliminated,bool pending,bool settled) {
    return state!="FINAL_RESULT" && state!="CANCELLED" && !ownEliminated && !pending && !settled;
}
}
