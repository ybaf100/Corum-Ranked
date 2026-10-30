#pragma once
#include "SurvivalReplayCodec.hpp"
namespace corum::ranked::replay {
// Commands already owned by the native world survive across render frames.
// Only the newly recorded suffix is injected, before the native scheduler.
template<class Commands> std::string prepareReplayInputBoundary(Commands& actual,Json const& recorded,Json const& carry){
    if(!replayButtonQueueValid(recorded)||!replayButtonQueueValid(carry))return "FRAME_INPUT_DATA";
    auto const source=recorded.asArray().unwrap(),pending=carry.asArray().unwrap();
    auto const live=encodeReplayButtons(actual).asArray().unwrap();
    if(source.size()<pending.size()||!std::equal(pending.begin(),pending.end(),source.begin()))return "SOURCE_PENDING_INPUT_LOST";
    if(live.size()<pending.size()||!std::equal(pending.begin(),pending.end(),live.begin()))return "NATIVE_PENDING_INPUT_LOST";
    // An unrecorded viewer click is not part of the tape. Never clear carry.
    actual.resize(pending.size());
    for(auto it=source.begin()+pending.size();it!=source.end();++it)actual.push_back(decodeReplayButton<typename Commands::value_type>(*it));
    return {};
}
}
