#include "../SurvivalRuntime.hpp"
#include "../RankedPopup.hpp"
#include "../RankedRuntime.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/PlayLayer.hpp>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
bool rankedLobbyRunning(CCScene* scene) {
    return scene && scene->getChildByID("corum-ranked-fullscreen"_spr);
}

class SurvivalQueueOverlay final : public CCMenu {
    CCLabelBMFont* m_label=nullptr;
    CCNode* m_face=nullptr;
    std::string m_text;
    bool init() override {
        if(!CCMenu::init())return false;
        setID("survival-queue-overlay"_spr);setPosition(CCPointZero);
        m_face=ui::surface({96,22},{},ui::Accent,ui::Ink,236,true);
        m_label=ui::menuLabel("",.26f,{48,11},ui::Text,ui::FontWeight::ExtraBold);m_face->addChild(m_label,5);
        auto* item=CCMenuItemSpriteExtra::create(m_face,this,menu_selector(SurvivalQueueOverlay::onTap));
        item->setID("survival-queue-overlay-button"_spr);addChild(item);
        return true;
    }
    void onTap(CCObject*) {
        auto& runtime=SurvivalRuntime::get();
        if(runtime.queueFreePlaying()){runtime.requestQueueFreePlayReturn();return;}
        showRankedPopup();
    }
public:
    static SurvivalQueueOverlay* create(){auto* node=new SurvivalQueueOverlay;if(node->init()){node->autorelease();return node;}delete node;return nullptr;}
    void present(std::string const& text) {
        if(text==m_text)return;
        m_text=text;m_label->setString(text.c_str());ui::fitMenuLabelWidth(m_label,88,.26f,.18f);
        auto const size=CCDirector::sharedDirector()->getWinSize();
        if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(getChildByID("survival-queue-overlay-button"_spr)))
            item->setPosition({8+48,size.height-8-11});
    }
};
}

// The queue lives inside the Ranked lobby (the Survival card expands). Outside
// the lobby (free play, recovery) the lobby is opened instead of a layer.
void showSurvivalQueue(){
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(rankedLobbyRunning(scene) || SurvivalRuntime::get().queueFreePlaying())return;
    showRankedPopup();
}

void pumpSurvivalQueueOverlay(){
    auto* director=CCDirector::sharedDirector();auto* scene=director->getRunningScene();
    if(!scene || typeinfo_cast<CCTransitionScene*>(scene))return;
    auto& runtime=SurvivalRuntime::get();
    auto* existing=typeinfo_cast<SurvivalQueueOverlay*>(scene->getChildByID("survival-queue-overlay"_spr));
    auto const soloStage=RankedRuntime::get().view().stage;
    auto const solo=soloStage==RuntimeStage::Queued || soloStage==RuntimeStage::JoiningQueue;
    auto const survival=runtime.queueing() && !runtime.active();
    auto const visible=(survival || solo) && !rankedLobbyRunning(scene) &&
        !scene->getChildByID("survival-game-coordinator"_spr) && !director->getNextScene();
    if(!visible){if(existing)existing->removeFromParentAndCleanup(true);return;}
    if(!existing){existing=SurvivalQueueOverlay::create();if(!existing)return;scene->addChild(existing,10000);}
    if(survival){
        auto const& queue=runtime.queue();auto const waiting=runtime.queueWaitingSeconds();
        existing->present(fmt::format("{}/{} - {:02}:{:02}",queue.players,queue.maximumPlayers,waiting/60,waiting%60));
    }else{
        auto const waiting=runtime.soloQueueWaitingSeconds();
        existing->present(fmt::format("SOLO - {:02}:{:02}",waiting/60,waiting%60));
    }
}
}
