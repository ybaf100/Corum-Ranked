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
    bool init() override {
        if (!Popup::init(320,180)) return false;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-session-conflict"_spr);
        auto* panel=surface({320,180},{160,90},Red,Surface,255,true); m_mainLayer->addChild(panel);
        auto* title=menuLabel(i18n::text("session-other-device","Connected on another device"),.44f,{160,153},Red,FontWeight::Black);
        fitMenuLabelWidth(title,292,.44f,.32f); panel->addChild(title);
        auto* detail=menuLabel(i18n::text("session-takeover-detail","Disconnect the other device to enter here.\nAn ongoing match will be recovered."),.30f,{160,105},Text);
        fitFullMenuLabel(detail,290,48,.30f); panel->addChild(detail);
        m_actions=CCMenu::create(); m_actions->setPosition(CCPointZero); panel->addChild(m_actions,20);
        auto* red=surface({274,32},{},Red,SurfaceRaised,255,true);
        auto* caption=menuLabel(i18n::text("session-disconnect-other","Disconnect other device"),.35f,{137,16},Red,FontWeight::ExtraBold);
        fitMenuLabelWidth(caption,252,.35f,.27f); red->addChild(caption,5);
        auto* take=CCMenuItemSpriteExtra::create(red,this,menu_selector(RankedSessionConflictPopup::onTakeOver));
        take->setID("ranked-session-takeover-button"_spr); take->setPosition({160,58}); m_actions->addChild(take);
        auto* close=CCMenuItemSpriteExtra::create(menuLabel(i18n::text("close","Close"),.32f,{},Text),this,menu_selector(RankedSessionConflictPopup::onClose));
        close->setPosition({160,23}); m_actions->addChild(close);
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
