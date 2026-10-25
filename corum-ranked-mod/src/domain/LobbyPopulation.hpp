#pragma once
#include <matjson.hpp>
namespace corum::ranked {
struct LobbyPopulation {int offline=0,online=0;bool known=false;bool operator==(LobbyPopulation const&)const=default;};
inline LobbyPopulation parseLobbyPopulation(matjson::Value const& value){
    auto const offline=value["offline"].asInt(),online=value["online"].asInt();
    if(offline.isErr()||online.isErr()||offline.unwrap()<0||online.unwrap()<0||offline.unwrap()>2147483647||online.unwrap()>2147483647)return {};
    return {static_cast<int>(offline.unwrap()),static_cast<int>(online.unwrap()),true};
}
}
