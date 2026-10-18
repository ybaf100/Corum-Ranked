#pragma once
#include "../domain/SurvivalHudPresentation.hpp"
#include "RankedConfirmPopup.hpp"
#include <Geode/binding/ProfilePage.hpp>
namespace corum::ranked::ui {
inline void openSurvivalProfile(cocos2d::CCNode* owner,SurvivalStanding const& player){
    using namespace geode::prelude;
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(!owner || !owner->getParent() || !scene || CCDirector::sharedDirector()->getNextScene() || scene->getChildByIDRecursive("survival-gd-profile"_spr))return;
    if(player.bot || player.accountId<=0){
        if(scene->getChildByIDRecursive("survival-no-gd-profile"_spr))return;
        if(auto* popup=RankedConfirmPopup::create("Geometry Dash",i18n::text("survival-profile-none","No GD profile"),[](bool){})){
            popup->setID("survival-no-gd-profile"_spr);popup->show();
        }return;
    }
    if(auto* profile=ProfilePage::create(player.accountId,false)){
        profile->setID("survival-gd-profile"_spr);profile->show();
    }
}
}
