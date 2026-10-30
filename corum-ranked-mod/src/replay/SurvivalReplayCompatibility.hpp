#pragma once
#include <matjson.hpp>
#include <string>
namespace corum::ranked::replay {
inline std::string replayPhysicsEnvironmentProblem(matjson::Value const& environment){
    if(environment["schema"].asInt().unwrapOr(0)!=2)return "LEGACY_PHYSICS_ENVIRONMENT_UNAVAILABLE";
    auto const& cbf=environment["cbf"];
    if(!cbf["loaded"].isBool())return "PHYSICS_ENVIRONMENT_INCOMPLETE";
    if(!cbf["loaded"].asBool().unwrap())return {};
    if(cbf["version"].asString().unwrapOr("")!="v1.5.0")return "CBF_VERSION_UNVERIFIED";
    for(auto key:{"disabled","clickOnSteps","physicsBypass"})if(!cbf[key].isBool())return "CBF_SETTINGS_UNAVAILABLE";
    if(cbf["physicsBypass"].asBool().unwrap())return "CBF_PHYSICS_BYPASS_UNSUPPORTED";
    if(!cbf["disabled"].asBool().unwrap())return "CBF_ACTIVE_TIMING_UNSUPPORTED";
    if(cbf["clickOnSteps"].asBool().unwrap())return "CBF_COS_PIPELINE_UNSUPPORTED";
    return {};
}
}
