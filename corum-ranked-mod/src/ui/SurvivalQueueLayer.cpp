#include "../SurvivalRuntime.hpp"
#include "../RankedPopup.hpp"
#include "../RankedRuntime.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedModUpdateManager.hpp"
#include "RankAssetPackManager.hpp"
#include "../RankedAudioManager.hpp"
#include "../domain/DownloadOverlayPolicy.hpp"
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

// Free play during the mod update / resource download: a card at the top left
// of every scene (gameplay, menus, level search) with the percent and a bar.
// Tapping it returns to the Ranked download screen.
class RankedDownloadOverlay final : public CCMenu {
    CCLabelBMFont* m_label=nullptr;
    CCLayerColor* m_fill=nullptr;
    std::string m_text;
    static constexpr float kWidth=120,kHeight=26;
    bool init() override {
        if(!CCMenu::init())return false;
        setID("ranked-download-overlay"_spr);setPosition(CCPointZero);
        auto* face=ui::surface({kWidth,kHeight},{},ui::Accent,ui::Ink,236,true);
        m_label=ui::menuLabel("",.24f,{kWidth/2,kHeight/2+3},ui::Text,ui::FontWeight::ExtraBold);face->addChild(m_label,5);
        auto* track=CCLayerColor::create({ui::SurfaceSoft.r,ui::SurfaceSoft.g,ui::SurfaceSoft.b,255},kWidth-12,3);track->setPosition({6,4});face->addChild(track,5);
        m_fill=CCLayerColor::create({ui::Accent.r,ui::Accent.g,ui::Accent.b,255},0,3);m_fill->setPosition({6,4});face->addChild(m_fill,6);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedDownloadOverlay::onTap));
        item->setID("ranked-download-overlay-button"_spr);addChild(item);
        return true;
    }
    void onTap(CCObject*) { showRankedPopup(); }
public:
    static RankedDownloadOverlay* create(){auto* node=new RankedDownloadOverlay;if(node->init()){node->autorelease();return node;}delete node;return nullptr;}
    void present(DownloadOverlayState const& state,float top) {
        auto text=fmt::format(fmt::runtime(state.modUpdate?i18n::text("download-overlay-update","UPDATE {}%"):i18n::text("download-overlay-resources","RESOURCES {}%")),state.percent);
        if(text!=m_text){m_text=text;m_label->setString(text.c_str());ui::fitMenuLabelWidth(m_label,kWidth-12,.24f,.16f);}
        m_fill->setContentSize({(kWidth-12)*std::clamp(state.percent,0,100)/100.0f,3});
        if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(getChildByID("ranked-download-overlay-button"_spr)))
            item->setPosition({8+kWidth/2,top-kHeight/2});
    }
};

DownloadOverlayState currentDownloadOverlayState() {
    using ui::ModUpdateState;
    auto const& update=ui::RankedModUpdateManager::get().view();
    auto const& assets=ui::RankAssetPackManager::get().view();
    auto const audio=RankedAudioManager::get().downloadView();
    DownloadOverlayInputs in;
    in.modUpdating=update.state==ModUpdateState::Downloading||update.state==ModUpdateState::Verifying;
    in.modProgress=update.progress;
    in.assetsDownloading=assets.state==ui::RankAssetPackState::Downloading;
    in.assetProgress=assets.progress;
    in.audioDownloading=audio.state==RankedResourceDownloadState::Downloading;
    in.audioReady=audio.ready;in.audioTotal=audio.total;in.audioActivePercent=audio.activeProgress;
    return downloadOverlayState(in);
}
}

// The queue lives inside the Ranked lobby (the Survival card expands). Outside
// the lobby (free play, recovery) the lobby is opened instead of a layer.
void showSurvivalQueue(){
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(rankedLobbyRunning(scene) || SurvivalRuntime::get().queueFreePlaying())return;
    showRankedPopup();
}

static void pumpRankedDownloadOverlay(CCScene* scene,bool queueCardShown){
    auto* existing=typeinfo_cast<RankedDownloadOverlay*>(scene->getChildByID("ranked-download-overlay"_spr));
    auto const lobby=rankedLobbyRunning(scene);
    // Outside Ranked the resource queue still has to advance (the lobby ticks it itself).
    if(!lobby)RankedAudioManager::get().tickDownloads();
    auto const state=currentDownloadOverlayState();
    auto const visible=state.visible && !lobby && !CCDirector::sharedDirector()->getNextScene();
    if(!visible){if(existing)existing->removeFromParentAndCleanup(true);return;}
    if(!existing){existing=RankedDownloadOverlay::create();if(!existing)return;scene->addChild(existing,10000);}
    // Below the queue card when both are shown.
    existing->present(state,CCDirector::sharedDirector()->getWinSize().height-8-(queueCardShown?28.0f:0.0f));
}

void pumpSurvivalQueueOverlay(){
    auto* director=CCDirector::sharedDirector();auto* scene=director->getRunningScene();
    if(!scene || typeinfo_cast<CCTransitionScene*>(scene))return;
    struct DownloadPump{CCScene* scene;bool queue=false;~DownloadPump(){pumpRankedDownloadOverlay(scene,queue);}} download{scene};
    auto& runtime=SurvivalRuntime::get();
    auto* existing=typeinfo_cast<SurvivalQueueOverlay*>(scene->getChildByID("survival-queue-overlay"_spr));
    auto const soloStage=RankedRuntime::get().view().stage;
    auto const solo=soloStage==RuntimeStage::Queued || soloStage==RuntimeStage::JoiningQueue;
    auto const survival=runtime.queueing() && !runtime.active();
    auto const visible=(survival || solo) && !rankedLobbyRunning(scene) &&
        !scene->getChildByID("survival-game-coordinator"_spr) && !director->getNextScene();
    if(!visible){if(existing)existing->removeFromParentAndCleanup(true);return;}
    if(!existing){existing=SurvivalQueueOverlay::create();if(!existing)return;scene->addChild(existing,10000);}
    download.queue=true;
    if(survival){
        auto const& queue=runtime.queue();auto const waiting=runtime.queueWaitingSeconds();
        existing->present(fmt::format("{}/{} - {:02}:{:02}",queue.players,queue.maximumPlayers,waiting/60,waiting%60));
    }else{
        auto const waiting=runtime.soloQueueWaitingSeconds();
        existing->present(fmt::format("SOLO - {:02}:{:02}",waiting/60,waiting%60));
    }
}
}
