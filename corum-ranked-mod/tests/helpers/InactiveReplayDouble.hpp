#pragma once
#include <string>
// Inactive replay boundary for pre-existing Solo/ordinary native fixtures.
// Active recorder/storage/timeline and Survival admission have dedicated production-path tests.
namespace corum::ranked::replay {
struct InactiveWorld {template<class...T>void followRemote(T const&...){}bool seeking()const{return false;}bool desync()const{return false;}bool partial()const{return false;}};
struct EmptyContext {bool valid()const{return false;}template<class T>operator T()const{return {};}};
struct Runtime {static Runtime&get(){static Runtime r;return r;}bool owns(int)const{return false;}bool canLaunch()const{return false;}void pump(){}void exited(){}EmptyContext context()const{return {};}template<class T>bool arm(T*)const{return false;}};
struct Recorder {static Recorder&get(){static Recorder r;return r;}template<class...T>void finish(T const&...){}template<class...T>void accepted(T const&...){}template<class...T>void officialEnd(T const&...){}bool hasPendingClose(std::string const&)const{return false;}bool recovering()const{return false;}void pump(){}template<class...T>void finishFromLast(T const&...){}};
}

namespace corum::ranked {template<class T>void finishSurvivalReplayForReturn(T*,std::string={},bool=false){}}

namespace corum::ranked {template<class T>void survivalReplayNativeSnap(T*,bool,float,float){}}

namespace corum::ranked {template<class T>void survivalReplayPlayerStep(T*,float,bool){}}
