#include "HistoryLevelLookup.hpp"
#include "RankedAudioManager.hpp"
#include "RankedRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "RankedMapDetails.hpp"
#include "domain/HistoryPresentation.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#endif
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
using namespace geode::prelude;
namespace corum::ranked {
HistoryLevelLookup& HistoryLevelLookup::get(){static HistoryLevelLookup owner;return owner;}
void HistoryLevelLookup::cancel(std::uint64_t generation){
    if(generation!=m_generation)return;
    if(!m_key.empty()){m_retired.push_back(m_key);while(m_retired.size()>64)m_retired.pop_front();}
    m_key.clear();m_level=nullptr;m_error.clear();m_id=0;
}
std::uint64_t HistoryLevelLookup::begin(int id){
    cancel(m_generation);auto const generation=++m_generation;m_id=id;
    auto* manager=GameLevelManager::sharedState();
    if(id<=0 || !manager){m_error="unavailable";return generation;}
    if(auto* saved=manager->getSavedLevel(id);saved && static_cast<int>(saved->m_levelID)==id){m_level=saved;return generation;}
    // The first field is always the recorded numeric ID. Extra form fields are
    // the same exact-ID native lookup used by Ranked preparation. A unique key
    // prevents a cancelled response for the same map from accepting a new UI.
    auto* search=GJSearchObject::create(SearchType::Type19,fmt::format("{}&gameVersion=22&corumHistory={}",id,generation));
    if(!search){m_error="unavailable";return generation;}
    m_key=search->getKey();manager->getOnlineLevels(search);return generation;
}
bool HistoryLevelLookup::ownsResponse(char const* key)const{
    return key && ((!m_key.empty()&&m_key==key)||std::find(m_retired.begin(),m_retired.end(),key)!=m_retired.end());
}
geode::Ref<GJGameLevel> HistoryLevelLookup::level(std::uint64_t generation)const{return generation==m_generation?m_level:geode::Ref<GJGameLevel>{};}
std::string HistoryLevelLookup::error(std::uint64_t generation)const{return generation==m_generation?m_error:"cancelled";}
void HistoryLevelLookup::loadLevelsFinished(CCArray* levels,char const* key){
    if(!key || m_key.empty() || m_key!=key)return;
    if(levels)for(unsigned i=0;i<levels->count();++i){auto* candidate=typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
        if(candidate && static_cast<int>(candidate->m_levelID)==m_id){m_level=candidate;RankedMapDetails::get().remember(candidate);break;}}
    if(!m_level)m_error="unavailable";
}
void HistoryLevelLookup::loadLevelsFailed(char const* key){if(key&&!m_key.empty()&&m_key==key)m_error="unavailable";}
void HistoryLevelLookup::responseCompleted(char const* key){
    if(key&&!m_key.empty()&&m_key==key&&!m_level&&m_error.empty())m_error="unavailable";
}
bool canOpenHistoryLevel(){
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(debug::DebugSpectateRuntime::get().mode()!=debug::DebugSpectateMode::None)return false;
#endif
    auto const stage=RankedRuntime::get().view().stage;auto& survival=SurvivalRuntime::get();
    auto const soloBusy=stage==RuntimeStage::Matched||stage==RuntimeStage::Queued||stage==RuntimeStage::JoiningQueue||stage==RuntimeStage::Loading;
    return historyLevelNavigationAllowed(soloBusy,survival.active()||survival.creating()||survival.queueing()||survival.acquiring(),PlayLayer::get()!=nullptr);
}
namespace {
class HistoryLevelPopup final:public Popup {
    std::uint64_t m_ticket=0;std::optional<WeakRef<CCScene>> m_origin;
    CCLabelBMFont* m_status=nullptr;
    std::chrono::steady_clock::time_point m_deadline;
    void refresh(float){
        auto origin=m_origin?m_origin->lock():Ref<CCScene>{};
        auto* director=CCDirector::sharedDirector();
        if(!origin || origin.data()!=director->getRunningScene() || director->getNextScene() || !canOpenHistoryLevel()){onClose(nullptr);return;}
        auto& lookup=HistoryLevelLookup::get();auto level=lookup.level(m_ticket);
        if(level){
            // Keep the native object alive across removing this popup. No Ready,
            // song gate, attempt, result reporting or Play call exists here.
            auto* scene=LevelInfoLayer::scene(level.data(),false);
            if(!scene){fail();return;}
            auto* transition=CCTransitionFade::create(.2f,scene);
            if(!transition){fail();return;}
            // History opens ordinary GD LevelInfo/Play scenes, whose PlayLayer
            // deliberately never acquires Ranked audio. Retire every owned
            // lobby/crossfade channel before the native scene takes over.
            RankedAudioManager::get().fadeOutForGameplay();
            log::info("[RankedHistory] native level audio handoff level={}",static_cast<int>(level->m_levelID));
            onClose(nullptr);CCDirector::sharedDirector()->pushScene(transition);return;
        }
        if(!lookup.error(m_ticket).empty() || std::chrono::steady_clock::now()>=m_deadline)fail();
    }
    void fail(){
        unschedule(schedule_selector(HistoryLevelPopup::refresh));HistoryLevelLookup::get().cancel(m_ticket);
        m_status->setString(i18n::text("history-level-unavailable","Map information could not be found.").c_str());
        ui::fitFullMenuLabel(m_status,220,55,.34f);
    }
    bool init(int id,std::string message){
        if(!Popup::init(250,115))return false;
        auto* title=ui::menuLabel(i18n::text("history-view-level","VIEW LEVEL"),.4f,{125,92},ui::Cyan,ui::FontWeight::ExtraBold);ui::fitFullMenuLabel(title,210,25,.4f);m_mainLayer->addChild(title);
        m_status=ui::menuLabel(message.empty()?i18n::text("loading","LOADING..."):message,.34f,{125,48},ui::Text,ui::FontWeight::SemiBold);ui::fitFullMenuLabel(m_status,220,55,.34f);m_mainLayer->addChild(m_status);
        if(!message.empty())return true;
        m_origin.emplace(CCDirector::sharedDirector()->getRunningScene());m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        m_ticket=HistoryLevelLookup::get().begin(id);schedule(schedule_selector(HistoryLevelPopup::refresh),.1f);return true;
    }
public:
    static HistoryLevelPopup* create(int id,std::string message={}){auto* p=new HistoryLevelPopup;if(p->init(id,std::move(message))){p->autorelease();return p;}delete p;return nullptr;}
    void onExit()override{unschedule(schedule_selector(HistoryLevelPopup::refresh));HistoryLevelLookup::get().cancel(m_ticket);Popup::onExit();}
};
}
void showHistoryLevel(int id){
    if(!canOpenHistoryLevel()){if(auto* p=HistoryLevelPopup::create(0,i18n::text("history-level-busy","Finish or leave the current match or queue before opening a level.")))p->show();return;}
    if(id<=0){if(auto* p=HistoryLevelPopup::create(0,i18n::text("history-level-unavailable","Map information could not be found.")))p->show();return;}
    if(auto* p=HistoryLevelPopup::create(id))p->show();
}
}
