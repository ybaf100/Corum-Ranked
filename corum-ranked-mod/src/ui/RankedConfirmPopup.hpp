#pragma once
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedInput.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>

namespace corum::ranked::ui {
using namespace geode::prelude;
class RankedConfirmPopup final : public Popup {
    std::function<void(bool)> m_done;
    bool m_closed=false;
    bool init(std::string const& title,std::string const& message,std::function<void(bool)> done,std::string const& confirm) {
        if(!Popup::init(340,190)) { return false; }
        m_done=std::move(done);m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);
        auto* panel=surface({340,190},{170,95},Gold,Surface,255,true);m_mainLayer->addChild(panel);
        auto* heading=menuLabel(title,.38f,{170,160},Gold,FontWeight::SemiBold);fitMenuLabelWidth(heading,310,.38f,.26f);panel->addChild(heading,5);
        auto* detail=menuLabel(message,.28f,{170,106},Text,FontWeight::SemiBold);fitFullMenuLabel(detail,310,64,.28f);panel->addChild(detail,5);
        // Popup owns modal touch priority for this menu. A separate nested menu
        // can remain behind the native Pause swallowing delegate on iOS.
        auto* menu=m_buttonMenu;
        for(int i=0;i<2;i++) {
            auto* face=surface({140,32},{},i?Gold:Blue,SurfaceRaised,255,true);
            face->addChild(menuLabel(i && !confirm.empty()?confirm:i18n::text(i?"confirm":"cancel",i?"CONFIRM":"CANCEL"),.30f,{70,16},i?Gold:Text,FontWeight::SemiBold),5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedConfirmPopup::onChoice));item->setTag(i);item->setPosition({i?248.0f:92.0f,38});menu->addChild(item);
        }
        return true;
    }
    void onChoice(CCObject* sender) {auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);finish(item&&item->getTag()==1);}
    void finish(bool accepted) {
        if(m_closed) { return; }
        m_closed=true;auto done=std::exchange(m_done,{});auto keep=Ref<RankedConfirmPopup>(this);
        queueInMainThread([keep,done=std::move(done),accepted] {if(!keep->isRunning())return;keep->Popup::onClose(nullptr);if(done)done(accepted);});
    }
public:
    static RankedConfirmPopup* create(std::string const& title,std::string const& message,std::function<void(bool)> done,std::string const& confirm="") {
        auto* popup=new RankedConfirmPopup;if(popup->init(title,message,std::move(done),confirm)){popup->autorelease();return popup;}delete popup;return nullptr;
    }
    void cancel() {m_done={};finish(false);}
    void onClose(CCObject*) override {finish(false);}
    void onExit() override {m_closed=true;m_done={};Popup::onExit();}
};
}
