#include "SurvivalOptionsPreferences.hpp"
#include "domain/SurvivalNativeOptions.hpp"
#include <Geode/Geode.hpp>
using namespace geode::prelude;
namespace corum::ranked {
SurvivalOptionsPreferences& SurvivalOptionsPreferences::get(){static SurvivalOptionsPreferences value;return value;}
SurvivalOptionsPreferences::SurvivalOptionsPreferences(){
    auto* mod=Mod::get();
    if(!mod->getSavedValue<bool>("survival-defaults-migrated",false)){
        mod->setSavedValue("survival-default-ldm",mod->getSavedValue<bool>("survival-low-detail",false));
        mod->setSavedValue("survival-default-shake",mod->getSavedValue<bool>("survival-disable-shake",false));
        mod->setSavedValue("survival-defaults-migrated",true);
    }
    m_thumbnails=mod->getSavedValue<bool>("survival-image-thumbnails",true);
    m_selectionMotion=mod->getSavedValue<bool>("survival-selection-motion",true);
    m_defaults={mod->getSavedValue<bool>("survival-default-ldm",false),mod->getSavedValue<bool>("survival-default-shake",false)};
}
void SurvivalOptionsPreferences::setDefaultLowDetail(bool value){m_defaults.lowDetail=value;Mod::get()->setSavedValue("survival-default-ldm",value);}
void SurvivalOptionsPreferences::setDefaultDisableShake(bool value){m_defaults.disableShake=value;Mod::get()->setSavedValue("survival-default-shake",value);}
void SurvivalOptionsPreferences::setThumbnails(bool value){m_thumbnails=value;Mod::get()->setSavedValue("survival-image-thumbnails",value);}
void SurvivalOptionsPreferences::setSelectionMotion(bool value){m_selectionMotion=value;Mod::get()->setSavedValue("survival-selection-motion",value);}
void SurvivalOptionsPreferences::apply(GJGameLevel* level,std::string const& context){
    if(context.empty())return;
    auto options=current(context);applySurvivalNativeOptions(level,options.lowDetail,options.disableShake);
}
}
