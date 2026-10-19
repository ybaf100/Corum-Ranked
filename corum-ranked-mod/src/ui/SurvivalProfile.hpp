#pragma once
#include "SurvivalPlayerInfoPopup.hpp"
namespace corum::ranked::ui {
inline void openSurvivalProfile(cocos2d::CCNode* owner,SurvivalStanding const& player){
    using namespace geode::prelude;
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(!owner || !owner->getParent() || !scene || CCDirector::sharedDirector()->getNextScene() || scene->getChildByIDRecursive("survival-player-info-popup"_spr))return;
    if(auto* popup=SurvivalPlayerInfoPopup::create(player,SurvivalRuntime::get().view().id))popup->show();
}
}
