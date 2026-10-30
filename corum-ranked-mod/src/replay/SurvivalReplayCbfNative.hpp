#pragma once
#include <matjson.hpp>
#include <string>
class PlayLayer;
namespace corum::ranked::replay {
class Playback;
// Source capture is read-only. Physics hooks are leased only while a replay
// owns a scene; no setting value or official source-gameplay hook is changed.
matjson::Value cbfNativeCapability();
std::string acquireCbfReplayHooks(matjson::Value const& environment);
void releaseCbfReplayHooks();
bool cbfReplayHooksHeld();
void beginCbfNativeFrame(PlayLayer*,Playback* replay=nullptr);
matjson::Value finishCbfNativeFrame();
std::string cbfNativeFrameError();
matjson::Value cbfNativeUpdateCounts();
matjson::Value captureCbfInitial(PlayLayer*);
std::string validateCbfInitial(PlayLayer*,matjson::Value const&);
void restoreCbfInitial(PlayLayer*,matjson::Value const&);
}
