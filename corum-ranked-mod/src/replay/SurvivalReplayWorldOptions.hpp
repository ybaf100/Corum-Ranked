#pragma once
#include <matjson.hpp>
#include <string>
namespace corum::ranked::replay {
inline matjson::Value replayWorldOptions(bool lowDetail,bool disableShake){
    matjson::Value value;value["schema"]=1;value["lowDetail"]=lowDetail;value["disableShake"]=disableShake;return value;
}
inline bool replayWorldOptionsValid(matjson::Value const& value){
    return value.isObject()&&value["schema"].asInt().unwrapOr(0)==1&&value["lowDetail"].isBool()&&value["disableShake"].isBool();
}
inline std::string replayWorldOptionsProblem(matjson::Value const& recorded){
    // Legacy files never recorded these options. Do not guess or rewrite them;
    // their existing full map/physics validation remains authoritative.
    return recorded.isNull()||replayWorldOptionsValid(recorded)?"":"INITIAL_WORLD_OPTIONS_INVALID";
}
inline std::string replayWorldOptionsMismatch(matjson::Value const& recorded,bool lowDetail){
    if(auto problem=replayWorldOptionsProblem(recorded);!problem.empty())return problem;
    return !recorded.isNull()&&recorded["lowDetail"].asBool().unwrap()!=lowDetail?"INITIAL_WORLD_OPTIONS_MISMATCH":"";
}
// Native init/reset must see the Source's load options, not current menu
// defaults. Restore the shared level object's preferences on every exit path.
template<class Level> class ReplayWorldOptionsScope final {
    Level* m_level=nullptr;bool m_low=false,m_shake=false;
public:
    ReplayWorldOptionsScope(Level* level,matjson::Value const& recorded){
        if(!level||!replayWorldOptionsValid(recorded))return;
        m_level=level;m_low=level->m_lowDetailModeToggled;m_shake=level->m_disableShakeToggled;
        level->m_lowDetailModeToggled=recorded["lowDetail"].asBool().unwrap();
        level->m_disableShakeToggled=recorded["disableShake"].asBool().unwrap();
    }
    ~ReplayWorldOptionsScope(){if(m_level){m_level->m_lowDetailModeToggled=m_low;m_level->m_disableShakeToggled=m_shake;}}
    ReplayWorldOptionsScope(ReplayWorldOptionsScope const&)=delete;
    ReplayWorldOptionsScope& operator=(ReplayWorldOptionsScope const&)=delete;
};
}
