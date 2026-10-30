#pragma once
#include "SurvivalReplayCompatibility.hpp"
#include "SurvivalReplayCbfNative.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <string_view>
#include <utility>
namespace corum::ranked::replay {
inline matjson::Value captureReplayPhysicsEnvironment(){
    auto environment=matjson::Value::object();environment["schema"]=2;
    auto cbf=matjson::Value::object();cbf["loaded"]=false;
    auto mods=matjson::Value::array();
    for(auto* mod:geode::Loader::get()->getAllMods())if(mod&&mod->isLoaded()){
        auto item=matjson::Value::object();item["id"]=std::string(mod->getID());item["version"]=mod->getVersion().toVString();mods.push(item);
        if(mod->getID()!="syzzi.click_between_frames")continue;
        cbf["loaded"]=true;cbf["version"]=mod->getVersion().toVString();
        if(cbf["version"].asString().unwrapOr("")!="v1.5.0")continue;
        for(auto const& [key,label]:{std::pair{"soft-toggle","disabled"},std::pair{"click-on-steps","clickOnSteps"},std::pair{"physics-bypass","physicsBypass"},std::pair{"precision-fix","precisionFix"}}){
            if(mod->getSetting(key))cbf[label]=mod->getSettingValue<bool>(key);
#if !defined(GEODE_IS_WINDOWS)
            else if(std::string_view(key)=="physics-bypass"||std::string_view(key)=="precision-fix")cbf[label]=false;
#endif
        }
    }
    if(cbf["loaded"].asBool().unwrapOr(false)&&!cbf["disabled"].asBool().unwrapOr(true))cbf["nativeBoundary"]=cbfNativeCapability();
    environment["cbf"]=std::move(cbf);environment["mods"]=std::move(mods);
    environment["driver"]="GD_2_2081_NATIVE_FRAME_V2";
    return environment;
}
}
