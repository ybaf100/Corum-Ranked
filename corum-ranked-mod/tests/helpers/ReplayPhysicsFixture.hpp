#pragma once
#include "../../src/replay/SurvivalReplayCodec.hpp"
namespace corum::ranked::replay {
// Schema-valid input data for contract tests; this does not simulate GD physics.
inline Json initialReplayPhysicsFixture(){
    Json initial;initial["kind"]="initial";initial["randomSeed"]="1";initial["replayRandSeed"]="2";initial["physicsSchema"]=2;
    Json state;state["schema"]=2;state["refs"]=Json::object();state["platformFields"]="DESKTOP_CBS";
#define FIELD(type,name) state[#name]=replayPhysicsValue(static_cast<type>(0));
    CORUM_REPLAY_PHYSICS_SCALARS(FIELD)
#undef FIELD
#define POINT(type,name) state[#name]=Json::array();state[#name].push(0.);state[#name].push(0.);
    CORUM_REPLAY_PHYSICS_POINTS(POINT)
#undef POINT
    state["holdingButtons"]=Json::array();state["position"]=Json::array();state["position"].push(0.);state["position"].push(0.);
    state["scale"]=Json::array();state["scale"].push(1.);state["scale"].push(1.);state["rotation"]=0.;
    initial["player1"]=state;initial["player2"]=nullptr;initial["queued"]=Json::array();initial["nativeStep"]=0;initial["resumeTimer"]=0;
    for(auto key:{"started","inResetDelay","dual","clickBetweenSteps","clickOnSteps","isBetweenSteps"})initial[key]=false;
    for(auto key:{"attemptTime","levelTime","extraDelta","timestamp"})initial[key]=0.;
    initial["timeWarp"]=1.;return initial;
}
}
