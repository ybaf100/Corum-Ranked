#include "SurvivalMapSelectionLayer.hpp"
#include "../SurvivalThumbnailProvider.hpp"
#include "../RankedVolumeControl.hpp"
#include "../domain/SurvivalBoardPresentation.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <chrono>
#include <map>
#include <random>
using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
std::int64_t monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::uint64_t sceneSequence=0;
struct RevealRecord {std::int64_t began=0;std::size_t consumed=0;bool complete=false,centerPlayed=false;SurvivalRevealModeChoice mode;};
// Retained scenes normally keep the timeline. The bounded registry also covers
// a recreated coordinator without replaying elapsed locks or a second reveal.
std::map<std::string,RevealRecord> reveals;
CCClippingNode* clip(CCSize size){
    auto* mask=CCDrawNode::create();CCPoint points[]{{0,0},{size.width,0},{size.width,size.height},{0,size.height}};
    mask->drawPolygon(points,4,{1,1,1,1},0,{1,1,1,1});auto* result=CCClippingNode::create(mask);result->setContentSize(size);return result;
}
CCLayerColor* group(){auto* value=CCLayerColor::create({0,0,0,0},0,0);value->setCascadeOpacityEnabled(true);return value;}
struct ThumbnailCard {
    CCLayerColor* root=nullptr;CCSprite* image=nullptr;CCLayerColor* shade=nullptr;CCNode* trim=nullptr;
    CCTexture2D* texture=nullptr;float width=0,height=0;
    void make(CCNode* parent,float w){width=w;height=w*9/16;root=group();parent->addChild(root);
        auto* face=surface({width,height},{},Blue,SurfaceRaised,255,true);root->addChild(face);
        auto* viewport=clip({width-4,height-4});viewport->setPosition({2,2});face->addChild(viewport,4);
        image=CCSprite::create();image->setPosition({(width-4)/2,(height-4)/2});viewport->addChild(image);
        shade=CCLayerColor::create({2,7,21,160},width-4,height-4);shade->setPosition({2,2});face->addChild(shade,5);
        trim=CCLayerColor::create({Gold.r,Gold.g,Gold.b,255},width-8,2);trim->setPosition({4,height-4});face->addChild(trim,6);
    }
    void set(CCTexture2D* next){if(texture==next)return;texture=next;image->setVisible(next!=nullptr);if(!next)return;
        image->setTexture(next);auto const size=next->getContentSize();image->setTextureRect({0,0,size.width,size.height});
        image->setScale(std::max((width-4)/size.width,(height-4)/size.height));
    }
};
void fitSingle(CCLabelBMFont* label,float width,float height,float maximum){
    label->setWidth(-1);setLabelScale(label,rankedMenuTextScale(maximum));
    auto const size=label->getContentSize();if(size.width>0 && size.height>0)label->setScale(std::min({label->getScale(),width/size.width,height/size.height}));
}
}
struct SurvivalMapSelectionLayer::Visuals {
    struct Reel {CCNode* root=nullptr;std::array<CCLabelBMFont*,3> labels{};std::array<float,3> scales{};CCLayerColor* impact=nullptr;};
    CCNode* reels=nullptr;CCClippingNode* carousel=nullptr;CCLayerColor* decoration=nullptr;
    std::vector<Reel> columns;std::array<ThumbnailCard,5> cards;ThumbnailCard selected;
    std::vector<std::pair<std::uint64_t,Ref<CCTexture2D>>> previews;
    Ref<CCTexture2D> finalTexture;
    float reelHeight=54,reelY=0,cardWidth=164;
    bool cardsBuilt=false,previewsFrozen=false;
};
SurvivalMapSelectionLayer::SurvivalMapSelectionLayer()=default;
SurvivalMapSelectionLayer::~SurvivalMapSelectionLayer()=default;
bool SurvivalMapSelectionLayer::init(std::function<void(int)> action){
    auto const size=CCDirector::sharedDirector()->getWinSize();if(!initWithColor({4,9,22,255},size.width,size.height))return false;
    setID("survival-map-selection"_spr);m_action=std::move(action);m_sceneGeneration=++sceneSequence;
    addBackdrop(this,size);
    auto* fragments=CCDrawNode::create();addChild(fragments,-10);
    for(int i=0;i<8;++i){auto const x=size.width*(i%2?.92f:.08f),y=size.height*(.18f+i*.085f);
        CCPoint ray[]{{size.width/2,size.height*.48f},{x-13,y-8},{x+13,y+8}};
        fragments->drawPolygon(ray,3,{.15f,.30f,.65f,.11f},0,{0,0,0,0});
    }
    m_header=menuLabel("NEXT UP!",.83f,{},Text,FontWeight::Black);addChild(m_header,15);
    m_subtitle=menuLabel("MAP SELECTION",.23f,{},Cyan,FontWeight::ExtraBold);addChild(m_subtitle,15);
    m_clock=menuLabel("",.25f,{},Gold,FontWeight::Black);addChild(m_clock,16);
    m_hint=menuLabel("",.18f,{},TextMuted,FontWeight::SemiBold);addChild(m_hint,15);
    m_finalId=menuLabel("",.8f,{},Gold,FontWeight::Black,countdownFont());addChild(m_finalId,15);
    m_pendingLabel=menuLabel(i18n::text("survival-selecting-map","SELECTING A SERVER MAP..."),.3f,{size.width/2,size.height/2},Cyan,FontWeight::ExtraBold);
    fitFullMenuLabel(m_pendingLabel,size.width-48,40,.3f);m_pendingLabel->setVisible(false);addChild(m_pendingLabel,15);
    m_panel=SurvivalSelectedMapPanel::create([weak=WeakRef(this)](int tag){
        auto owner=weak.lock();if(!owner || !owner->isRunning() || !owner->m_action)return;
        auto const& c=owner->m_timeline.context();auto const& v=SurvivalRuntime::get().view();
        if(c.matchId==v.id && c.roundKey==v.roundKey && c.mapGeneration==v.mapGeneration && c.levelId==static_cast<std::uint64_t>(v.map.levelId))owner->m_action(tag);
    });
    if(!m_panel)return false;addChild(m_panel,10);resetVisuals();return true;
}
SurvivalMapSelectionLayer* SurvivalMapSelectionLayer::create(std::function<void(int)> action){auto* p=new SurvivalMapSelectionLayer;if(p->init(std::move(action))){p->autorelease();return p;}delete p;return nullptr;}
void SurvivalMapSelectionLayer::prepareServerMap(SurvivalMatchView const& view){
    if(m_active)hideSelection();setVisible(false);
    if(view.roundKey.empty())return;
    SurvivalSelectionContext context{view.id,view.roundKey,view.mapGeneration,static_cast<std::uint64_t>(std::max(0,view.map.levelId)),m_sceneGeneration};
    if(context.levelId)reveals.try_emplace(context.key(),RevealRecord{monotonicMs(),0,false});
    // The roster's existing presentation time is useful prefetch time even
    // before the server has selected the real level. No reveal starts here.
    if(SurvivalThumbnailProvider::supported()){SurvivalThumbnailProvider::get().activate(context);m_providerContext=context;}
}
void SurvivalMapSelectionLayer::resetVisuals(){
    if(m_visuals){for(auto* node:{m_visuals->reels,static_cast<CCNode*>(m_visuals->carousel),static_cast<CCNode*>(m_visuals->decoration),static_cast<CCNode*>(m_visuals->selected.root)})if(node)node->removeFromParentAndCleanup(true);}
    m_visuals=std::make_unique<Visuals>();m_sounds.clear();m_lastSound=0;m_finalId->setVisible(false);m_pendingLabel->setVisible(false);
}
void SurvivalMapSelectionLayer::numberFrame(SurvivalRevealFrame const& frame,SurvivalSelectionLayout const& r){
    auto& v=*m_visuals;auto digits=m_timeline.digits();auto const n=digits.empty()?9:digits.size();auto const size=getContentSize();
    if(v.columns.size()!=n){
        if(v.reels)v.reels->removeFromParentAndCleanup(true);v.columns.clear();v.reels=CCNode::create();addChild(v.reels,8);
        auto const layout=survivalNumberLayout(size.width,size.height,n);v.reelHeight=layout.lineStep;v.reelY=layout.ribbon.cy();
        v.reels->addChild(surface({layout.ribbon.w,layout.ribbon.h},{layout.ribbon.cx(),layout.ribbon.cy()},Blue,Surface,255,true));
        for(std::size_t i=0;i<n;++i){auto const rect=layout.columns[i];auto const width=rect.w;Visuals::Reel reel;auto* viewport=clip({width,rect.h});viewport->setPosition({rect.x,rect.y});v.reels->addChild(viewport,2);reel.root=viewport;
            viewport->addChild(CCLayerColor::create({10,22,43,255},width,76*r.scale));
            for(std::size_t j=0;j<3;++j){reel.labels[j]=menuLabel("0",.9f,{width/2,38*r.scale},Text,FontWeight::Black,countdownFont());fitSingle(reel.labels[j],width-4*r.scale,50*r.scale,.9f);reel.scales[j]=reel.labels[j]->getScale();viewport->addChild(reel.labels[j],3);}
            reel.impact=CCLayerColor::create({Gold.r,Gold.g,Gold.b,255},width,2*r.scale);reel.impact->setPosition({0,0});viewport->addChild(reel.impact,5);v.columns.push_back(reel);
        }
    }
    bool const final=frame.elapsed>=2.70 && !digits.empty();v.reels->setVisible(!final);m_finalId->setVisible(final);
    if(!final)for(std::size_t i=0;i<n;++i){auto value=survivalReel(m_timeline,frame,i);
        if(digits.empty()){auto const phase=frame.elapsed*(34.+i*3.1)+i*1.73;value.digit=static_cast<int>(phase)%10;value.offset=phase-std::floor(phase);}
        auto& col=v.columns[i];auto const width=col.root->getContentSize().width;
        for(std::size_t j=0;j<3;++j){auto const neighbor=static_cast<int>(j)-1;auto const digit=(value.digit+neighbor+10)%10;auto* label=col.labels[j];
            auto str=std::to_string(digit);if(str!=label->getString())label->setString(str.c_str());
            label->setColor(value.locked?Gold:Text);label->setOpacity(neighbor?85:255);
            label->setScale(col.scales[j]*static_cast<float>(1+.07*value.impact));
            label->setPosition({width/2,38*r.scale+static_cast<float>((neighbor-value.offset)*v.reelHeight+value.impact*2*r.scale)});
            label->setVisible(!value.locked || neighbor==0);
        }
        col.impact->setVisible(value.locked);col.impact->setOpacity(static_cast<GLubyte>(155+100*value.impact));
    }
    if(final){if(digits!=m_finalId->getString())m_finalId->setString(digits.c_str());
        auto const progress=frame.landing;auto const y=size.height*.5+(r.numberFinal.cy()-size.height*.5)*progress;
        m_finalId->setPosition({size.width/2,static_cast<float>(y)});fitSingle(m_finalId,r.numberFinal.w,static_cast<float>((58+(r.numberFinal.h-58)*progress)),.9f);
        if(frame.elapsed<2.87)m_finalId->setScale(m_finalId->getScale()*static_cast<float>(1+.06*std::sin((frame.elapsed-2.70)/.17*3.141592653589793)));
    }
}
void SurvivalMapSelectionLayer::thumbnailFrame(SurvivalRevealFrame const& frame,SurvivalSelectionLayout const& r){
    auto& v=*m_visuals;auto const size=getContentSize();
    if(!v.cardsBuilt){v.cardWidth=std::min(252.f*r.scale,size.width*.40f);v.carousel=clip({size.width-28*r.scale,r.carousel.h+12*r.scale});v.carousel->setPosition({14*r.scale,r.carousel.cy()-(r.carousel.h+12*r.scale)/2});addChild(v.carousel,8);
        for(auto& card:v.cards)card.make(v.carousel,v.cardWidth);v.selected.make(this,v.cardWidth);v.selected.root->setZOrder(12);v.cardsBuilt=true;
    }
    auto const provider=SurvivalThumbnailProvider::get().previews();
    // A carousel must use a fixed ordered reel throughout the four-second
    // reveal. Changing modulo with each newly downloaded image made cards jump.
    if(!v.previewsFrozen && !provider.empty()){
        for(auto const& p:provider)
            if(std::none_of(v.previews.begin(),v.previews.end(),[&](auto const& cached){return cached.first==p.first;}))
                v.previews.push_back(p);
        // Wait for a useful reel instead of freezing at only the selected
        // thumbnail and repeating that same image five times.
        v.previewsFrozen=v.previews.size()>=4 || frame.elapsed>=2.15;
    }
    if(v.previews.size()>8)v.previews.erase(v.previews.begin(),v.previews.end()-8);
    if(auto preferred=SurvivalThumbnailProvider::get().selected();preferred && preferred.data()!=v.finalTexture.data())v.finalTexture=preferred;
    // Maintain position and velocity at the deceleration boundary. The old
    // cubic easing jumped in speed and then nearly froze half-way through.
    constexpr double glideStart=1.95,glideDuration=.80,cyclesPerSecond=1./.14;
    double phase=frame.elapsed*cyclesPerSecond;
    if(frame.elapsed>=glideStart){
        auto const p=std::clamp((frame.elapsed-glideStart)/glideDuration,0.,1.);
        phase=glideStart*cyclesPerSecond+glideDuration*cyclesPerSecond*(p-.5*p*p);
    }
    auto const base=static_cast<std::size_t>(std::floor(phase));auto const fraction=phase-std::floor(phase);
    auto const central=frame.elapsed>=2.15 && m_timeline.context().levelId>0;
    v.carousel->setVisible(frame.elapsed<3.10);
    for(std::size_t i=0;i<v.cards.size();++i){auto& card=v.cards[i];auto const relative=static_cast<float>(i)-2-static_cast<float>(fraction);
        card.set(v.previews.empty()?nullptr:v.previews[(base+i)%v.previews.size()].second.data());
        auto const emphasis=std::max(0.f,1-std::abs(relative)*.19f);card.root->setPosition({v.carousel->getContentSize().width/2+relative*v.cardWidth*.84f,v.carousel->getContentSize().height/2});
        card.root->setScale(.63f+.37f*emphasis);card.root->setZOrder(static_cast<int>(10-std::abs(relative)*2));card.trim->setVisible(false);card.shade->setOpacity(static_cast<GLubyte>(90+75*(1-emphasis)));
        setModeLobbyOpacity(card.root,static_cast<GLubyte>((1-std::clamp((frame.elapsed-2.75)/.30,0.,1.))*255));
        if(central && std::abs(relative)<.9f)card.root->setVisible(false);else card.root->setVisible(true);
    }
    auto& selected=v.selected;selected.root->setVisible(central);if(central){selected.set(v.finalTexture.data());selected.shade->setOpacity(0);selected.trim->setVisible(true);
        // Thumbnail landing starts at 2.75s; numeric landing starts at 2.70s.
        auto const p=survivalEase((frame.elapsed-2.75)/.45);auto const start=CCPoint{size.width/2,r.carousel.cy()};
        auto const arrival=1-survivalEase((frame.elapsed-2.15)/.60);
        selected.root->setPosition({static_cast<float>(start.x+(r.thumbnail.cx()-start.x)*p+v.cardWidth*.84*arrival),static_cast<float>(start.y+(r.thumbnail.cy()-start.y)*p)});
        auto scale=static_cast<float>(1+(r.thumbnail.w/v.cardWidth-1)*p);
        if(frame.elapsed>=2.75 && frame.elapsed<2.93)scale*=static_cast<float>(1+.06*std::sin((frame.elapsed-2.75)/.18*3.141592653589793));selected.root->setScale(scale);
        setModeLobbyOpacity(selected.root,255);
    }
}
void SurvivalMapSelectionLayer::playLocks(SurvivalRevealFrame const& frame){
    auto& record=reveals[m_timeline.context().key()];auto locks=m_timeline.takeLocks(frame,!m_thumbnail);
    for(auto lock:locks)if(lock>=record.consumed)m_sounds.push_back(lock);
    record.consumed=std::max(record.consumed,frame.locked);
    if(m_thumbnail && frame.elapsed>=2.75 && !record.centerPlayed){record.centerPlayed=true;if(!frame.skipped && !m_needsResume)m_sounds.push_back(0);}
    auto const now=monotonicMs();if(frame.skipped || m_needsResume){m_sounds.clear();return;}
    if(!m_sounds.empty() && now-m_lastSound>=65){[[maybe_unused]] auto const column=m_sounds.front();m_sounds.pop_front();m_lastSound=now;
        auto const gain=RankedVolumeControl::get().volume();if(gain>0)if(auto* engine=FMODAudioEngine::sharedEngine())engine->playEffect("clickChange.ogg",1.f,0.f,.22f*gain);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        log::info("[SurvivalReveal] tick id={} thumbnail={} column={} elapsedMs={} gain={}",m_timeline.context().levelId,m_thumbnail,column,static_cast<int>(frame.elapsed*1000),gain);
#endif
    }
}
void SurvivalMapSelectionLayer::present(SurvivalMatchView const& view,RoundPreparationView const& prep,SurvivalNativeOptions options,
    bool supportsLdm,bool actionsEnabled,bool canEnter,std::optional<std::int64_t> countdown,std::optional<std::int64_t> remaining){
    auto const now=monotonicMs();auto const size=getContentSize();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    if(m_lastFrameMillis && now-m_lastFrameMillis>50)log::info("[SurvivalReveal] frameGapMs={} id={}",now-m_lastFrameMillis,view.map.levelId);
#endif
    m_lastFrameMillis=now;
    auto const appEpoch=SurvivalThumbnailProvider::applicationEpoch();
    if(appEpoch!=m_appEpoch){m_appEpoch=appEpoch;resetVisuals();m_needsResume=true;}
    SurvivalSelectionContext context{view.id,view.roundKey,view.mapGeneration,static_cast<std::uint64_t>(std::max(0,view.map.levelId)),m_sceneGeneration};
    bool const changed=context!=m_timeline.context();
    if(changed){resetVisuals();
        auto [it,added]=reveals.try_emplace(context.key(),RevealRecord{now,0,false});
        if(reveals.size()>16)for(auto old=reveals.begin();old!=reveals.end();++old)if(old->first!=context.key()){reveals.erase(old);break;}
        m_timeline.observe(context,it->second.began);if(it->second.complete || Mod::get()->getSavedValue<std::string>("survival-map-reveal-complete")==context.key())m_timeline.skip();
    }
    m_active=true;setVisible(true);
    auto const supported=SurvivalThumbnailProvider::supported();bool previewsReady=false;
    if(supported){auto& provider=SurvivalThumbnailProvider::get();provider.activate(context);m_providerContext=context;previewsReady=!provider.previews().empty();}
    auto& record=reveals[context.key()];
    m_thumbnail=record.mode.choose(supported,previewsReady);
    if(changed)log::info("[SurvivalReveal] mode level={} supported={} previewsReady={} thumbnail={}",context.levelId,supported,previewsReady,m_thumbnail);
    m_fallback=!m_thumbnail && Loader::get()->isModLoaded("cdc.level_thumbnails");
    m_timeline.followServer(view.state,countdown,now);
    auto const frame=m_timeline.frame(now);if(frame.stage==SurvivalRevealStage::Cancelled){hideSelection();return;}
    // An asynchronously arriving winner image must not switch the reel to a
    // numeric mode in the middle of its landing animation.
    if(m_thumbnail && !supported){
        record.mode.fallback();m_thumbnail=false;m_fallback=true;resetVisuals();
    }
    auto const r=survivalSelectionLayout(size.width,size.height,m_thumbnail,supportsLdm);
    m_pendingLabel->setVisible(!context.levelId && !m_thumbnail);
    if(m_thumbnail)thumbnailFrame(frame,r);else if(context.levelId)numberFrame(frame,r);
    m_panel->present(size,m_thumbnail,view,prep,options,supportsLdm,actionsEnabled,canEnter,frame.details);
    auto const finalHeader=frame.elapsed>=3.2 || frame.skipped;
    auto clock=(finalHeader || view.state=="COUNTDOWN" || view.state=="PLAYING")?survivalBoardClock(view.state,countdown,remaining):std::string{};
    if(auto wait=SurvivalRuntime::get().entryWaitMs();view.state=="ENTERING" && wait && *wait>0)
        clock=fmt::format(fmt::runtime(i18n::text("survival-entry-wait","ALL READY · ENTERING IN {}s")),(*wait+999)/1000);
    auto const header=finalHeader?i18n::text("survival-selected-map","SELECTED MAP"):std::string("NEXT UP!");if(header!=m_header->getString())m_header->setString(header.c_str());
    m_header->setPosition({r.header.cx(),r.header.cy()});fitFullMenuLabel(m_header,r.header.w,r.header.h,finalHeader?.48f:.83f);
    m_header->setSkewX(finalHeader?0:-5);
    if(!clock.empty()){
        auto const phase=view.phase=="TIEBREAK"?std::string("DEATH MATCH"):view.phase=="FINAL"?std::string("FINAL"):fmt::format("ROUND {}",view.roundNumber);
        auto const clockText=phase+"\n"+i18n::text(clock,clock);if(clockText!=m_clock->getString())m_clock->setString(clockText.c_str());m_clock->setAnchorPoint({1,.5f});m_clock->setPosition({r.header.right(),r.header.cy()});fitFullMenuLabel(m_clock,142*r.scale,34*r.scale,.27f);
        m_header->setPosition({r.header.cx(),r.header.cy()});fitFullMenuLabel(m_header,std::max(80.f,r.header.w-304*r.scale),r.header.h,finalHeader?.48f:.83f);
    }m_clock->setVisible(!clock.empty());
    auto const subtitle=finalHeader?std::string("SURVIVAL"):context.levelId==0 && !view.mapError.empty()?i18n::text("survival-map-retry","MAP SEARCH FAILED · RETRYING..."):std::string("MAP SELECTION");if(subtitle!=m_subtitle->getString())m_subtitle->setString(subtitle.c_str());m_subtitle->setPosition({r.subtitle.cx(),r.subtitle.cy()});fitFullMenuLabel(m_subtitle,r.subtitle.w,r.subtitle.h,.20f);
    auto hint=m_fallback?i18n::text("survival-thumbnail-unavailable","Thumbnail unavailable; showing the server level ID."):
        i18n::text("survival-thumbnail-hint","(i) Install Level Thumbnails for map previews and a richer selection reveal.");
    if(hint!=m_hint->getString())m_hint->setString(hint.c_str());m_hint->setPosition({r.hint.cx(),r.hint.cy()});fitFullMenuLabel(m_hint,r.hint.w,r.hint.h,.17f);
    m_hint->setVisible(!m_thumbnail && frame.details>0);m_hint->setOpacity(static_cast<GLubyte>(frame.details*255));
    playLocks(frame);m_needsResume=false;
    if(frame.settled){if(!record.complete){record.complete=true;Mod::get()->setSavedValue("survival-map-reveal-complete",context.key());
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        log::info("[SurvivalReveal] id={} thumbnail={} elapsedMs={} skipped={}",context.levelId,m_thumbnail,now-record.began,frame.skipped);
#endif
    }}
}
bool SurvivalMapSelectionLayer::settled()const{return m_active && m_timeline.frame(monotonicMs()).settled;}
void SurvivalMapSelectionLayer::hideSelection(){
    if(m_providerContext){SurvivalThumbnailProvider::get().release(*m_providerContext);m_providerContext.reset();}
    if(!m_active)return;m_active=false;setVisible(false);m_panel->suspend();m_sounds.clear();resetVisuals();m_needsResume=true;m_lastFrameMillis=0;
}
void SurvivalMapSelectionLayer::onEnter(){CCLayerColor::onEnter();m_needsResume=true;}
void SurvivalMapSelectionLayer::onExit(){hideSelection();CCLayerColor::onExit();}
}
