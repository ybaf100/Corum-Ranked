#include "RankedCreditsPopup.hpp"
#include "../RankedRuntime.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedPopupLayout.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <set>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
class RankedCreditsPopup final:public Popup {
    struct Credit {int id=0,requests=0;std::string text;CCLabelBMFont* label=nullptr;std::chrono::steady_clock::time_point next{};};
    std::vector<Credit> m_songs;bool m_closed=false,m_exited=false;
    bool init() override{
        auto const layout=rankedPopupLayout({350,294},CCDirector::sharedDirector()->getWinSize());
        if(!Popup::init(layout.size.width,layout.size.height))return false;
        m_noElasticity=true;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-credits-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        content->addChild(surface({350,294},{175,147},Cyan,Surface,255,true));
        auto add=[&](std::string text,CCPoint at,ccColor3B tint,float scale){auto* label=menuLabel(text,scale,at,tint,FontWeight::ExtraBold);fitFullMenuLabel(label,316,26,scale);content->addChild(label,5);};
        add("CREDITS",{175,267},Cyan,.49f);
        add(i18n::text("credits-intro","Planning and music behind Corum Ranked"),{175,247},TextMuted,.21f);
        content->addChild(surface({318,34},{175,216},Blue,SurfaceRaised,250,false));
        add(i18n::text("credits-planner","Planner"),{71,216},TextMuted,.26f);
        add("hwanhee1",{231,216},Gold,.34f);
        add(i18n::text("credits-music","Music"),{175,183},Text,.30f);
        std::map<int,std::set<std::string>> songs;
        std::vector<int> order;
        for(auto const& resource:RankedRuntime::get().view().client.audio.resources){
            if(resource.songId<=0)continue;
            if(!songs.contains(resource.songId)){if(songs.size()>=16)continue;order.push_back(resource.songId);}
            auto use=resource.label.empty()?resource.key:resource.label;
            if(resource.key=="menu")use=i18n::text("admin-audio-menu","Lobby");
            else if(resource.key=="menu_promotion")use=i18n::text("admin-audio-promotion","Promotion lobby");
            else if(resource.key=="menu_demotion_warning")use=i18n::text("admin-audio-warning","Demotion warning lobby");
            else if(resource.key=="match")use=i18n::text("credits-rounds-one-two","Rounds 1 & 2");
            else if(resource.key=="round3_deathmatch_wait")use=i18n::text("admin-audio-deathmatch","Round 3 / Death Match wait");
            else if(resource.key=="result_win")use=i18n::text("credits-result","Match results");
            songs[resource.songId].insert(std::move(use));
        }
        auto* scroll=ScrollLayer::create({326,129});scroll->setPosition({12,44});content->addChild(scroll,10);
        auto const h=std::max(129.f,static_cast<float>(songs.size())*61+8);scroll->m_contentLayer->setContentSize({326,h});
        int n=0;for(auto const id:order){
            auto const& uses=songs.at(id);
            std::string scope;for(auto const& use:uses){if(!scope.empty())scope+=" · ";scope+=use;}
            auto const y=h-18-n++*61;
            scroll->m_contentLayer->addChild(surface({314,55},{163,y-10},Blue,SurfaceRaised,245,false),1);
            auto* usage=menuLabel(scope,.20f,{163,y},TextMuted,FontWeight::SemiBold);fitFullMenuLabel(usage,306,22,.20f);scroll->m_contentLayer->addChild(usage,5);
            auto* title=menuLabel(i18n::text("credits-song-loading","Loading song credits..."),.29f,{163,y-25},Text,FontWeight::ExtraBold);fitFullMenuLabel(title,306,28,.29f);scroll->m_contentLayer->addChild(title,5);m_songs.push_back({id,0,"",title,{}});
        }
        if(songs.empty())add(i18n::text("credits-no-music","No configured music"),{175,137},TextMuted,.27f);
        scroll->scrollToTop();
        auto* face=surface({120,28},{},Blue,SurfaceRaised,255,true);auto* text=menuLabel(i18n::text("close","Close"),.32f,{60,14},Text,FontWeight::ExtraBold);face->addChild(text,5);
        auto* close=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedCreditsPopup::onClose));close->setPosition({175,22});m_buttonMenu->addChild(close);
        MusicDownloadManager::sharedState()->tryLoadLibraries();schedule(schedule_selector(RankedCreditsPopup::refresh),.25f);refresh(0);return true;
    }
    void refresh(float){
        if(m_closed)return;auto* manager=MusicDownloadManager::sharedState();if(!manager)return;
        auto const now=std::chrono::steady_clock::now();
        for(auto& song:m_songs){
            auto* info=manager->getSongInfoObject(song.id);
            if(info && !info->m_songName.empty() && !info->m_artistName.empty()){
                auto text=std::string(info->m_artistName)+" - "+std::string(info->m_songName);
                if(text!=song.text){song.text=std::move(text);song.label->setString(song.text.c_str());fitFullMenuLabel(song.label,306,28,.29f);}continue;
            }
            if(song.requests<2 && now>=song.next && !manager->isDLActive(manager->getSongDownloadKey(song.id))){
                ++song.requests;song.next=now+std::chrono::seconds(8);manager->getSongInfo(song.id,true);
            }else if(song.requests>=2 && now>=song.next && song.text.empty()){
                song.text=i18n::text("info-unavailable","Unavailable")+fmt::format(" · GD Song #{}",song.id);song.label->setString(song.text.c_str());fitFullMenuLabel(song.label,306,28,.29f);
            }
        }
    }
public:
    static RankedCreditsPopup* create(){auto* p=new RankedCreditsPopup;if(p->init()){p->autorelease();return p;}delete p;return nullptr;}
    void onClose(CCObject* sender) override{if(m_closed)return;m_closed=true;unschedule(schedule_selector(RankedCreditsPopup::refresh));Popup::onClose(sender);}
    void onExit()override{m_closed=true;unschedule(schedule_selector(RankedCreditsPopup::refresh));if(!m_exited){m_exited=true;CCTouchDispatcher::get()->unregisterForcePrio(this);}Popup::onExit();}
};
}
void showRankedCredits(){auto* scene=CCDirector::sharedDirector()->getRunningScene();if(!scene || scene->getChildByIDRecursive("ranked-credits-popup"_spr))return;if(auto* popup=RankedCreditsPopup::create())popup->show();}
}
