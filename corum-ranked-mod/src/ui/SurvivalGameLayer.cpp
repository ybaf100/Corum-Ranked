#include "../SurvivalRuntime.hpp"
#include "../RankedRoundPreparation.hpp"
#include "../RankedRuntime.hpp"
#include "../RankedAudioManager.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankTransitionLayer.hpp"
#include "ResultScoreLayer.hpp"
#include <Geode/binding/PlayLayer.hpp>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalGameLayer final : public CCLayerColor {
    CCLabelBMFont *m_heading=nullptr,*m_status=nullptr,*m_map=nullptr,*m_records=nullptr;
    CCMenuItemSpriteExtra *m_resume=nullptr,*m_continue=nullptr,*m_surrender=nullptr;
    std::string m_launchedContext,m_resultMotion;
    bool m_closed=false,m_exited=false,m_motion=false;
    static std::string text(char const* key,char const* fallback) {return i18n::text(key,fallback);}
    CCMenuItemSpriteExtra* button(CCMenu* menu,std::string const& caption,CCPoint position,float width,int tag,ccColor3B color=ui::Blue) {
        auto* face=ui::surface({width,30},{},color,ui::SurfaceRaised,255,true);
        auto* label=ui::menuLabel(caption,.29f,{width/2,15},color,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(label,width-16,.29f,.22f);face->addChild(label,5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalGameLayer::onAction));item->setTag(tag);item->setPosition(position);menu->addChild(item);return item;
    }
    bool init() override {
        if(!CCLayerColor::initWithColor({0,0,0,255}))return false;setID("survival-game-coordinator"_spr);setKeypadEnabled(true);
        auto const size=CCDirector::sharedDirector()->getWinSize();
        setContentSize(size);
        m_heading=ui::menuLabel("SURVIVAL",.60f,{size.width/2,size.height-27},ui::Cyan,ui::FontWeight::Black);addChild(m_heading);
        m_map=ui::menuLabel("",.27f,{size.width/2,size.height-55},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_map);
        m_status=ui::menuLabel("",.28f,{size.width/2,54},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_status);
        m_records=ui::menuLabel("",.22f,{size.width/2,size.height/2},ui::Text,ui::FontWeight::SemiBold);addChild(m_records);
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);addChild(menu,20);
        m_resume=button(menu,text("continue","CONTINUE"),{size.width/2,25},132,0,ui::Cyan);
        m_continue=button(menu,text("close","CLOSE"),{size.width/2,25},132,1);
        m_surrender=button(menu,text("surrender","SURRENDER"),{size.width-63,25},106,2,ui::Red);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        button(menu,"Force n",{65,25},100,3,ui::Red);
#endif
        scheduleUpdate();return true;
    }
    void onAction(CCObject* object) {
        if(m_closed || m_motion)return;auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);if(!item)return;
        auto& runtime=SurvivalRuntime::get();
        if(item->getTag()==0) {if(runtime.canEnter()&&RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene()))m_launchedContext=preparationContextText(runtime.preparationContext());}
        if(item->getTag()==1)runtime.acknowledge();
        if(item->getTag()==2)runtime.surrender();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if(item->getTag()==3)showSurvivalForceRankPopup();
#endif
    }
    void scoreMotion(ScoreAnimationInput input) {
        auto const weak=WeakRef<SurvivalGameLayer>(this);
        queueInMainThread([weak,input=std::move(input)]() mutable {
            auto owner=weak.lock();if(!owner || !owner->isRunning())return;
            auto* score=ui::ResultScoreLayer::create(std::move(input),owner.data(),[weak] {
                if(auto layer=weak.lock()) {layer->m_motion=false;layer->setKeypadEnabled(true);}
            });
            if(score)owner->addChild(score,1100);else {owner->m_motion=false;owner->setKeypadEnabled(true);}
        });
    }
    void beginResultMotion() {
        auto& runtime=SurvivalRuntime::get();auto const& view=runtime.view();
        if(!view.result.isObject() || m_resultMotion==view.id)return;m_resultMotion=view.id;
        if(Mod::get()->getSavedValue<std::string>("survival-result-animation")==view.id)return;
        auto input=runtime.scoreAnimation();if(!input)return;
        Mod::get()->setSavedValue("survival-result-animation",view.id);m_motion=true;setKeypadEnabled(false);
        auto const weak=WeakRef<SurvivalGameLayer>(this);
        auto finish=[weak,input=*input] {if(auto owner=weak.lock())owner->scoreMotion(input);};
        if((input->promoted || input->demoted) && input->beforeTier!=input->afterTier) {
            auto* transition=ui::RankTransitionLayer::create(input->beforeTier,input->afterTier,
                input->promoted?ui::RankTransitionType::Promotion:ui::RankTransitionType::Demotion,
                text(input->promoted?"promoted":"demoted",input->promoted?"PROMOTED":"DEMOTED"),
                text("skip","SKIP"),text("continue","CONTINUE"),finish,true);
            if(transition){addChild(transition,1200);return;}
        }
        finish();
    }
public:
    static SurvivalGameLayer* create() {auto* layer=new SurvivalGameLayer;if(layer->init()){layer->autorelease();return layer;}delete layer;return nullptr;}
    void update(float) override {
        if(m_closed)return;auto& runtime=SurvivalRuntime::get();runtime.tick();
        if(!runtime.active() && !runtime.creating()) {m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});return;}
        auto const& view=runtime.view();auto const& preparation=RankedRoundPreparation::get().view();
        auto const result=view.result.isObject();m_resume->setVisible(!result);m_resume->setEnabled(!m_motion&&runtime.canEnter());
        m_continue->setVisible(result);m_continue->setEnabled(!m_motion&&!runtime.creating());m_surrender->setVisible(!result);
        m_heading->setString(result?fmt::format("SURVIVAL · {}",survivalOrdinal(static_cast<int>(view.result["rank"].asInt().unwrapOr(0)))).c_str():
            fmt::format("SURVIVAL · {}",view.phase=="TIEBREAK"?"DEATH MATCH":view.phase=="FINAL"?"FINAL":("ROUND "+std::to_string(view.roundNumber))).c_str());
        m_map->setString((view.map.levelId>0?fmt::format("{} · {} · GD #{}",view.group,view.state,view.map.levelId):view.group+" · "+view.state).c_str());
        std::string status;
        if(!view.error.empty())status=i18n::text(view.error,view.error);
        else if(result)status=fmt::format("{:+} LP · {} LP",view.result["actualDelta"].asInt().unwrapOr(0),view.result["profileAfter"]["lp"].asInt().unwrapOr(0));
        else if(view.state=="MAP_SELECTION")status=text("survival-selecting-map","SELECTING A SERVER MAP...");
        else if(!preparation.mapReady)status=text("downloading-map","DOWNLOADING MAP...");
        else if(!preparation.audioReady)status=text("downloading-audio","DOWNLOADING AUDIO...");
        else status=text("survival-game-running","THE SERVER TIMER CONTINUES. YOU CAN RESUME THIS GAME.");
        m_status->setString(status.c_str());ui::fitMenuLabelWidth(m_status,getContentSize().width-24,.28f,.19f);
        std::string records;
        if(result) {
            for(auto const& p:view.result["standings"].asArray().unwrapOr(std::vector<matjson::Value>{}))records+=fmt::format("{}  {}  {:+} LP\n",survivalOrdinal(static_cast<int>(p["rank"].asInt().unwrapOr(0))),
                p["participantId"].asString().unwrapOr("")==view.ownId?text("you","YOU"):p["name"].asString().unwrapOr(""),p["delta"].asInt().unwrapOr(0));
        } else {
            for(auto const& p:view.standings)records+=fmt::format("{}  {}  {:.3f}%\n",survivalOrdinal(p.rank),p.id==view.ownId?text("you","YOU"):p.name,p.score);
        }
        m_records->setString(records.c_str());ui::fitFullMenuLabel(m_records,330,getContentSize().height-140,.22f);
        if(view.state=="PREPARING" && RankedRoundPreparation::get().ready())runtime.assetsReady(RankedLevelPreparation::get().level());
        auto const context=preparationContextText(runtime.preparationContext());
        if(!m_motion && runtime.canEnter() && m_launchedContext!=context && RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene()))m_launchedContext=context;
        if(result)beginResultMotion();
    }
    void keyBackClicked() override {
        if(m_closed || m_motion)return;
        if(SurvivalRuntime::get().view().result.isObject()) {SurvivalRuntime::get().acknowledge();return;}
        m_closed=true;queueInMainThread([] {CCDirector::sharedDirector()->popScene();});
    }
    void onExit() override {m_exited=true;CCLayerColor::onExit();}
};
}
void showSurvivalGame() {
    auto* director=CCDirector::sharedDirector();auto* current=director->getRunningScene();
    if(!SurvivalRuntime::get().active() || (current && current->getChildByID("survival-game-coordinator"_spr)))return;
    if(auto* play=PlayLayer::get();play && SurvivalRuntime::get().gameplayLevel(static_cast<int>(play->m_level->m_levelID)))return;
    auto* layer=SurvivalGameLayer::create();if(!layer)return;
    auto* scene=CCScene::create();scene->addChild(layer);director->pushScene(scene);
}
}
