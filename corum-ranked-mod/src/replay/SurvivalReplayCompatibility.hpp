#pragma once
#include <matjson.hpp>
#include <string>
#include <algorithm>
namespace corum::ranked::replay {
inline std::string replayPhysicsEnvironmentProblem(matjson::Value const& environment){
    if(environment["schema"].asInt().unwrapOr(0)!=2)return "LEGACY_PHYSICS_ENVIRONMENT_UNAVAILABLE";
    auto const& cbf=environment["cbf"];
    if(!cbf["loaded"].isBool())return "PHYSICS_ENVIRONMENT_INCOMPLETE";
    if(!cbf["loaded"].asBool().unwrap())return {};
    if(cbf["version"].asString().unwrapOr("")!="v1.5.0")return "CBF_VERSION_UNVERIFIED";
    for(auto key:{"disabled","clickOnSteps","physicsBypass"})if(!cbf[key].isBool())return "CBF_SETTINGS_UNAVAILABLE";
    if(cbf["physicsBypass"].asBool().unwrap())return "CBF_PHYSICS_BYPASS_UNSUPPORTED";
    if(!cbf["disabled"].asBool().unwrap()){
        auto const& boundary=cbf["nativeBoundary"];
        auto hash=boundary["binarySha256"].asString().unwrapOr("");
        auto platform=boundary["platform"].asString().unwrapOr("");
        if(boundary["driver"].asInt().unwrapOr(0)!=1||!boundary["verified"].asBool().unwrapOr(false)||hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||(platform!="win"&&platform!="mac"&&platform!="ios"&&platform!="android"))return "CBF_NATIVE_BOUNDARIES_UNAVAILABLE";
        if(!cbf["precisionFix"].isBool())return "CBF_SETTINGS_UNAVAILABLE";
    }
    if(cbf["clickOnSteps"].asBool().unwrap())return "CBF_COS_PIPELINE_UNSUPPORTED";
    return {};
}
inline bool replayCbfActive(matjson::Value const& environment){return environment["cbf"]["loaded"].asBool().unwrapOr(false)&&!environment["cbf"]["disabled"].asBool().unwrapOr(true);}
inline std::string replayPhysicsEnvironmentMismatch(matjson::Value const& source,matjson::Value const& current){
    if(auto p=replayPhysicsEnvironmentProblem(source);!p.empty())return p;
    if(auto p=replayPhysicsEnvironmentProblem(current);!p.empty())return p;
    if(replayCbfActive(source)!=replayCbfActive(current))return "CBF_ACCURACY_MODE_MISMATCH";
    if(replayCbfActive(source))for(auto key:{"version","precisionFix"})if(source["cbf"][key]!=current["cbf"][key])return "CBF_SETTINGS_MISMATCH";
    if(replayCbfActive(source))for(auto key:{"platform","architecture","binarySha256","driver"})if(source["cbf"]["nativeBoundary"][key]!=current["cbf"]["nativeBoundary"][key])return "CBF_NATIVE_DRIVER_MISMATCH";
    return {};
}
}
