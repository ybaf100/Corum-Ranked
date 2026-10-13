#include "../SurvivalRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalFillBotsPopup final : public Popup {
    TextInput* m_password=nullptr;
    bool m_closed=false,m_exited=false;
    bool init() {
        if(!Popup::init(320,174))return false;
        setID("survival-fill-auth"_spr);m_bgSprite->setVisible(false);
        auto* panel=ui::surface({320,174},{160,87},ui::Blue,ui::Surface,255,true);m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel(i18n::text("survival-fill-bots","FILL WITH BOTS"),.44f,{160,147},ui::Cyan,ui::FontWeight::Black));
        auto* warning=ui::menuLabel(i18n::text("survival-debug-rating","DEBUG RESULTS AFFECT YOUR SURVIVAL LP"),.23f,{160,121},ui::Red,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(warning,292,.23f,.19f);panel->addChild(warning);
        m_password=TextInput::create(256,"Debug password");m_password->setPasswordMode(true);m_password->setMaxCharCount(128);m_password->setPosition({160,82});panel->addChild(m_password,10);
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);panel->addChild(menu,15);
        auto* face=ui::surface({240,32},{},ui::Cyan,ui::SurfaceRaised,255,true);
        face->addChild(ui::menuLabel(i18n::text("start","START"),.34f,{120,16},ui::Text,ui::FontWeight::SemiBold),5);
        auto* start=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalFillBotsPopup::onStart));start->setPosition({160,34});menu->addChild(start);return true;
    }
    void onStart(CCObject*) {
        if(m_closed || SurvivalRuntime::get().creating())return;
        auto password=std::string(m_password->getString());if(password.empty())return;
        m_password->setString("");m_closed=true;auto keep=Ref<SurvivalFillBotsPopup>(this);
        queueInMainThread([keep,password=std::move(password)]() mutable {
            if(!keep->isRunning())return;keep->Popup::onClose(nullptr);
            SurvivalRuntime::get().fillBots(std::move(password));
        });
    }
public:
    static SurvivalFillBotsPopup* create(){auto* popup=new SurvivalFillBotsPopup;if(popup->init()){popup->autorelease();return popup;}delete popup;return nullptr;}
    void onExit() override {
        m_closed=true;if(m_password)m_password->setString("");
        if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();
    }
};
}
void showSurvivalFillBotsPopup(){
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(!SurvivalRuntime::get().queueing() || (scene && scene->getChildByIDRecursive("survival-fill-auth"_spr)))return;
    if(auto* popup=SurvivalFillBotsPopup::create())popup->show();
}
}
#endif
