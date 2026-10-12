#pragma once
#include "RankedVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/actions/CCActionEase.h>

namespace corum::ranked::ui {
using namespace geode::prelude;

inline CCPoint lobbyCardPerimeter(float distance) {
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
inline void drawLobbyCardShine(CCDrawNode* node,float elapsed) {
    node->clear();if(elapsed>=.76f)return;
    auto const head=std::min(1.0f,elapsed/.66f)*796;
    auto const fade=std::clamp((.76f-elapsed)/.10f,0.0f,1.0f);
    for(int i=0;i<22;i++)node->drawSegment(lobbyCardPerimeter(head-i*3.0f),lobbyCardPerimeter(head-i*3.0f+2.5f),
        .45f,{1,.90f,.45f,fade*(1-i/22.0f)});
}
// Tap feedback owns a card action, not the hover controller's input lifetime:
// it finishes once even when the card immediately begins its channel slide.
class LobbyCardShine final : public CCActionInterval {
    CCDrawNode* m_node=nullptr;
public:
    static LobbyCardShine* create(){auto* action=new LobbyCardShine;if(!action->initWithDuration(.76f)){delete action;return nullptr;}action->autorelease();return action;}
    void startWithTarget(CCNode* target) override {CCActionInterval::startWithTarget(target);m_node=dynamic_cast<CCDrawNode*>(target);}
    void update(float progress) override {if(m_node)drawLobbyCardShine(m_node,.76f*std::clamp(progress,0.0f,1.0f));}
};

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
    bool m_cardsSelectable=false;
    static constexpr int PulseTag=7463;
    static bool visible(CCNode* node) {
        for(auto* p=node;p;p=p->getParent())if(!p->isVisible())return false;
        return node && node->isRunning();
    }
    void pulse(Target& target, bool hold=false) {
        auto node=target.node.lock();if(!node)return;
        node->stopActionByTag(PulseTag);
        if(hold) {
            auto* action=CCEaseSineOut::create(CCScaleTo::create(.12f,target.baseScale*1.035f));
            action->setTag(PulseTag);node->runAction(action);
        } else {
            auto* action=CCSequence::create(CCEaseSineOut::create(CCScaleTo::create(.09f,target.baseScale*1.035f)),
                CCEaseSineInOut::create(CCScaleTo::create(.17f,target.baseScale)),nullptr);
            action->setTag(PulseTag);node->runAction(action);
        }
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
            if(!card || m_cardsSelectable) {
                m_targets.push_back({WeakRef(item),WeakRef(target),target->getScale(),item->m_scaleMultiplier,card,false,nullptr,1});
                item->m_scaleMultiplier=card?1.0f:1.065f;
            }
        }
        for(auto* child:CCArrayExt<CCNode*>(node->getChildren()))collectNode(child);
    }
public:
    static LobbyHover* create(CCNode* root,CCNode* settings,bool cardsSelectable) {
        auto* node=new LobbyHover;if(!node->init()){delete node;return nullptr;}
        node->m_cardsSelectable=cardsSelectable;
        node->autorelease();node->collectNode(root);node->collectNode(settings);node->scheduleUpdate();return node;
    }
    void setActive(bool active) {
        m_active=active;
        if(!active)for(auto& target:m_targets){
            if(target.inside && target.card)restore(target);
            target.inside=false;if(target.node.lock() && target.shine)target.shine->clear();target.shineTime=1;
        }
    }
    void press(CCMenuItemSpriteExtra* item) {
        if(!m_active || !m_cardsSelectable)return;
        for(auto& target:m_targets)if(auto owner=target.item.lock();owner && owner.data()==item && target.card){
            auto node=target.node.lock();if(!node)break;pulse(target,target.inside);
            if(target.shine)target.shine->clear();
            target.shineTime=1;
            auto* shine=CCDrawNode::create();node->addChild(shine,51);drawLobbyCardShine(shine,0);
            shine->runAction(CCSequence::create(LobbyCardShine::create(),CallFuncExt::create([weak=WeakRef(shine)]{
                if(auto node=weak.lock())node->removeFromParentAndCleanup(true);
            }),nullptr));break;
        }
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
            if(inside && !target.inside && !item->isSelected())pulse(target,target.card);
            if(!inside && target.inside && target.card)restore(target);
            target.inside=inside;
#endif
            if(!target.shine || target.shineTime>=.76f)continue;
            target.shineTime+=std::max(0.0f,dt);
            // Small pieces follow corners; the existing frame's thickness is
            // unchanged, with one quick clockwise highlight and no looping.
            drawLobbyCardShine(target.shine,target.shineTime);
        }
    }
private:
    void restore(Target& target) {
        if(auto node=target.node.lock()) {
            node->stopActionByTag(PulseTag);
            auto* action=CCEaseSineInOut::create(CCScaleTo::create(.16f,target.baseScale));
            action->setTag(PulseTag);node->runAction(action);
        }
    }
};
}
