#pragma once
#include "RankedVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/actions/CCActionEase.h>

namespace corum::ranked::ui {
using namespace geode::prelude;

// Owned by this lobby render only. No native button detours or global hover
// behavior; weak identities retire with the corresponding menu/card nodes.
class LobbyHover final : public CCNode {
    struct Target {
        WeakRef<CCMenuItemSpriteExtra> item;
        WeakRef<CCNode> node;
        float baseScale=1,originalPressScale=1;
        bool card=false,inside=false;
        CCDrawNode* shine=nullptr;
        float shineTime=1;
    };
    std::vector<Target> m_targets;
    bool m_active=true;
    static constexpr int PulseTag=7463;
    static bool visible(CCNode* node) {
        for(auto* p=node;p;p=p->getParent())if(!p->isVisible())return false;
        return node && node->isRunning();
    }
    static CCPoint perimeterPoint(float distance) {
        constexpr float w=188,h=210,length=2*(w+h);
        auto d=std::fmod(distance+length,length);
        if(d<w)return {-94+d,105};
        d-=w;
        if(d<h)return {94,105-d};
        d-=h;
        if(d<w)return {94-d,-105};
        d-=w;
        return {-94,-105+d};
    }
    void pulse(Target& target) {
        auto node=target.node.lock();if(!node)return;
        node->stopActionByTag(PulseTag);
        auto* action=CCSequence::create(CCEaseSineOut::create(CCScaleTo::create(.09f,target.baseScale*1.035f)),
            CCEaseSineInOut::create(CCScaleTo::create(.17f,target.baseScale)),nullptr);
        action->setTag(PulseTag);node->runAction(action);
        if(target.card){
            if(!target.shine){target.shine=CCDrawNode::create();node->addChild(target.shine,50);}
            target.shineTime=0;
        }
    }
    void collectNode(CCNode* node) {
        if(!node)return;
        if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
            CCNode* target=item;bool card=false;
            for(auto* p=item->getParent();p;p=p->getParent())
                if(p->getID()=="survival-profile-card"_spr || p->getID()=="solo-ranked-profile-card"_spr){target=p;card=true;break;}
            m_targets.push_back({WeakRef(item),WeakRef(target),target->getScale(),item->m_scaleMultiplier,card,false,nullptr,1});
            item->m_scaleMultiplier=card?1.0f:1.065f;
        }
        for(auto* child:CCArrayExt<CCNode*>(node->getChildren()))collectNode(child);
    }
public:
    static LobbyHover* create(CCNode* root,CCNode* settings) {
        auto* node=new LobbyHover;if(!node->init()){delete node;return nullptr;}
        node->autorelease();node->collectNode(root);node->collectNode(settings);node->scheduleUpdate();return node;
    }
    void setActive(bool active) {
        m_active=active;
        if(!active)for(auto& target:m_targets){target.inside=false;if(target.node.lock() && target.shine)target.shine->clear();target.shineTime=1;}
    }
    void press(CCMenuItemSpriteExtra* item) {
        for(auto& target:m_targets)if(auto owner=target.item.lock();owner && owner.data()==item && target.card){pulse(target);break;}
    }
    void detach() {
        for(auto& target:m_targets){
            if(auto item=target.item.lock())item->m_scaleMultiplier=target.originalPressScale;
            if(auto node=target.node.lock()){node->stopActionByTag(PulseTag);node->setScale(target.baseScale);if(target.shine)target.shine->clear();}
        }
        m_targets.clear();
    }
    void update(float dt) override {
        if(!m_active)return;
        for(auto& target:m_targets){
            auto item=target.item.lock();auto node=target.node.lock();
            if(!item || !node)continue;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
            auto const point=item->convertToNodeSpace(geode::cocos::getMousePos());
            auto const size=item->getContentSize();
            auto const inside=visible(item.data()) && item->isEnabled() && CCRect{0,0,size.width,size.height}.containsPoint(point);
            if(inside && !target.inside && !item->isSelected())pulse(target);
            target.inside=inside;
#endif
            if(!target.shine || target.shineTime>=.52f)continue;
            target.shineTime+=std::max(0.0f,dt);target.shine->clear();
            if(target.shineTime>=.52f)continue;
            constexpr float length=796;
            auto const head=std::min(1.0f,target.shineTime/.44f)*length;
            auto const fade=std::clamp((.52f-target.shineTime)/.08f,0.0f,1.0f);
            // Small pieces follow corners; the existing frame's thickness is
            // unchanged, with one quick clockwise highlight and no looping.
            for(int i=0;i<22;i++) {
                auto const d=head-i*3.0f;
                auto const a=perimeterPoint(d),b=perimeterPoint(d+2.5f);
                target.shine->drawSegment(a,b,.45f,{1,.90f,.45f,fade*(1-i/22.0f)});
            }
        }
    }
};
}
