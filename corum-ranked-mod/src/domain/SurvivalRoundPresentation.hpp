#pragma once
#include <string>
namespace corum::ranked {
// Server ordinal is the actual played round order. Internal phase owns rules only.
inline std::string survivalRoundLabel(std::string const& phase,int displayRound,bool compact=false) {
    if(phase=="TIEBREAK")return "DEATH MATCH";
    if(phase=="FINAL")return "FINAL";
    return std::string(compact?"R":"ROUND ")+(displayRound>0?std::to_string(displayRound):"?");
}
}
