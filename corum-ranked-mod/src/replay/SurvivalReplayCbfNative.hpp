#pragma once
#include <matjson.hpp>
#include <string>
#include <cstdint>
#include <cstddef>
class PlayLayer;
class PlayerObject;
class GameObject;
namespace corum::ranked::replay {
class Playback;
// Source capture is read-only. Physics hooks are leased only while a replay
// owns a scene; no setting value or official source-gameplay hook is changed.
matjson::Value cbfNativeCapability();
std::string acquireCbfReplayHooks(matjson::Value const& environment);
void releaseCbfReplayHooks();
bool cbfReplayHooksHeld();
void beginCbfNativeFrame(PlayLayer*,Playback* replay=nullptr);
void noteCbfNativeCollisionCandidates(PlayLayer*,PlayerObject*,GameObject* const*,std::size_t);
matjson::Value finishCbfNativeFrame();
std::string cbfNativeFrameError();
matjson::Value cbfNativeUpdateCounts();
matjson::Value captureCbfInitial(PlayLayer*,bool newSourceAttempt=false);
std::string validateCbfInitial(PlayLayer*,matjson::Value const&);
std::string prepareCbfInitialCollisionLogs(PlayLayer*,matjson::Value const&,bool nativeResetOwned);
void restoreCbfInitial(PlayLayer*,matjson::Value const&);
// The collision state a Native reset kept from the previous attempt (GD clears
// it only for a fresh PlayLayer): last targets and collision log entries, as
// map references; null when one cannot be identified. Call after
// captureCbfInitial(layer,true).
matjson::Value captureCbfInitialCollisions(PlayLayer*);
bool cbfInitialLogsKept(matjson::Value const& cbfInitial);
// Playback: sets those targets on the players in this world (after the
// player snapshot); returns a problem when one cannot be resolved.
std::string restoreCbfInitialCollisions(PlayLayer*,matjson::Value const&);
void beginNativeInputClock(PlayLayer*,Playback* replay=nullptr);
std::size_t enterNativeInputClock(char const* phase,double& timestamp);
void leaveNativeInputClock(std::size_t index,double& timestamp,std::uint64_t generation=0);
std::uint64_t nativeInputClockGeneration();
void retireNativeReplayFrame(PlayLayer*);
matjson::Value finishNativeInputClock();
matjson::Value nativeInputClockDiagnostics();
std::string nativeInputClockError();
class NativeInputClockScope {
    double& m_timestamp;std::uint64_t m_generation;std::size_t m_index;
public:
    NativeInputClockScope(char const* phase,double& timestamp):m_timestamp(timestamp),m_generation(nativeInputClockGeneration()),m_index(enterNativeInputClock(phase,timestamp)){}
    ~NativeInputClockScope(){leaveNativeInputClock(m_index,m_timestamp,m_generation);}
};
}
