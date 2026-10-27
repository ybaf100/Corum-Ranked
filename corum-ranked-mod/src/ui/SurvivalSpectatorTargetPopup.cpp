#include "../SurvivalSpectatorRuntime.hpp"
#include "../SurvivalRuntime.hpp"
#include "../RankedRoundPreparation.hpp"
#include "SurvivalOutcomePanel.hpp"
#include <Geode/ui/Popup.hpp>
using namespace geode::prelude;
namespace corum::ranked {
class SurvivalSpectatorTargetPopup final:public Popup {
    struct Card{std::string id;CCMenuItemSpriteExtra* item=nullptr;CCLabelBMFont* name=nullptr;CCLabelBMFont* score=nullptr;CCLabelBMFont* status=nullptr;};
    std::vector<Card> m_cards;std::string m_identity,m_rosterKey;std::size_t m_page=0,m_pageSize=16;
    CCLabelBMFont* m_clock=nullptr;CCLabelBMFont* m_pageLabel=nullptr;CCLabelBMFont* m_message=nullptr;CCNode* m_grid=nullptr;CCMenu* m_watchMenu=nullptr;
    bool init()override{
        if(!Popup::init(490,302))return false;setID("survival-live-targets"_spr);m_bgSprite->setVisible(false);
        auto const size=CCDirector::sharedDirector()->getWinSize();m_mainLayer->setScale(std::min({1.f,(size.width-12)/490,(size.height-12)/302}));
        m_mainLayer->addChild(ui::surface({490,302},{245,151},ui::Blue,ui::Surface,255,true));
        m_mainLayer->addChild(ui::menuLabel("SURVIVAL · LIVE SPECTATE",.36f,{245,280},ui::Cyan,ui::FontWeight::Black));
        m_clock=ui::menuLabel("",.22f,{245,258},ui::Text);m_mainLayer->addChild(m_clock);
        m_message=ui::menuLabel("",.18f,{245,42},ui::TextMuted);m_mainLayer->addChild(m_message);
        m_pageLabel=ui::menuLabel("",.23f,{245,18},ui::Text);m_mainLayer->addChild(m_pageLabel);
        for(int i:{-1,1}){auto* face=ui::surface({44,24},{},ui::Blue,ui::SurfaceRaised,255,true);face->addChild(ui::menuLabel(i<0?"<":">",.26f,{22,12},ui::Text));auto* b=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalSpectatorTargetPopup::onPage));b->setTag(i);b->setPosition({245.f+70*i,18});m_buttonMenu->addChild(b);}
        auto* backFace=ui::surface({86,24},{},ui::Blue,ui::SurfaceRaised,255,true);backFace->addChild(ui::menuLabel(i18n::text("back","BACK"),.22f,{43,12},ui::Text));
        auto* back=CCMenuItemSpriteExtra::create(backFace,this,menu_selector(SurvivalSpectatorTargetPopup::onBack));back->setPosition({60,18});m_buttonMenu->addChild(back);
        m_grid=CCNode::create();m_mainLayer->addChild(m_grid);m_watchMenu=CCMenu::create();m_watchMenu->setPosition({0,0});m_grid->addChild(m_watchMenu);
        m_identity=SurvivalRuntime::get().view().id;SurvivalSpectatorRuntime::get().selectionVisible(true);scheduleUpdate();update(0);return true;
    }
    void onBack(CCObject* sender){onClose(sender);}
    void onPage(CCObject* object){auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);if(!item)return;auto n=std::max<std::size_t>(1,(SurvivalSpectatorRuntime::get().targets().size()+m_pageSize-1)/m_pageSize);m_page=item->getTag()<0?(m_page+n-1)%n:(m_page+1)%n;m_rosterKey.clear();}
    void onWatch(CCObject* object){auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);if(!item||item->getTag()<0||static_cast<std::size_t>(item->getTag())>=m_cards.size())return;auto id=m_cards[item->getTag()].id;SurvivalSpectatorRuntime::get().select(std::move(id));onClose(nullptr);}
    void onInfo(CCObject* object){auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(object);if(!item||item->getTag()<0||static_cast<std::size_t>(item->getTag())>=m_cards.size())return;for(auto const& row:ui::survivalRoster(SurvivalRuntime::get().view()))if(row.id==m_cards[item->getTag()].id){ui::openSurvivalProfile(this,row);break;}}
    void onClose(CCObject* sender)override{SurvivalSpectatorRuntime::get().selectionVisible(false);Popup::onClose(sender);}
    void update(float)override{
        auto& live=SurvivalSpectatorRuntime::get();auto const& sur=SurvivalRuntime::get();auto const&v=sur.view();
        if(v.id!=m_identity||v.state=="FINAL_RESULT"||v.state=="CANCELLED"){onClose(nullptr);return;}
        auto const left=sur.roundRemainingMs().value_or(0)/1000;auto clock=fmt::format("ROUND {} · {:02}:{:02}",v.roundNumber,left/60,left%60);if(clock!=m_clock->getString())m_clock->setString(clock.c_str());
        auto const& targets=live.targets();bool compact=v.matchPlayerCount==64&&(v.phase=="ROUND_1"||v.phase=="ROUND_2");m_pageSize=compact?24:16;
        auto const pages=std::max<std::size_t>(1,(targets.size()+m_pageSize-1)/m_pageSize);m_page%=pages;
        std::string key=std::to_string(m_page)+":"+std::to_string(m_pageSize);for(auto const&t:targets)key+=":"+t.id+":"+t.name;
        if(m_rosterKey!=key){m_rosterKey=key;m_grid->removeAllChildrenWithCleanup(true);m_cards.clear();m_watchMenu=CCMenu::create();m_watchMenu->setPosition({0,0});m_grid->addChild(m_watchMenu);
            for(std::size_t i=m_page*m_pageSize;i<std::min(targets.size(),(m_page+1)*m_pageSize);++i){auto const&t=targets[i];auto const cell=i%m_pageSize;float h=compact?29:43,x=65.f+120*(cell%4),y=compact?238.f-32*(cell/4):232.f-48*(cell/4);
                auto* face=ui::surface({82,h},{},ui::Blue,ui::SurfaceRaised,255,true);
                auto* name=ui::playerNameLabel(t.name,.18f,{43,h-7},ui::Text);ui::fitSingleLineMenuLabel(name,62,11,.18f);face->addChild(name,5);
                auto* score=ui::menuLabel("",compact?.12f:.15f,{43,h-17},ui::Text);face->addChild(score,5);
                auto* status=ui::menuLabel("",.12f,{43,compact?3.f:8.f},ui::TextMuted);face->addChild(status,5);
                SurvivalStanding row;row.id=t.id;row.name=t.name;row.bot=t.bot;row.accountId=t.account;
                if(auto* avatar=ui::survivalAvatar(row,v.ownId,11)){avatar->setPosition({8,h/2});face->addChild(avatar,5);}
                auto* info=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalSpectatorTargetPopup::onInfo));info->setPosition({x-15,y});info->setTag(static_cast<int>(m_cards.size()));m_watchMenu->addChild(info);
                auto* watchFace=ui::surface({27,h-4},{},ui::Cyan,ui::Surface,255,true);watchFace->addChild(ui::menuLabel(i18n::text("survival-watch","WATCH"),.12f,{13.5f,(h-4)/2},ui::Cyan));
                auto* watch=CCMenuItemSpriteExtra::create(watchFace,this,menu_selector(SurvivalSpectatorTargetPopup::onWatch));watch->setPosition({x+40,y});watch->setTag(static_cast<int>(m_cards.size()));m_watchMenu->addChild(watch);
                m_cards.push_back({t.id,watch,name,score,status});
            }
        }
        for(auto&c:m_cards){auto found=std::find_if(targets.begin(),targets.end(),[&](auto const&t){return t.id==c.id;});if(found==targets.end())continue;
            auto const&t=*found;auto text=fmt::format("#{} B{:.1f}% N{:.1f}%",t.rank,t.best,t.now);if(text!=c.score->getString())c.score->setString(text.c_str());ui::fitSingleLineMenuLabel(c.score,66,9,compact?.12f:.15f);
            auto status=t.bot?i18n::text("survival-no-native-stream","NO NATIVE STREAM"):t.status=="CAPACITY"?i18n::text("survival-spectator-capacity","RELAY FULL"):t.status=="CLEARED"?i18n::text("cleared","CLEARED"):t.status=="PLAYING"?i18n::text("playing","PLAYING"):i18n::text("survival-waiting-player","WAITING FOR PLAYER");
            if(status!=c.status->getString())c.status->setString(status.c_str());ui::fitSingleLineMenuLabel(c.status,66,8,.12f);c.item->setEnabled(t.selectable&&!t.bot);c.item->setVisible(t.selectable&&!t.bot);
        }
        auto pageText=fmt::format("{} / {}",m_page+1,pages);if(pageText!=m_pageLabel->getString())m_pageLabel->setString(pageText.c_str());
        auto message=!v.online?i18n::text("survival-live-online-only","Live spectator requires an online human match."):!live.error().empty()?live.error():targets.empty()?i18n::text("survival-no-targets","NO SURVIVORS AVAILABLE"):live.active()?i18n::text("survival-preparing-watch","PREPARING MAP / AUDIO..."):"";
        if(message!=m_message->getString()){m_message->setString(message.c_str());ui::fitFullMenuLabel(m_message,450,18,.18f);}
    }
public:
    static SurvivalSpectatorTargetPopup* create(){auto*p=new SurvivalSpectatorTargetPopup;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
};
void showSurvivalSpectatorTargets(){auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene||scene->getChildByIDRecursive("survival-live-targets"_spr))return;if(auto*p=SurvivalSpectatorTargetPopup::create())p->show();}
}
