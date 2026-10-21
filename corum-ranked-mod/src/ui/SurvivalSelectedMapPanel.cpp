#include "SurvivalSelectedMapPanel.hpp"
#include "SurvivalOptionSwitch.hpp"
#include "RankedTranslations.hpp"
#include "MapDifficultyBadge.hpp"
#include "../domain/MapFeaturePresentation.hpp"
#include "ModeLobbyMotion.hpp"
#include "../domain/SurvivalMapSelection.hpp"
#include "RankedInput.hpp"
#include "../domain/RoundResourcePolicy.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <Geode/binding/SongInfoObject.hpp>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
CCPoint center(SurvivalRect const& r){return {r.cx(),r.cy()};}
void line(CCLabelBMFont* label,std::string text,SurvivalRect r,float scale,bool ellipsis=false){
    if(!label)return;label->setWidth(-1);label->setString(text.c_str());setLabelScale(label,rankedMenuTextScale(scale));
    auto const height=label->getContentSize().height*label->getScale();if(height>r.h)label->setScale(label->getScale()*r.h/height);
    if(ellipsis){while(text.size()>3 && label->getContentSize().width*label->getScale()>r.w){
        auto start=text.size()-1;while(start>0 && (static_cast<unsigned char>(text[start])&0xc0)==0x80)--start;
        text.resize(start);label->setString((text+"...").c_str());
    }}
    auto const width=label->getContentSize().width*label->getScale();if(width>r.w)label->setScale(label->getScale()*r.w/width);
    label->setPosition(center(r));
}
}
SurvivalSelectedMapPanel* SurvivalSelectedMapPanel::create(std::function<void(int)> action){
    auto* p=new SurvivalSelectedMapPanel;
    if(p->initWithColor({0,0,0,0},0,0)){p->m_action=std::move(action);p->setCascadeOpacityEnabled(true);p->setID("survival-selected-map-panel"_spr);p->autorelease();return p;}
    delete p;return nullptr;
}
CCNode* SurvivalSelectedMapPanel::readyFace(bool enabled,bool retry,bool start){
    (void)start;auto const& r=m_layout.ready;auto* face=surface({r.w,r.h},{},retry?Blue:ccc3(255,237,139),retry?SurfaceRaised:Gold,255,true);
    auto* label=menuLabel(i18n::text(retry?"retry":"survival-reenter",retry?"RETRY":"RE-ENTER"),.34f,{r.w/2,r.h/2},retry?Text:Ink,FontWeight::Black);
    fitSurvivalSingleLine(label,r.w-16,r.h-10,.34f);face->addChild(label,5);if(!enabled)grayButtonVisual(face);return face;
}
void SurvivalSelectedMapPanel::build(CCSize size,bool thumbnail,bool ldm){
    if(m_menu)cancelMenuTouch(m_menu);
    // Cached values belong to the removed labels/nodes, not just the level.
    // Rebuilding for LDM/thumbnail changes must render every field again and
    // must never remove a difficulty node already destroyed by this cleanup.
    m_difficulty=nullptr;m_levelId=-1;m_metadataRevision=0;m_mapMetadata={};
    m_creatorText.clear();m_statusText.clear();m_songText.clear();
    removeAllChildrenWithCleanup(true);m_thumbnail=thumbnail;m_ldmSupported=ldm;m_layout=survivalSelectionLayout(size.width,size.height,thumbnail,ldm);m_built=true;
    auto const& r=m_layout;auto* panel=surface({r.panel.w,r.panel.h},center(r.panel),Blue,Surface,250,true);addChild(panel,0);
    auto caption=[&](SurvivalRect rect,ccColor3B color,FontWeight weight){auto* value=menuLabel("",.25f,center(rect),color,weight);addChild(value,5);return value;};
    m_title=caption(r.title,Text,FontWeight::Black);m_creator=caption(r.creator,TextMuted,FontWeight::SemiBold);
    m_rating=caption({r.creator.x+r.creator.w*.70f,r.creator.y,r.creator.w*.30f,r.creator.h},Gold,FontWeight::ExtraBold);m_rating->setID("survival-selected-map-rating"_spr);
    m_metadata=caption(r.metadata,Cyan,FontWeight::SemiBold);m_readiness=caption(r.readiness,TextMuted,FontWeight::SemiBold);
    m_song=caption(r.song,TextMuted,FontWeight::SemiBold);m_song->setID("survival-selected-map-song"_spr);
    m_countdown=caption(r.ready,Gold,FontWeight::Black);m_countdown->setID("survival-automatic-entry-countdown"_spr);
    // Verified native frame used by Geode ModsLayer; never an emoji substitute.
    m_star=CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
    if(m_star){m_star->setScale(std::min(12*r.scale/m_star->getContentSize().height,12*r.scale/m_star->getContentSize().width));m_star->setPosition({r.metadata.cx()-12*r.scale,r.metadata.cy()-12*r.scale});addChild(m_star,5);}
    m_stars=caption({r.metadata.cx()-4*r.scale,r.metadata.cy()-18*r.scale,28*r.scale,12*r.scale},Gold,FontWeight::ExtraBold);
    m_difficulty=nullptr;m_levelId=0;m_metadataRevision=0;m_mapMetadata={};m_creatorText.clear();m_statusText.clear();
    auto* menu=CCMenu::create();m_menu=menu;menu->setPosition(CCPointZero);addChild(menu,10);
    auto item=[&](SurvivalRect rect,CCNode* face,int tag,char const* id){
        auto* b=CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalSelectedMapPanel::onAction));b->setPosition(center(rect));b->setTag(tag);b->setID(id);menu->addChild(b);return b;
    };
    m_ldm=item(r.ldm,ldm?survivalOptionFace({r.ldm.w,r.ldm.h},"LDM",m_options.lowDetail):survivalUnavailableLdmFace({r.ldm.w,r.ldm.h}),4,"survival-round-ldm");
    m_shake=item(r.shake,survivalOptionFace({r.shake.w,r.shake.h},i18n::text("disable-shake","Disable Shake"),m_options.disableShake),5,"survival-round-shake");
    auto* jukebox=surface({r.jukebox.w,r.jukebox.h},{},Blue,SurfaceRaised,255,false);
    // The existing public native widget opens Jukebox's real song selector.
    // Track arrows/selection remain owned by that selector, not a private ABI.
    auto* jukeboxCaption=menuLabel("Jukebox",.22f,{r.jukebox.w/2,r.jukebox.h/2},Text,FontWeight::ExtraBold);
    fitSurvivalSingleLine(jukeboxCaption,r.jukebox.w-10,r.jukebox.h-8,.22f);jukebox->addChild(jukeboxCaption,5);
    m_jukebox=item(r.jukebox,jukebox,6,"survival-map-jukebox");
    m_readyEnabled=false;m_retry=false;m_start=false;m_ready=item(r.ready,readyFace(false),0,"survival-map-ready");
}
void SurvivalSelectedMapPanel::onAction(CCObject* sender){if(auto* b=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);b && b->isEnabled() && m_action)m_action(b->getTag());}
void SurvivalSelectedMapPanel::suspend(){
    if(m_menu)cancelMenuTouch(m_menu);
    for(auto* item:{m_ldm,m_shake,m_jukebox,m_ready})if(item)item->setEnabled(false);
}
void SurvivalSelectedMapPanel::present(CCSize size,bool thumbnail,SurvivalMatchView const& view,RoundPreparationView const& prep,
    SurvivalNativeOptions options,bool supportsLdm,bool actionsEnabled,bool canEnter,double appearance){
    if(!m_built || thumbnail!=m_thumbnail || supportsLdm!=m_ldmSupported){m_options=options;build(size,thumbnail,supportsLdm);}
    auto const& r=m_layout;
    if(supportsLdm && m_options.lowDetail!=options.lowDetail)m_ldm->setNormalImage(survivalOptionFace({r.ldm.w,r.ldm.h},"LDM",options.lowDetail));
    if(m_options.disableShake!=options.disableShake)m_shake->setNormalImage(survivalOptionFace({r.shake.w,r.shake.h},i18n::text("disable-shake","Disable Shake"),options.disableShake));
    m_options=options;auto const interactive=appearance>=1. && actionsEnabled;
    auto* level=RankedLevelPreparation::get().level();auto const native=level && static_cast<int>(level->m_levelID)==view.map.levelId;
    m_ldm->setEnabled(interactive && native && prep.mapReady && supportsLdm);m_shake->setEnabled(interactive && native && prep.mapReady);m_jukebox->setEnabled(interactive && native && prep.mapReady);
    auto const retry=!prep.error.empty();auto const start=SurvivalRuntime::get().previouslyEntered() || SurvivalRuntime::get().observing();
    auto const ready=interactive && (retry || canEnter);
    if(ready!=m_readyEnabled || retry!=m_retry || start!=m_start){
        m_readyEnabled=ready;m_retry=retry;m_start=start;
        m_ready->setNormalImage(readyFace(ready,retry,start));
    }
    m_ready->setTag(retry?10:0);m_ready->setVisible(retry || start);m_ready->setEnabled(ready && (retry || start));
    m_countdown->setVisible(!retry && !start);
    auto const wait=prep.mapReady && prep.audioReady?SurvivalRuntime::get().selectedPanelWaitMs():std::nullopt;
    auto const beat=survivalReadyFrame(wait);
    auto const countdown=beat.digit>0?std::to_string(beat.digit):i18n::text(canEnter?"survival-auto-start":"survival-preparing",canEnter?"AUTO START":"PREPARING...");
    line(m_countdown,countdown,r.ready,beat.digit>0?.90f:.30f);
    m_countdown->setScale(m_countdown->getScale()*static_cast<float>(beat.scale));m_countdown->setOpacity(static_cast<GLubyte>(beat.opacity*appearance*255));
    if(native){RankedMapDetails::get().remember(level);RankedMapDetails::get().requestDemonTier(view.map.levelId);}
    std::string songName;
    if(native){
        std::vector<int> ids;if(level->m_songID>0)ids.push_back(level->m_songID);
        appendRoundResourceIds(ids,std::string_view(level->m_songIDs.c_str()));
        auto* info=ids.empty()?LevelTools::getSongObject(level->m_audioTrack):MusicDownloadManager::sharedState()->getSongInfoObject(ids.front());
        if(info)songName=std::string(info->m_songName);
    }
    auto const song="Song - "+(songName.empty()?i18n::text("loading-uppercase","LOADING"):songName);
    if(song!=m_songText){m_songText=song;line(m_song,song,r.song,.19f,true);}
    auto metadata=RankedMapDetails::get().metadata(view.map.levelId);
    if(metadata.kind=="UNKNOWN"){metadata.kind=view.map.demon==SurvivalDemon::None?"NORMAL":"DEMON";metadata.stars=view.map.stars;}
    if(!metadata.stars && view.map.stars>0)metadata.stars=view.map.stars;
    if(!metadata.lengthCategory && view.map.length>=0 && view.map.length<=4)metadata.lengthCategory=view.map.length;
    if(!metadata.featureScore && view.certificate["featureScore"].isNumber())metadata.featureScore=static_cast<int>(view.certificate["featureScore"].asInt().unwrapOr(0));
    if(!metadata.epic && view.certificate["epic"].isNumber())metadata.epic=static_cast<int>(view.certificate["epic"].asInt().unwrapOr(0));
    auto const title=native?std::string(level->m_levelName):i18n::text("downloading-map","DOWNLOADING MAP...");
    if(title!=m_title->getString()){m_title->setString(title.c_str());fitFullMenuLabel(m_title,r.title.w,r.title.h,.43f);}
    auto creator=native?"by "+std::string(level->m_creatorName):i18n::text("survival-map-data-pending","Map data pending");
    if(creator!=m_creatorText){m_creatorText=creator;line(m_creator,creator,{r.creator.x,r.creator.y,r.creator.w*.68f,r.creator.h},.22f,true);}
    auto const revision=RankedMapDetails::get().revision();
    if(m_levelId!=view.map.levelId || m_metadataRevision!=revision || m_mapMetadata!=metadata){
        m_levelId=view.map.levelId;m_metadataRevision=revision;m_mapMetadata=metadata;
        line(m_rating,std::string(mapFeatureGrade(metadata)),{r.creator.x+r.creator.w*.70f,r.creator.y,r.creator.w*.30f,r.creator.h},.19f);
        m_rating->setColor(metadata.epic.value_or(0)>0?Gold:metadata.featureScore.value_or(0)>0?Cyan:TextMuted);
        if(m_difficulty)m_difficulty->removeFromParentAndCleanup(true);
        m_difficulty=mapDifficultyBadge(view.map.levelId,metadata,r.difficulty.w,r.difficulty.h,false);m_difficulty->setPosition({r.difficulty.x,r.difficulty.y});addChild(m_difficulty,5);
        auto const detail=fmt::format("GD #{}\n{}",view.map.levelId,mapLengthText(metadata));line(m_metadata,detail,{r.metadata.x,r.metadata.y+6*r.scale,r.metadata.w,r.metadata.h},.21f);
        m_stars->setString(metadata.stars?std::to_string(*metadata.stars).c_str():"--");fitFullMenuLabel(m_stars,28*r.scale,12*r.scale,.22f);
    }
    std::string status;
    if(SurvivalRuntime::get().surrenderPending())status=i18n::text("survival-surrender-pending","Surrender submitted · waiting for server");
    else if(!view.error.empty())status=i18n::text(view.error,view.error);
    else if(!prep.error.empty())status=i18n::text("survival-prep-retry","Preparation failed · RETRY")+": "+i18n::text(prep.error,prep.error);
    else if(auto wait=SurvivalRuntime::get().selectedPanelWaitMs();wait && *wait>0)
        status=fmt::format(fmt::runtime(i18n::text("survival-entry-wait","ALL READY · ENTERING IN {}s")),(*wait+999)/1000);
    else status=fmt::format("{} {}   /   {} {}",i18n::text("map","MAP"),i18n::text(prep.mapReady?"ready":"loading",prep.mapReady?"READY":"LOADING"),
        i18n::text("audio","AUDIO"),i18n::text(prep.audioReady?"ready":"loading",prep.audioReady?"READY":"LOADING"));
    if(status!=m_statusText){m_statusText=status;line(m_readiness,status,r.readiness,.19f,true);}m_readiness->setColor(!prep.error.empty()?Red:prep.mapReady && prep.audioReady?Green:TextMuted);
    setPositionY(static_cast<float>((1-survivalEase(appearance))*-10*r.scale));
    setVisible(appearance>0);setModeLobbyOpacity(this,static_cast<GLubyte>(std::clamp(appearance,0.,1.)*255));
    auto reveal=[&](CCNode* node,double delay){auto const alpha=appearance>=1?1.:std::clamp((appearance*.80-delay)/.60,0.,1.);if(node)propagateModeOpacity(node,static_cast<GLubyte>(alpha*255));};
    reveal(m_title,0);reveal(m_creator,.05);reveal(m_rating,.05);reveal(m_difficulty,.10);reveal(m_metadata,.10);reveal(m_star,.10);reveal(m_stars,.10);reveal(m_song,.15);
    for(auto* item:{m_ldm,m_shake,m_jukebox,m_ready})reveal(item,.20);
    m_countdown->setOpacity(static_cast<GLubyte>(beat.opacity*appearance*255));
}
}
