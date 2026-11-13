#include "RankedTierGuidePopup.hpp"
#include "RankedVisuals.hpp"
#include "RankBadgeAssets.hpp"
#include "RankedTranslations.hpp"
#include "RankedInput.hpp"
#include "RankedPopupLayout.hpp"
#include "../RankedRuntime.hpp"
#include "../domain/TierGuidePresentation.hpp"
#include <algorithm>
#include <chrono>
#include <utility>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
// Q3 board: 960 x 560 board px.
constexpr float Width=480, Height=280, Pad=12, ColumnGap=5;
CCLabelBMFont* text(CCNode* node,std::string const& value,CCPoint at,float width,float scale,ccColor3B color,FontWeight weight,float anchor=.5f) {
    auto* label=menuLabel(value,scale,at,color,weight);label->setAnchorPoint({anchor,.5f});
    fitSingleLineMenuLabel(label,width,scale*48,scale);node->addChild(label,8);return label;
}
// A 1.5 px outline (board inset 0 0 0 2px #C8FF2E) around a column.
void outline(CCNode* node,CCSize size,ccColor3B color) {
    for(int i=0;i<4;++i){
        auto* bar=CCLayerColor::create({color.r,color.g,color.b,255});
        bar->setContentSize(i<2?CCSize{size.width,1.5f}:CCSize{1.5f,size.height});
        bar->setPosition(i==0?CCPoint{0,0}:i==1?CCPoint{0,size.height-1.5f}:i==2?CCPoint{0,0}:CCPoint{size.width-1.5f,0});
        node->addChild(bar,9);
    }
}
CCNode* segmentFace(std::string const& value,float width,bool on) {
    auto* face=surface({width,18},{},on?Accent:Steel,on?Accent:SurfaceRaised,255,true);
    auto* label=menuLabel(value,.21f,{width/2,9},on?AccentInk:TextMuted,FontWeight::ExtraBold);
    fitSingleLineMenuLabel(label,width-6,13,.21f);face->addChild(label,5);return face;
}
CCNode* ghostFace(std::string const& value,float width) {
    auto* face=surface({width,18},{},Steel,SurfaceRaised,250,true);
    auto* label=menuLabel(value,.21f,{width/2,9},Text,FontWeight::ExtraBold);
    fitSingleLineMenuLabel(label,width-6,13,.21f);face->addChild(label,5);return face;
}
std::string lpRange(TierGuideColumn const& column) {
    if(!column.present)return "—";
    return column.maximumExclusive?fmt::format("{} ~ {} LP",column.minimum,*column.maximumExclusive-1):fmt::format("{}+ LP",column.minimum);
}
} // namespace

RankedTierGuidePopup* RankedTierGuidePopup::create() {
    auto* popup=new RankedTierGuidePopup();
    if(popup && popup->init()){popup->autorelease();return popup;}
    delete popup;return nullptr;
}

bool RankedTierGuidePopup::init() {
    auto const layout=rankedPopupLayout({Width,Height},CCDirector::sharedDirector()->getWinSize());
    if(!Popup::init(layout.size.width,layout.size.height))return false;
    setID("ranked-tier-guide-popup"_spr);m_noElasticity=true;
    m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);m_closeBtn->setEnabled(false);
    auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
    m_panel=surface({Width,Height},{Width/2,Height/2},Accent,Surface,255,true);content->addChild(m_panel,1);
    // Header: the leaning title, the Solo / Survival segment, REFRESH and X.
    auto* title=text(m_panel,i18n::text("tier-guide","TIER GUIDE"),{Pad,262},120,28.f/64.f,Text,FontWeight::Black,0);leanLabel(title);
    auto const titleRight=Pad+title->getContentSize().width*title->getScaleX()+12;
    auto* menu=m_buttonMenu;menu->setZOrder(20);
    auto button=[&](CCNode* face,CCPoint at,SEL_MenuHandler handler,int tag=0){
        auto* item=CCMenuItemSpriteExtra::create(face,this,handler);item->setPosition(at);item->setTag(tag);menu->addChild(item);return item;
    };
    m_modes[0]=button(segmentFace(i18n::text("solo-ranked","SOLO RANKED"),64,true),{titleRight+32,262},menu_selector(RankedTierGuidePopup::onMode),0);
    m_modes[1]=button(segmentFace(i18n::text("survival","SURVIVAL"),64,false),{titleRight+32+66,262},menu_selector(RankedTierGuidePopup::onMode),1);
    m_refresh=button(ghostFace(i18n::text("leaderboard-refresh","REFRESH"),58),{Width-Pad-20-6-29,262},menu_selector(RankedTierGuidePopup::onRefresh));
    auto* cross=surface({20,20},{},Steel,SurfaceRaised,250,true);
    auto* glyph=menuLabel("X",.26f,{10,10},Text,FontWeight::Black);fitSingleLineMenuLabel(glyph,14,14,.26f);cross->addChild(glyph,5);
    button(cross,{Width-Pad-10,262},menu_selector(RankedTierGuidePopup::onClose))->setID("ranked-tier-guide-close"_spr);
    m_caption=text(m_panel,corum::ranked::i18n::literal("inline.loading"),{Width/2,16},Width-Pad*2,.19f,TextMuted,FontWeight::SemiBold);
    render();fetch();return true;
}

void RankedTierGuidePopup::fetch() {
    if(m_closed || m_busy)return;
    m_busy=true;m_refresh->setEnabled(false);
    auto const generation=++m_generation;
    web::WebRequest request;[](auto& q){if constexpr(requires{q.getHttpVersion();})q.version(decltype(q.getHttpVersion())::VERSION_2TLS);}(request); /* HTTP/2: see PREFER_HTTP2 note in RankedRuntime.cpp */request.timeout(std::chrono::seconds(20));
    m_request.spawn(request.get(RankedRuntime::get().tierGuideUrl()),[weak=WeakRef(this),generation](web::WebResponse response) {
        queueInMainThread([weak,generation,response=std::move(response)] {
            auto self=weak.lock();if(!self || self->m_closed || generation!=self->m_generation)return;
            ++self->m_generation;self->m_busy=false;self->m_refresh->setEnabled(true);
            auto const data=response.json().unwrapOr(matjson::Value());
            auto const rows=data["tiers"].asArray();
            self->m_failed=!response.ok() || rows.isErr() || rows.unwrap().size()>16;
            if(!self->m_failed)self->m_solo=data;
            self->render();
        });
    });
}

// Q3: five tier columns (trophy, name, divisions, LP range, note or the
// current-tier chip), then the promotion and demotion panels.
void RankedTierGuidePopup::render() {
    if(m_closed)return;
    if(m_contents)m_contents->removeFromParentAndCleanup(true);
    m_contents=CCNode::create();m_panel->addChild(m_contents,6);
    for(std::size_t i=0;i<m_modes.size();++i)if(m_modes[i])
        m_modes[i]->setNormalImage(segmentFace(i?i18n::text("survival","SURVIVAL"):i18n::text("solo-ranked","SOLO RANKED"),64,(i==1)==m_survival));
    auto const& view=RankedRuntime::get().view();
    std::vector<TierGuideBand> bands;
    if(m_survival){
        auto const& cutoffs=view.survival.tierBands;
        for(std::size_t i=0;i<cutoffs.size();++i)
            bands.push_back({cutoffs[i].tier,cutoffs[i].minimum,i+1<cutoffs.size()?std::optional<int>(cutoffs[i+1].minimum):std::nullopt});
    }else if(!m_solo.isNull()){
        std::vector<matjson::Value> const tiers=std::as_const(m_solo)["tiers"].asArray().unwrapOr(std::vector<matjson::Value>{});
        for(auto const& band:tiers){
            auto const max=band["maxExclusive"].asInt();
            bands.push_back({band["tier"].asString().unwrapOr(""),static_cast<int>(band["minInclusive"].asInt().unwrapOr(0)),
                max.isOk()?std::optional<int>(static_cast<int>(max.unwrap())):std::nullopt,
                static_cast<int>(band["promotionGames"].asInt().unwrapOr(0)),static_cast<int>(band["promotionWinsRequired"].asInt().unwrapOr(0))});
        }
    }
    auto const current=m_survival?view.survival.profile.displayedTier:view.profileTier;
    auto const columns=tierGuideColumns(bands);
    auto const columnWidth=(Width-Pad*2-ColumnGap*4)/5,top=246.f,bottom=100.f,height=top-bottom;
    for(std::size_t i=0;i<columns.size();++i){
        auto const& column=columns[i];auto const color=tierColor(column.base);
        auto const left=Pad+static_cast<float>(i)*(columnWidth+ColumnGap),cx=left+columnWidth/2;
        // .panel #20232E with the tier-coloured 3 px top rule.
        auto* face=CCLayerColor::create({SurfaceRaised.r,SurfaceRaised.g,SurfaceRaised.b,255},columnWidth,height);
        face->setPosition({left,bottom});m_contents->addChild(face,1);
        auto* rule=CCLayerColor::create({color.r,color.g,color.b,255},columnWidth,1.5f);rule->setPosition({0,height-1.5f});face->addChild(rule,2);
        auto const mine=!current.empty() && rankTierBase(current)==column.base;
        if(mine)outline(face,{columnWidth,height},Accent);
        auto* badge=createAnimatedRankBadge(column.base,{44,44},RankBadgeDisplay::Leaderboard);badge->setPosition({cx,top-32});m_contents->addChild(badge,5);
        text(m_contents,column.base,{cx,top-64},columnWidth-8,.32f,color,FontWeight::Black);
        auto const divisions=column.base=="GOLD"?std::string("MASTER · CHAMPION"):column.divisions>1?std::string("III · II · I"):std::string("—");
        text(m_contents,m_survival&&column.base=="GOLD"?std::string("—"):divisions,{cx,top-78},columnWidth-8,.18f,TextMuted,FontWeight::SemiBold);
        text(m_contents,lpRange(column),{cx,top-93},columnWidth-8,.21f,Text,FontWeight::ExtraBold);
        if(mine){
            auto* chip=surface({columnWidth-14,16},{cx,bottom+14},Accent,Accent,255,true);m_contents->addChild(chip,6);
            text(chip,i18n::insertValue(i18n::text("tier-guide-current","CURRENT {}"),rankTierDisplay(current)),{(columnWidth-14)/2,8},columnWidth-20,.19f,AccentInk,FontWeight::ExtraBold);
        }else{
            std::string note;
            if(i==0)note=i18n::text("tier-guide-start","Starting tier");
            else if(column.promotionGames>0)note=fmt::format(fmt::runtime(corum::ranked::i18n::literal("inline.series-games-wins")),column.promotionGames,column.promotionWins);
            else if(column.base=="GOLD"&&!m_survival)note=i18n::text("tier-guide-ladder","Season ladder position");
            if(!note.empty())text(m_contents,note,{cx,bottom+14},columnWidth-8,.18f,TextMuted,FontWeight::SemiBold);
        }
    }
    // Promotion and demotion panels.
    auto const panelWidth=(Width-Pad*2-6)/2;
    for(int i=0;i<2;++i){
        auto const left=Pad+static_cast<float>(i)*(panelWidth+6);
        auto* face=CCLayerColor::create({SurfaceRaised.r,SurfaceRaised.g,SurfaceRaised.b,255},panelWidth,58);face->setPosition({left,32});m_contents->addChild(face,1);
        text(m_contents,i?i18n::text("tier-guide-demotion","DEMOTION WARNING"):i18n::text("tier-guide-promotion","PROMOTION SERIES"),{left+10,79},panelWidth-20,.22f,Accent,FontWeight::ExtraBold,0);
        auto const body=i?i18n::text("tier-guide-demotion-detail","Shown near a tier's lower bound; it clears once you recover the required LP."):
            m_survival?i18n::text("tier-guide-survival-promotion","You promote as soon as your LP reaches the next tier's lower bound."):
            i18n::text("tier-guide-promotion-detail","Reaching a tier boundary starts a promotion series; win the required games to promote.");
        auto* label=menuLabel(body,.19f,{left+panelWidth/2,52},TextMuted,FontWeight::SemiBold);
        fitWrappedMenuLabel(label,body,panelWidth-20,30,.19f);m_contents->addChild(label,8);
    }
    // Status line: loading, failure, or the Solo GOLD population.
    std::string status;ccColor3B statusColor=TextMuted;
    if(m_survival)status=bands.empty()?corum::ranked::i18n::literal("inline.could-not-load-press-refresh"):std::string();
    else if(m_failed){status=corum::ranked::i18n::literal("inline.could-not-load-press-refresh");statusColor=Red;}
    else if(m_solo.isNull())status=corum::ranked::i18n::literal("inline.loading");
    else status=fmt::format(fmt::runtime(corum::ranked::i18n::literal("inline.solo-ranked-gold-players")),m_solo["gold"]["total"].asInt().unwrapOr(0))+
        " · "+corum::ranked::i18n::literal("inline.ties-use-leaderboard-order-bots-included");
    m_caption->setString(status.c_str());m_caption->setColor(statusColor);fitSingleLineMenuLabel(m_caption,Width-Pad*2,10,.19f);
}

void RankedTierGuidePopup::onMode(CCObject* sender) {
    if(m_closed||!sender)return;
    auto const survival=static_cast<CCNode*>(sender)->getTag()==1;
    if(survival==m_survival)return;
    m_survival=survival;render();
}
void RankedTierGuidePopup::onRefresh(CCObject*) { fetch(); }
void RankedTierGuidePopup::onClose(CCObject* sender) {
    if(m_closed)return;m_closed=true;++m_generation;m_request.cancel();Popup::onClose(sender);
}
void RankedTierGuidePopup::onExit() { m_closed=true;++m_generation;m_request.cancel();Popup::onExit(); }
} // namespace corum::ranked::ui
