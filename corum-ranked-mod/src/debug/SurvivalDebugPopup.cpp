#include "../SurvivalRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "../RankedRuntime.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <charconv>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalDebugPopup final : public Popup {
    TextInput *m_password=nullptr,*m_rankInput=nullptr,*m_botCount=nullptr;
    std::array<CCMenuItemSpriteExtra*,3> m_groups{};
    CCLabelBMFont *m_forceLabel=nullptr,*m_status=nullptr;
    int m_group=0,m_rank=0;
    bool m_forceOnly=false,m_closed=false,m_exited=false;
    bool init(bool forceOnly) {
        if(!Popup::init(340,278))return false;m_forceOnly=forceOnly;
        m_bgSprite->setVisible(false);setID("survival-debug-config"_spr);
        auto* panel=ui::surface({340,278},{170,139},ui::Cyan,ui::Surface,255,true);m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel(forceOnly?"SURVIVAL · Force n":"SURVIVAL DEBUG",.46f,{170,251},ui::Cyan,ui::FontWeight::Black));
        auto* warning=ui::menuLabel(i18n::text("survival-debug-rating","DEBUG RESULTS AFFECT YOUR SURVIVAL LP"),.23f,{170,227},ui::Red,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(warning,312,.23f,.18f);panel->addChild(warning);
        m_password=TextInput::create(240,"Debug password");m_password->setPasswordMode(true);m_password->setMaxCharCount(128);m_password->setPosition({170,191});panel->addChild(m_password,10);
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);panel->addChild(menu,20);
        auto* average=ui::menuLabel("Average Tier",.23f,{170,166},ui::TextMuted,ui::FontWeight::SemiBold);average->setVisible(!forceOnly);panel->addChild(average);
        for(int i=0;i<3;i++) {
            m_groups[i]=CCMenuItemSpriteExtra::create(groupFace(i),this,menu_selector(SurvivalDebugPopup::onChoice));
            m_groups[i]->setTag(10+i);m_groups[i]->setPosition({66+i*104.0f,145});m_groups[i]->setVisible(!forceOnly);menu->addChild(m_groups[i]);
        }
        auto* countLabel=ui::menuLabel(corum::ranked::i18n::literal("inline.bot-count"),.27f,{106,111},ui::Text,ui::FontWeight::SemiBold);countLabel->setVisible(!forceOnly);panel->addChild(countLabel);
        m_botCount=TextInput::create(100,"1-15");m_botCount->setFilter("0123456789");m_botCount->setMaxCharCount(2);m_botCount->setString("15");m_botCount->setPosition({220,111});m_botCount->setVisible(!forceOnly);panel->addChild(m_botCount,10);
        m_forceLabel=ui::menuLabel("",.29f,{70,15},ui::Text,ui::FontWeight::SemiBold);
        auto* face=ui::surface({140,30},{},ui::Blue,ui::SurfaceRaised,255,true);face->addChild(m_forceLabel,5);
        auto* scenario=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalDebugPopup::onChoice));scenario->setTag(1);scenario->setPosition({120,75});menu->addChild(scenario);
        m_rankInput=TextInput::create(82,forceOnly?fmt::format("1-{}",SurvivalRuntime::get().view().playerCount):"n");m_rankInput->setFilter("0123456789");m_rankInput->setMaxCharCount(2);m_rankInput->setString("1");m_rankInput->setPosition({243,75});panel->addChild(m_rankInput,10);
        auto* startFace=ui::surface({240,30},{},ui::Cyan,ui::SurfaceRaised,255,true);
        startFace->addChild(ui::menuLabel(i18n::text("start","START"),.34f,{120,15},ui::Text,ui::FontWeight::SemiBold),5);
        auto* start=CCMenuItemSpriteExtra::create(startFace,this,menu_selector(SurvivalDebugPopup::onChoice));start->setTag(2);start->setPosition({170,35});menu->addChild(start);
        m_status=ui::menuLabel("",.22f,{170,13},ui::Red,ui::FontWeight::SemiBold);panel->addChild(m_status,5);
        m_rank=forceOnly?1:0;refreshLabels();return true;
    }
    CCNode* groupFace(int index) {
        constexpr char const* groups[]={"RED-BRONZE","SILVER","GOLD"};auto color=index==m_group?ui::Gold:ui::Blue;
        auto* face=ui::surface({98,28},{},color,ui::SurfaceRaised,255,true);
        auto* label=ui::menuLabel(groups[index],.24f,{49,14},index==m_group?ui::Gold:ui::Text,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(label,86,.24f,.18f);face->addChild(label,5);return face;
    }
    void refreshLabels() {
        for(int i=0;i<3;i++) {m_groups[i]->setNormalImage(groupFace(i));m_groups[i]->updateSprite();}
        m_rankInput->setVisible(m_rank>0);
        m_forceLabel->setString(m_rank?"Force n":i18n::text("normal-match","NORMAL MATCH").c_str());
        ui::fitMenuLabelWidth(m_forceLabel,122,.29f,.20f);
    }
    void onChoice(CCObject* sender) {
        if(m_closed)return;auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);if(!item)return;
        if(item->getTag()>=10&&item->getTag()<=12){m_group=item->getTag()-10;refreshLabels();return;}
        if(item->getTag()==1){if(!m_forceOnly)m_rank=m_rank?0:1;refreshLabels();return;}
        auto password=std::string(m_password->getString());
        if(password.empty()){m_status->setString(i18n::text("admin-password-needed","Enter the debug password.").c_str());return;}
        int bots=15;
        if(!m_forceOnly){auto value=std::string(m_botCount->getString());auto parsed=std::from_chars(value.data(),value.data()+value.size(),bots);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||bots<1||bots>15){m_status->setString(corum::ranked::i18n::literal("inline.bot-count-1-15"));return;}}
        int rank=0;
        if(m_rank>0) {
            auto const value=std::string(m_rankInput->getString());auto parsed=std::from_chars(value.data(),value.data()+value.size(),rank);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || rank<1 || rank>(m_forceOnly?SurvivalRuntime::get().view().playerCount:bots+1)) {m_status->setString(fmt::format("1 - {}",m_forceOnly?SurvivalRuntime::get().view().playerCount:bots+1).c_str());return;}
        }
        auto const group=m_group;auto const forceOnly=m_forceOnly;
        m_closed=true;auto keep=Ref<SurvivalDebugPopup>(this);
        queueInMainThread([keep,password=std::move(password),group,rank,forceOnly,bots]() mutable {
            if(!keep->isRunning())return;keep->m_password->setString("");keep->Popup::onClose(nullptr);
            if(forceOnly)SurvivalRuntime::get().forceRank(std::move(password),rank);
            else {constexpr char const* groups[]={"RED_BRONZE","SILVER","GOLD"};SurvivalRuntime::get().start(std::move(password),groups[group],rank,bots);}
        });
    }
public:
    static SurvivalDebugPopup* create(bool forceOnly) {auto* popup=new SurvivalDebugPopup;if(popup->init(forceOnly)){popup->autorelease();return popup;}delete popup;return nullptr;}
    void onExit() override {
        m_closed=true;if(m_password)m_password->setString("");
        if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();
    }
};
}
void showSurvivalDebugPopup() {
    SurvivalRuntime::get().tick();
    if(SurvivalRuntime::get().active()){showSurvivalGame();return;}
    if(auto* popup=SurvivalDebugPopup::create(false))popup->show();
}
void showSurvivalForceRankPopup() {if(auto* popup=SurvivalDebugPopup::create(true))popup->show();}
}
#endif
