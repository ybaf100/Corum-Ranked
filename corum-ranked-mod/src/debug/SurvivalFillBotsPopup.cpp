#include "../SurvivalRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedTranslations.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <charconv>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalFillBotsPopup final : public Popup {
    TextInput* m_password=nullptr,*m_botCount=nullptr;
    CCLabelBMFont* m_countLabel=nullptr;
    bool m_closed=false,m_exited=false;
    bool init() {
        if(!Popup::init(320,224))return false;
        setID("survival-fill-auth"_spr);m_bgSprite->setVisible(false);
        auto* panel=ui::surface({320,224},{160,112},ui::Blue,ui::Surface,255,true);m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel(i18n::text("survival-fill-bots","FILL WITH BOTS"),.44f,{160,199},ui::Cyan,ui::FontWeight::Black));
        auto* warning=ui::menuLabel(i18n::text("survival-debug-rating","DEBUG RESULTS AFFECT YOUR SURVIVAL LP"),.23f,{160,173},ui::Red,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(warning,292,.23f,.19f);panel->addChild(warning);
        m_password=TextInput::create(256,"Debug password");m_password->setPasswordMode(true);m_password->setMaxCharCount(128);m_password->setPosition({160,71});panel->addChild(m_password,10);
        m_countLabel=ui::menuLabel("",.26f,{160,145},ui::Text,ui::FontWeight::SemiBold);panel->addChild(m_countLabel);
        panel->addChild(ui::menuLabel(corum::ranked::i18n::literal("inline.bot-count"),.28f,{91,111},ui::Text,ui::FontWeight::SemiBold));
        m_botCount=TextInput::create(90,"0-15");m_botCount->setFilter("0123456789");m_botCount->setMaxCharCount(2);
        m_botCount->setString(std::to_string(std::clamp(16-SurvivalRuntime::get().queue().players,0,15)));
        m_botCount->setPosition({213,111});panel->addChild(m_botCount,10);scheduleUpdate();
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);panel->addChild(menu,15);
        auto* face=ui::surface({240,32},{},ui::Cyan,ui::SurfaceRaised,255,true);
        face->addChild(ui::menuLabel(i18n::text("start","START"),.34f,{120,16},ui::Text,ui::FontWeight::SemiBold),5);
        auto* start=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalFillBotsPopup::onStart));start->setPosition({160,34});menu->addChild(start);return true;
    }
    void onStart(CCObject*) {
        if(m_closed || SurvivalRuntime::get().creating())return;
        auto password=std::string(m_password->getString());if(password.empty())return;
        int bots=-1;auto value=std::string(m_botCount->getString());auto parsed=std::from_chars(value.data(),value.data()+value.size(),bots);
        auto const humans=SurvivalRuntime::get().queue().players;
        if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||bots<0||bots>15||humans+bots<2||humans+bots>16){m_countLabel->setString(corum::ranked::i18n::literal("inline.total-must-be-2-16-players"));m_countLabel->setColor(ui::Red);return;}
        m_password->setString("");m_closed=true;auto keep=Ref<SurvivalFillBotsPopup>(this);
        queueInMainThread([keep,password=std::move(password),bots]() mutable {
            if(!keep->isRunning())return;keep->Popup::onClose(nullptr);
            SurvivalRuntime::get().fillBots(std::move(password),bots);
        });
    }
public:
    void update(float) override {
        if(m_closed)return;
        int bots=0;auto value=std::string(m_botCount->getString());std::from_chars(value.data(),value.data()+value.size(),bots);
        auto const humans=SurvivalRuntime::get().queue().players;
        m_countLabel->setString((fmt::format(fmt::runtime(i18n::text("debug-player-totals","Players: {} · Total: {}")),humans,humans+bots)).c_str());
        m_countLabel->setColor(humans+bots>=2&&humans+bots<=16?ui::Text:ui::Red);
    }
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
