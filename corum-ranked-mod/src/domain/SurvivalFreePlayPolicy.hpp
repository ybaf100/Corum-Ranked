#pragma once
#include <string>
#include <utility>

namespace corum::ranked {
struct SurvivalRoundIdentity {
    std::string match,round;
    int mapGeneration=0;
    bool operator==(SurvivalRoundIdentity const&) const=default;
};
struct SurvivalFreePlayState {
    bool active=false,observer=false,returnRequested=false;
    int directLevelId=0;
    SurvivalRoundIdentity identity;
    std::string completedKey;
    void begin(SurvivalRoundIdentity current,std::string completed,bool observing=false,int nativeLevelId=0) {
        active=true;directLevelId=nativeLevelId;observer=observing;returnRequested=false;identity=std::move(current);completedKey=std::move(completed);
    }
    bool needsReturn(SurvivalRoundIdentity const& current,std::string const& state,
        std::string const& completed,bool eliminated) const {
        // Only authoritative state closes Free Play. A local 0s clock does not.
        return active && (returnRequested || current!=identity || state=="ROUND_RESULT" || state=="FINAL_RESULT" ||
            state=="CANCELLED" || (eliminated && !observer) || (!completed.empty() && completed!=completedKey));
    }
    void finish() {directLevelId=0;active=false;observer=false;returnRequested=false;identity={};completedKey.clear();}
};
}
