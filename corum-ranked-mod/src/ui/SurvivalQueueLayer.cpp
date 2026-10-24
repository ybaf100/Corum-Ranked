#include "../SurvivalRuntime.hpp"
#include "../RankedAudioManager.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include <Geode/ui/LoadingSpinner.hpp>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
class SurvivalQueueLayer final : public CCLayerColor {
    CCLabelBMFont *m_players=nullptr,*m_wait=nullptr,*m_status=nullptr,*m_minimum=nullptr;
    CCMenuItemSpriteExtra *m_leave=nullptr,*m_fill=nullptr;
    bool m_closed=false;
    CCMenuItemSpriteExtra* button(CCMenu* menu,std::string caption,CCPoint position,int tag,ccColor3B color) {
        auto* face=ui::surface({240,34},{},color,ui::SurfaceRaised,255,true);
        auto* label=ui::menuLabel(caption,.32f,{120,17},ui::Text,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(label,218,.32f,.24f);face->addChild(label,5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalQueueLayer::onAction));
        item->setPosition(position);item->setTag(tag);menu->addChild(item);return item;
    }
    bool init() override {
        if(!CCLayerColor::initWithColor({0,0,0,255}))return false;
        setID("survival-online-queue"_spr);setKeypadEnabled(true);
        auto const size=CCDirector::sharedDirector()->getWinSize();auto const center=CCPoint{size.width/2,size.height/2};
        addChild(ui::menuLabel("SURVIVAL",.58f,{center.x,size.height-32},ui::Cyan,ui::FontWeight::Black));
        addChild(ui::menuLabel(i18n::text("matchmaking","MATCHMAKING"),.34f,{center.x,size.height-59},ui::Text,ui::FontWeight::SemiBold));
        auto* spinner=LoadingSpinner::create(30);spinner->setPosition({center.x,center.y+38});addChild(spinner,5);
        m_players=ui::menuLabel("",.40f,{center.x,center.y-5},ui::Text,ui::FontWeight::ExtraBold);addChild(m_players);
        m_wait=ui::menuLabel("",.32f,{center.x,center.y-30},ui::Gold,ui::FontWeight::SemiBold);addChild(m_wait);
        m_minimum=ui::menuLabel(fmt::format("{}: 8",i18n::text("survival-queue-minimum","MINIMUM PLAYERS")),.26f,{center.x,center.y-50},ui::TextMuted,ui::FontWeight::SemiBold);addChild(m_minimum);
        auto* hint=ui::menuLabel(i18n::text("survival-queue-short","AFTER 180s, 2-7 PLAYERS PLAY SHORT FINALS"),.23f,{center.x,center.y-68},ui::TextMuted,ui::FontWeight::SemiBold);
        ui::fitMenuLabelWidth(hint,size.width-36,.23f,.20f);addChild(hint);m_status=hint;
        auto* menu=CCMenu::create();menu->setPosition(CCPointZero);addChild(menu,10);
        m_leave=button(menu,i18n::text("leave-queue","LEAVE QUEUE"),{center.x,27},0,ui::Red);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_fill=button(menu,i18n::text("survival-fill-bots","FILL WITH BOTS"),{center.x,68},1,ui::Blue);
#endif
        scheduleUpdate();return true;
    }
    void onAction(CCObject* sender) {
        if(m_closed || SurvivalRuntime::get().creating())return;
        auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);if(!item)return;
        if(item->getTag()==0)SurvivalRuntime::get().leaveQueue();
        if(item->getTag()==2){m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});}
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if(item->getTag()==1 && SurvivalRuntime::get().queue().status=="QUEUED")showSurvivalFillBotsPopup();
#endif
    }
public:
    static SurvivalQueueLayer* create(){auto* node=new SurvivalQueueLayer;if(node->init()){node->autorelease();return node;}delete node;return nullptr;}
    void update(float) override {
        if(m_closed)return;auto& runtime=SurvivalRuntime::get();runtime.tick();auto const& queue=runtime.queue();
        if(runtime.active()){
            m_closed=true;queueInMainThread([weak=WeakRef(this)]{
                auto owner=weak.lock();if(!owner || owner->getParent()!=CCDirector::sharedDirector()->getRunningScene())return;
                CCDirector::sharedDirector()->popScene();queueInMainThread([]{showSurvivalGame();});
            });return;
        }
        auto const waiting=runtime.queueWaitingSeconds();
        m_players->setString(fmt::format("{}: {} / {}",i18n::text("survival-queue-players","PLAYERS"),queue.players,queue.maximumPlayers).c_str());
        m_minimum->setString(fmt::format("{}: {}",i18n::text("survival-queue-minimum","MINIMUM PLAYERS"),queue.minimumPlayers).c_str());
        m_wait->setString(fmt::format("{}: {:02}:{:02}",i18n::text("waiting","WAITING"),waiting/60,waiting%60).c_str());
        m_status->setString((queue.error.empty()?fmt::format(fmt::runtime(i18n::text("survival-queue-short-format","AFTER {}s, SMALLER FIELDS MAY START")),queue.maxWaitSeconds):i18n::text(queue.error,queue.error)).c_str());m_status->setColor(queue.error.empty()?ui::TextMuted:ui::Red);ui::fitMenuLabelWidth(m_status,getContentSize().width-36,.24f,.20f);
        m_leave->setEnabled(!runtime.creating() && queue.status=="QUEUED");
        if(m_fill)m_fill->setEnabled(!runtime.creating() && queue.status=="QUEUED");
        if(queue.status!="QUEUED" && queue.status!="JOINING" && queue.status!="MATCHED" && !runtime.creating()){
            if(queue.error.empty()){m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});}
            else {m_leave->setEnabled(true);m_leave->setTag(2);}
        }
    }
    void keyBackClicked() override {
        if(m_closed || SurvivalRuntime::get().creating())return;
        if(SurvivalRuntime::get().queueing()){SurvivalRuntime::get().leaveQueue();return;}
        m_closed=true;queueInMainThread([]{CCDirector::sharedDirector()->popScene();});
    }
};
}
void showSurvivalQueue(){
    auto* director=CCDirector::sharedDirector();auto* current=director->getRunningScene();
    if(current && current->getChildByID("survival-online-queue"_spr))return;
    if(auto* layer=SurvivalQueueLayer::create()){auto* scene=CCScene::create();scene->addChild(layer);director->pushScene(scene);}
}
}
