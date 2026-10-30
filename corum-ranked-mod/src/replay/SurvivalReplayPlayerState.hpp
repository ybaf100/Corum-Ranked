#pragma once
#include "SurvivalReplayPhysicsFields.hpp"
namespace corum::ranked::replay {
using PhysicsJson=matjson::Value;
inline constexpr int ReplayPhysicsSchema=2;
template<class T> PhysicsJson replayPhysicsValue(T value){
    if constexpr(std::is_same_v<T,bool>)return PhysicsJson(value);
    else if constexpr(std::is_integral_v<T>)return PhysicsJson(static_cast<std::int64_t>(value));
    else return PhysicsJson(static_cast<double>(value));
}
template<class T> bool replayPhysicsFits(PhysicsJson const& value){
    if constexpr(std::is_same_v<T,bool>)return value.isBool();
    else if constexpr(std::is_integral_v<T>){auto n=value.asInt();return n.isOk()&&n.unwrap()>=std::numeric_limits<T>::lowest()&&n.unwrap()<=std::numeric_limits<T>::max();}
    else {auto n=value.asDouble();return n.isOk()&&std::isfinite(n.unwrap())&&n.unwrap()>=std::numeric_limits<T>::lowest()&&n.unwrap()<=std::numeric_limits<T>::max();}
}
template<class T> T replayPhysicsRead(PhysicsJson const& value){
    if constexpr(std::is_same_v<T,bool>)return value.asBool().unwrap();
    else if constexpr(std::is_integral_v<T>)return static_cast<T>(value.asInt().unwrap());
    else return static_cast<T>(value.asDouble().unwrap());
}
inline bool replayPhysicsPointValid(PhysicsJson const& value){
    auto items=value.asArray();return items.isOk()&&items.unwrap().size()==2&&replayPhysicsFits<float>(items.unwrap()[0])&&replayPhysicsFits<float>(items.unwrap()[1]);
}
template<class Player> PhysicsJson captureReplayPlayerState(Player* player,PhysicsJson refs){
    if(!player)return nullptr;
    PhysicsJson data=PhysicsJson::object();data["schema"]=ReplayPhysicsSchema;data["refs"]=std::move(refs);
#define CORUM_CAPTURE_SCALAR(type,name) data[#name]=replayPhysicsValue(player->name);
    CORUM_REPLAY_PHYSICS_SCALARS(CORUM_CAPTURE_SCALAR)
    CORUM_REPLAY_PHYSICS_PLATFORM(CORUM_CAPTURE_SCALAR)
#undef CORUM_CAPTURE_SCALAR
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    data["platformFields"]="MOBILE_CBS";
#else
    data["platformFields"]="DESKTOP_CBS";
#endif
#define CORUM_CAPTURE_POINT(type,name) {auto p=PhysicsJson::array();p.push(static_cast<double>(player->name.x));p.push(static_cast<double>(player->name.y));data[#name]=std::move(p);}
    CORUM_REPLAY_PHYSICS_POINTS(CORUM_CAPTURE_POINT)
#undef CORUM_CAPTURE_POINT
    auto position=PhysicsJson::array();position.push(static_cast<double>(player->getPositionX()));position.push(static_cast<double>(player->getPositionY()));data["position"]=std::move(position);
    auto scale=PhysicsJson::array();scale.push(static_cast<double>(player->getScaleX()));scale.push(static_cast<double>(player->getScaleY()));data["scale"]=std::move(scale);
    data["rotation"]=static_cast<double>(player->getRotation());
    auto holds=PhysicsJson::array();for(auto const& [button,down]:player->m_holdingButtons){auto h=PhysicsJson::array();h.push(button);h.push(down);holds.push(std::move(h));}data["holdingButtons"]=std::move(holds);
    return data;
}
inline std::string replayPlayerDataProblem(PhysicsJson const& data){
    if(!data.isObject()||data["schema"].asInt().unwrapOr(0)!=ReplayPhysicsSchema)return "INITIAL_PHYSICS_UNAVAILABLE";
    if(!data["refs"].isObject()||data["refs"]["unresolved"].asBool().unwrapOr(false))return "INITIAL_WORLD_REFERENCE_MISMATCH";
    auto const platform=data["platformFields"].asString().unwrapOr("");
    if(platform!="MOBILE_CBS"&&platform!="DESKTOP_CBS")return "INITIAL_PLATFORM_UNAVAILABLE";
    if(platform=="MOBILE_CBS"&&!data["m_collidingBetweenSteps"].isBool())return "INITIAL_MOBILE_CBS_UNAVAILABLE";
#define CORUM_CHECK_SCALAR(type,name) if(!replayPhysicsFits<type>(data[#name]))return #name;
    CORUM_REPLAY_PHYSICS_SCALARS(CORUM_CHECK_SCALAR)
#undef CORUM_CHECK_SCALAR
#define CORUM_CHECK_POINT(type,name) if(!replayPhysicsPointValid(data[#name]))return #name;
    CORUM_REPLAY_PHYSICS_POINTS(CORUM_CHECK_POINT)
#undef CORUM_CHECK_POINT
    if(!replayPhysicsPointValid(data["position"])||!replayPhysicsPointValid(data["scale"])||!replayPhysicsFits<float>(data["rotation"]))return "INITIAL_TRANSFORM_INVALID";
    auto holds=data["holdingButtons"].asArray();if(holds.isErr()||holds.unwrap().size()>4)return "INITIAL_HOLDS_INVALID";
    std::array<bool,4> seen{};
    for(auto const& h:holds.unwrap()){
        auto pair=h.asArray();if(pair.isErr()||pair.unwrap().size()!=2)return "INITIAL_HOLDS_INVALID";
        auto button=pair.unwrap()[0].asInt();if(button.isErr()||button.unwrap()<0||button.unwrap()>3||seen[button.unwrap()]||!pair.unwrap()[1].isBool())return "INITIAL_HOLDS_INVALID";
        seen[button.unwrap()]=true;
    }
    return {};
}
// Validate both players and their source-world references before applying either
// state. Modes must already agree with the verified map's native initialization.
template<class Player> std::string replayPlayerStateProblem(Player* player,PhysicsJson const& data,PhysicsJson const& refs){
    if(!player)return data.isNull()?std::string{}:"PLAYER_MISSING";
    if(auto problem=replayPlayerDataProblem(data);!problem.empty())return problem;
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    if(data["platformFields"].asString().unwrapOr("")!="MOBILE_CBS")return "INITIAL_PLATFORM_MISMATCH";
#else
    if(data["platformFields"].asString().unwrapOr("")!="DESKTOP_CBS")return "INITIAL_PLATFORM_MISMATCH";
#endif
    if(data["refs"]!=refs)return "INITIAL_WORLD_REFERENCE_MISMATCH";
    auto const native=captureReplayPlayerState(player,refs);
    for(auto key:{"m_isShip","m_isBall","m_isBird","m_isDart","m_isRobot","m_isSpider","m_isSwing","m_isSideways","m_isPlatformer"})
        if(native[key]!=data[key])return "INITIAL_MODE_MISMATCH";
    return {};
}
template<class Player> void applyReplayPlayerState(Player* player,PhysicsJson const& data){
    if(!player)return;
    auto const position=data["position"].asArray().unwrap(),scale=data["scale"].asArray().unwrap();
    player->setPosition({static_cast<float>(position[0].asDouble().unwrap()),static_cast<float>(position[1].asDouble().unwrap())});
    player->setRotation(static_cast<float>(data["rotation"].asDouble().unwrap()));
    player->setScaleX(static_cast<float>(scale[0].asDouble().unwrap()));player->setScaleY(static_cast<float>(scale[1].asDouble().unwrap()));
#define CORUM_APPLY_SCALAR(type,name) player->name=replayPhysicsRead<type>(data[#name]);
    CORUM_REPLAY_PHYSICS_SCALARS(CORUM_APPLY_SCALAR)
    CORUM_REPLAY_PHYSICS_PLATFORM(CORUM_APPLY_SCALAR)
#undef CORUM_APPLY_SCALAR
#define CORUM_APPLY_POINT(type,name) {auto p=data[#name].asArray().unwrap();player->name={static_cast<float>(p[0].asDouble().unwrap()),static_cast<float>(p[1].asDouble().unwrap())};}
    CORUM_REPLAY_PHYSICS_POINTS(CORUM_APPLY_POINT)
#undef CORUM_APPLY_POINT
    auto const holds=data["holdingButtons"].asArray().unwrap();player->m_holdingButtons.clear();for(auto const& h:holds){auto pair=h.asArray().unwrap();player->m_holdingButtons[static_cast<int>(pair[0].asInt().unwrap())]=pair[1].asBool().unwrap();}
}
}
