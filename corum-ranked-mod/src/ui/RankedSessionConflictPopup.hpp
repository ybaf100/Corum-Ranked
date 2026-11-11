#pragma once
#include "../RankedRuntime.hpp"
#include "RankedInput.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;
class RankedSessionConflictPopup final : public Popup {
    bool m_closed=false, m_exited=false;
    CCMenu* m_actions=nullptr;
    // Q9 board: red title, the account panel, what pressing the red button
    // does, then the filled red action over a ghost CLOSE.
    bool init() override {
        if (!Popup::init(290,210)) return false;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-session-conflict"_spr);
        auto* panel=surface({290,210},{145,105},Red,Surface,255,true); m_mainLayer->addChild(panel);
        auto line=[&](CCNode* parent,std::string const& text,CCPoint at,float width,float scale,ccColor3B color,FontWeight weight){
            auto* label=menuLabel(text,scale,at,color,weight);fitMenuLabelWidth(label,width,scale,scale*.6f);label->setAnchorPoint({0,.5f});parent->addChild(label,5);return label;
        };
        auto* title=line(panel,i18n::text("session-other-device","Connected on another device"),{14,188},262,28.f/64.f,Red,FontWeight::Black); leanLabel(title);
        auto* account=surface({262,44},{145,148},Steel,SurfaceRaised,255,false); panel->addChild(account,2);
        auto const name=std::string(GJAccountManager::get()->m_username);
        line(account,i18n::insertValue(i18n::text("session-account-elsewhere","{} is connected to Ranked on another device"),name),{10,29},242,.25f,Text,FontWeight::ExtraBold);
        line(account,i18n::text("session-one-device","One account can use Ranked on one device at a time"),{10,14},242,.19f,TextMuted,FontWeight::SemiBold);
        auto const detail=i18n::text("session-takeover-detail","Disconnect the other device to enter here.\nAn ongoing match will be recovered.");
        auto const split=detail.find('\n');
        line(panel,detail.substr(0,split),{14,106},262,.27f,Text,FontWeight::ExtraBold);
        if(split!=std::string::npos)line(panel,detail.substr(split+1),{14,91},262,.24f,TextMuted,FontWeight::SemiBold);
        m_actions=CCMenu::create(); m_actions->setPosition(CCPointZero); panel->addChild(m_actions,20);
        auto* red=surface({262,26},{},Red,Red,255,true);
        auto* caption=menuLabel(i18n::text("session-disconnect-other","Disconnect other device"),.30f,{131,13},Ink,FontWeight::ExtraBold);
        fitMenuLabelWidth(caption,240,.30f,.22f); red->addChild(caption,5);
        auto* take=CCMenuItemSpriteExtra::create(red,this,menu_selector(RankedSessionConflictPopup::onTakeOver));
        take->setID("ranked-session-takeover-button"_spr); take->setPosition({145,52}); m_actions->addChild(take);
        auto* ghost=surface({262,20},{},Steel,SurfaceRaised,255,true);
        auto* closeText=menuLabel(i18n::text("close","Close"),.24f,{131,10},Text,FontWeight::ExtraBold);
        fitMenuLabelWidth(closeText,240,.24f,.18f); ghost->addChild(closeText,5);
        auto* close=CCMenuItemSpriteExtra::create(ghost,this,menu_selector(RankedSessionConflictPopup::onClose));
        close->setPosition({145,22}); m_actions->addChild(close);
        schedule(schedule_selector(RankedSessionConflictPopup::refresh),.1f); return true;
    }
    void refresh(float) { if (!m_closed && !RankedRuntime::get().view().accountActiveElsewhere) dismiss(); }
    void retire(bool takeOver) {
        if (m_closed) return;
        m_closed=true; unschedule(schedule_selector(RankedSessionConflictPopup::refresh));
        for (auto* object : CCArrayExt<CCObject*>(m_actions->getChildren()))
            if (auto* item=typeinfo_cast<CCMenuItem*>(object)) item->setEnabled(false);
        auto const keep=Ref<RankedSessionConflictPopup>(this);
        queueInMainThread([keep,takeOver] {
            if (!keep->isRunning()) return;
            cancelMenuTouch(keep->m_actions); keep->Popup::onClose(nullptr);
            if (takeOver) RankedRuntime::get().takeOverSession();
        });
    }
    void onTakeOver(CCObject*) { retire(true); }
public:
    static RankedSessionConflictPopup* create() {
        auto* popup=new RankedSessionConflictPopup;
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onClose(CCObject*) override { retire(false); }
    void dismiss() { retire(false); }
    void onExit() override {
        m_closed=true; unschedule(schedule_selector(RankedSessionConflictPopup::refresh));
        if (!m_exited) { m_exited=true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
}
