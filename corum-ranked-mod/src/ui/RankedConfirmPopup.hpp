#pragma once
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedInput.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>
#include <array>

namespace corum::ranked::ui {
using namespace geode::prelude;
// The native Pause touch delegate can consume a confirmation menu's tap on
// iPadOS even while the popup is visible. Route only the exact two button
// rectangles before game input; do not consume touches outside the popup.
#if !defined(CORUM_RANKED_NATIVE_TEST)
class RankedConfirmTouchProxy final : public cocos2d::CCLayer {
    std::array<geode::WeakRef<CCMenuItemSpriteExtra>,2> m_buttons;
    std::function<void(bool)> m_select;
    int m_touch=-1,m_selected=-1;
    bool contains(int i,cocos2d::CCTouch* touch)const {
        auto item=m_buttons[i].lock();
        if(!item || !item->isVisible() || !item->isEnabled())return false;
        for(auto* n=static_cast<cocos2d::CCNode*>(item.data());n;n=n->getParent())
            if(!n->isVisible())return false;
        auto const local=item->convertToNodeSpace(touch->getLocation());
        auto const size=item->getContentSize();
        return size.width>0 && size.height>0 &&
            cocos2d::CCRect(0,0,size.width,size.height).containsPoint(local);
    }
public:
    static RankedConfirmTouchProxy* create(CCMenuItemSpriteExtra* cancel,CCMenuItemSpriteExtra* confirm,
        std::function<void(bool)> callback){
        auto* input=new RankedConfirmTouchProxy;
        if(!input->init()){delete input;return nullptr;}
        input->m_buttons={geode::WeakRef(cancel),geode::WeakRef(confirm)};
        input->m_select=std::move(callback);
        input->setTouchEnabled(true);input->autorelease();return input;
    }
    void registerWithTouchDispatcher() override {
        cocos2d::CCDirector::sharedDirector()->getTouchDispatcher()->addPrioTargetedDelegate(this,-65000,true);
    }
    bool ccTouchBegan(cocos2d::CCTouch* touch,cocos2d::CCEvent*) override {
        if(m_touch!=-1)return false;
        for(int i=0;i<2;++i)if(contains(i,touch)){
            m_touch=touch->getID();m_selected=i;
            log::info("[RankedConfirmInput] began choice={} touch={}",i,m_touch);
            return true;
        }
        return false;
    }
    void ccTouchEnded(cocos2d::CCTouch* touch,cocos2d::CCEvent*) override {
        if(touch->getID()!=m_touch)return;
        auto const choice=m_selected;
        auto const accepted=choice>=0 && contains(choice,touch);
        m_touch=m_selected=-1;
        log::info("[RankedConfirmInput] ended choice={} accepted={}",choice,accepted);
        if(accepted && m_select)m_select(choice==1);
    }
    void ccTouchMoved(cocos2d::CCTouch*,cocos2d::CCEvent*) override {}
    void ccTouchCancelled(cocos2d::CCTouch* touch,cocos2d::CCEvent*) override {
        if(touch->getID()==m_touch)m_touch=m_selected=-1;
    }
    void onExit() override {m_touch=m_selected=-1;cocos2d::CCLayer::onExit();}
};
#endif

class RankedConfirmPopup final : public Popup {
    std::function<void(bool)> m_done;
    bool m_closed=false,m_exited=false;
    bool init(std::string const& title,std::string const& message,std::function<void(bool)> done,std::string const& confirm) {
        if(!Popup::init(340,190)) { return false; }
        m_done=std::move(done);m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);
        auto* panel=surface({340,190},{170,95},Gold,Surface,255,true);m_mainLayer->addChild(panel);
        auto* heading=menuLabel(title,.38f,{170,160},Gold,FontWeight::SemiBold);fitMenuLabelWidth(heading,310,.38f,.26f);panel->addChild(heading,5);
        auto* detail=menuLabel(message,.28f,{170,106},Text,FontWeight::SemiBold);fitFullMenuLabel(detail,310,64,.28f);panel->addChild(detail,5);
        // Popup owns modal touch priority for this menu. A separate nested menu
        // can remain behind the native Pause swallowing delegate on iOS.
        auto* menu=m_buttonMenu;
        std::array<CCMenuItemSpriteExtra*,2> buttons{};
        for(int i=0;i<2;i++) {
            auto* face=surface({140,32},{},i?Gold:Blue,SurfaceRaised,255,true);
            face->addChild(menuLabel(i && !confirm.empty()?confirm:i18n::text(i?"confirm":"cancel",i?"CONFIRM":"CANCEL"),.30f,{70,16},i?Gold:Text,FontWeight::SemiBold),5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedConfirmPopup::onChoice));item->setTag(i);item->setPosition({i?248.0f:92.0f,38});menu->addChild(item);buttons[i]=item;
        }
#if !defined(CORUM_RANKED_NATIVE_TEST)
        if(auto* input=RankedConfirmTouchProxy::create(buttons[0],buttons[1],[weak=WeakRef(this)](bool accepted){
            if(auto popup=weak.lock())popup->finish(accepted);
        }))m_mainLayer->addChild(input,90);
#endif
        return true;
    }
    void onChoice(CCObject* sender) {auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        log::info("[RankedConfirmInput] native choice tag={}",item?item->getTag():-1);
        finish(item&&item->getTag()==1);}
    void finish(bool accepted) {
        if(m_closed) { return; }
        log::info("[RankedConfirmInput] finish accepted={}",accepted);
        m_closed=true;auto done=std::exchange(m_done,{});auto keep=Ref<RankedConfirmPopup>(this);
        queueInMainThread([keep,done=std::move(done),accepted] {if(!keep->isRunning())return;keep->Popup::onClose(nullptr);if(done)done(accepted);});
    }
public:
    static RankedConfirmPopup* create(std::string const& title,std::string const& message,std::function<void(bool)> done,std::string const& confirm="") {
        auto* popup=new RankedConfirmPopup;if(popup->init(title,message,std::move(done),confirm)){popup->autorelease();return popup;}delete popup;return nullptr;
    }
    void cancel() {m_done={};finish(false);}
    void onClose(CCObject*) override {finish(false);}
    void onExit() override {
        m_closed=true;m_done={};
        // WeakRefPool can retain a closed popup. Its invisible modal must stop
        // affecting Pause and waiting-room input at exit, not at destruction.
        if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}
        Popup::onExit();
    }
};
}
