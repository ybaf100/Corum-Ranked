#pragma once
#include <Geode/Geode.hpp>
#include <Geode/cocos/actions/CCActionEase.h>

namespace corum::ranked::ui {
// GD paints CCLayerColor chrome from its own opacity. Fade those renderable
// layers explicitly as well as inheriting the content group's text/icon fade.
inline void fadeModeLobbySurfaces(cocos2d::CCNode* root,float seconds,bool reveal) {
    if(!root)return;
    for(auto* child:geode::cocos::CCArrayExt<cocos2d::CCNode*>(root->getChildren())) {
        if(auto* layer=dynamic_cast<cocos2d::CCLayerColor*>(child)) {
            auto const size=layer->getContentSize();
            if(size.width>0 && size.height>0) {
                auto const opacity=layer->getOpacity();
                if(reveal)layer->setOpacity(0);
                auto* fade=cocos2d::CCFadeTo::create(seconds,reveal?opacity:0);
                if(reveal)layer->runAction(cocos2d::CCEaseSineOut::create(fade));
                else layer->runAction(cocos2d::CCEaseSineIn::create(fade));
            }
        }
        fadeModeLobbySurfaces(child,seconds,reveal);
    }
}
}
