#pragma once
#include "RankedPopupLayout.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>
#include <string>
#include <vector>

namespace corum::ranked::ui {
using namespace geode::prelude;
struct RankedHistoryFilterChoice {std::string value,caption;};
class RankedHistoryFilterPopup final:public Popup {
    std::vector<RankedHistoryFilterChoice> m_choices;
    std::function<void(std::string)> m_choose;
    bool m_closed=false,m_exited=false;
    bool init(std::vector<RankedHistoryFilterChoice> choices,std::string const& selected,std::function<void(std::string)> choose) {
        if(choices.empty()||choices.size()>7)return false;
        auto const height=86.f+choices.size()*34.f;
        auto const layout=rankedPopupLayout({280,height},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_choices=std::move(choices);m_choose=std::move(choose);m_noElasticity=true;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-history-filter-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        auto* panel=surface({280,height},{140,height/2},Cyan,Surface,255,true);content->addChild(panel);
        auto* title=menuLabel(i18n::text("match-history","MATCH HISTORY"),.4f,{140,height-22},Text,FontWeight::Black);
        fitSingleLineMenuLabel(title,252,24,.4f);panel->addChild(title,5);
        for(std::size_t i=0;i<m_choices.size();++i){
            auto const active=m_choices[i].value==selected;
            auto* face=surface({244,28},{},active?Cyan:Blue,SurfaceRaised,255,true);
            auto* label=menuLabel(m_choices[i].caption,.30f,{122,14},active?Cyan:Text,FontWeight::ExtraBold);
            fitSingleLineMenuLabel(label,222,21,.30f);face->addChild(label,5);
            auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedHistoryFilterPopup::onSelect));
            item->setTag(static_cast<int>(i));item->setPosition({140,height-57-34.f*i});m_buttonMenu->addChild(item);
        }
        auto* face=surface({120,28},{},Blue,SurfaceRaised,255,true);
        auto* label=menuLabel(i18n::text("close","CLOSE"),.30f,{60,14},Text,FontWeight::ExtraBold);face->addChild(label,5);
        fitSingleLineMenuLabel(label,100,20,.30f);
        auto* close=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedHistoryFilterPopup::onClose));
        close->setPosition({140,22});m_buttonMenu->setZOrder(20);m_buttonMenu->addChild(close);
        return true;
    }
    void onSelect(CCObject* sender) {
        if(m_closed||!sender)return;
        auto const index=sender->getTag();if(index<0||static_cast<std::size_t>(index)>=m_choices.size())return;
        [[maybe_unused]] Ref<RankedHistoryFilterPopup> keepAlive(this);
        auto value=m_choices[index].value;auto choose=std::move(m_choose);
        dismiss();if(choose)choose(std::move(value));
    }
public:
    static RankedHistoryFilterPopup* create(std::vector<RankedHistoryFilterChoice> choices,std::string const& selected,std::function<void(std::string)> choose) {
        auto* p=new RankedHistoryFilterPopup;
        if(p->init(std::move(choices),selected,std::move(choose))){p->autorelease();return p;}delete p;return nullptr;
    }
    void dismiss() {
        if(m_closed)return;
        [[maybe_unused]] Ref<RankedHistoryFilterPopup> keepAlive(this);
        m_closed=true;m_choose={};Popup::onClose(nullptr);
    }
    void onClose(CCObject*)override{dismiss();}
    void onExit()override {
        m_closed=true;m_choose={};
        if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}
        Popup::onExit();
    }
};
}
