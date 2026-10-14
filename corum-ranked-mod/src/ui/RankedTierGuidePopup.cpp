#include "RankedTierGuidePopup.hpp"
#include "RankedVisuals.hpp"
#include "RankBadgeAssets.hpp"
#include "RankedTranslations.hpp"
#include "RankedInput.hpp"
#include "../RankedRuntime.hpp"
#include <algorithm>
#include <chrono>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
constexpr float RowHeight=47;
std::string lpCut(matjson::Value const& value) {
    auto const number=value.asInt();return number.isOk()?fmt::format("{} LP",number.unwrap()):"— LP";
}
void caption(CCNode* node,std::string const& text,CCPoint point,float width,float scale,ccColor3B color=Text) {
    auto* label=menuLabel(text,scale,point,color,FontWeight::SemiBold);
    label->setAnchorPoint({0,.5f});fitMenuLabelWidth(label,width,scale,scale*.78f);node->addChild(label,8);
}
}

RankedTierGuidePopup* RankedTierGuidePopup::create() {
    auto* popup=new RankedTierGuidePopup();
    if(popup && popup->init()){popup->autorelease();return popup;}
    delete popup;return nullptr;
}

bool RankedTierGuidePopup::init() {
    auto const win=CCDirector::sharedDirector()->getWinSize();
    CCSize const size{std::min(470.f,win.width-20),std::min(340.f,win.height-20)};
    if(!Popup::init(size.width,size.height))return false;
    setID("ranked-tier-guide-popup"_spr);
    m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);m_closeBtn->setEnabled(false);
    auto* panel=surface(size,{size.width/2,size.height/2},Gold,Surface,255,true);m_mainLayer->addChild(panel,1);
    panel->addChild(menuLabel(i18n::isKorean()?"티어 / 커트라인":"TIERS / CUTOFFS",.49f,{size.width/2,size.height-22},Gold,FontWeight::Black),5);
    m_caption=menuLabel(i18n::isKorean()?"불러오는 중...":"Loading...",.28f,{size.width/2,size.height-47},TextMuted);
    panel->addChild(m_caption,5);
    caption(panel,i18n::isKorean()?"티어":"TIER",{44,size.height-67},105,.27f);
    caption(panel,i18n::isKorean()?"LP / 기준":"LP / RULE",{size.width*.36f,size.height-67},135,.27f);
    caption(panel,i18n::isKorean()?"맵 풀":"MAP POOL",{size.width*.74f,size.height-67},100,.27f);
    m_scroll=ScrollLayer::create({size.width-24,size.height-143});m_scroll->setPosition({12,61});
    panel->addChild(m_scroll,4);
    auto* note=menuLabel(i18n::isKorean()?"동점은 리더보드 순서로 결정 · 봇 포함":"Ties use leaderboard order · Bots included",.22f,{size.width/2,49},TextMuted);
    fitMenuLabelWidth(note,size.width-32,.22f,.18f);panel->addChild(note,5);
    auto* menu=CCMenu::create();menu->setPosition(CCPointZero);panel->addChild(menu,20);
    auto button=[&](std::string const& title,float x,SEL_MenuHandler callback) {
        auto* face=surface({104,26},{},Blue,SurfaceRaised,250,true);
        face->addChild(menuLabel(title,.34f,{52,13},Text,FontWeight::SemiBold),5);
        auto* item=CCMenuItemSpriteExtra::create(face,this,callback);item->setPosition({x,22});menu->addChild(item);return item;
    };
    button(i18n::text("close","CLOSE"),size.width/2-62,menu_selector(RankedTierGuidePopup::onClose));
    m_refresh=button(i18n::text("leaderboard-refresh","REFRESH"),size.width/2+62,menu_selector(RankedTierGuidePopup::onRefresh));
    fetch();return true;
}

void RankedTierGuidePopup::fetch() {
    if(m_closed || m_busy)return;
    m_busy=true;m_refresh->setEnabled(false);
    auto const generation=++m_generation;
    web::WebRequest request;request.timeout(std::chrono::seconds(20));
    m_request.spawn(request.get(RankedRuntime::get().tierGuideUrl()),[weak=WeakRef(this),generation](web::WebResponse response) {
        queueInMainThread([weak,generation,response=std::move(response)] {
            auto self=weak.lock();if(!self || self->m_closed || generation!=self->m_generation)return;
            ++self->m_generation;self->m_busy=false;self->m_refresh->setEnabled(true);
            auto const data=response.json().unwrapOr(matjson::Value());
            auto const rows=data["tiers"].asArray();
            if(!response.ok() || rows.isErr() || rows.unwrap().size()>16) {
                self->m_caption->setString(i18n::isKorean()?"불러오지 못했습니다. 새로고침하세요.":"Could not load. Press Refresh.");
                self->m_caption->setColor(Red);return;
            }
            self->showRows(data);
        });
    });
}

void RankedTierGuidePopup::showRows(matjson::Value const& data) {
    auto const tiers=data["tiers"].asArray().unwrap();auto const gold=data["gold"];
    auto const total=static_cast<int>(gold["total"].asInt().unwrapOr(0));
    m_caption->setString(fmt::format(fmt::runtime(i18n::isKorean()?"SOLO RANKED · GOLD 이상 {}명":"SOLO RANKED · {} Gold players"),total).c_str());
    m_caption->setColor(TextMuted);fitMenuLabelWidth(m_caption,m_scroll->getContentSize().width,.28f,.22f);
    auto* content=m_scroll->m_contentLayer;content->removeAllChildrenWithCleanup(true);
    auto const width=m_scroll->getContentSize().width;
    auto const height=std::max(m_scroll->getContentSize().height,RowHeight*static_cast<float>(tiers.size()+2));
    content->setContentSize({width,height});
    auto row=[&](std::string const& tier,std::string const& rule,std::string const& pool,std::string const& detail,std::size_t index) {
        auto* face=surface({width,RowHeight-3},{},tierColor(tier),SurfaceRaised,250,true);
        face->setAnchorPoint(CCPointZero);face->setPosition({0,height-RowHeight*static_cast<float>(index+1)});content->addChild(face);
        auto* badge=createAnimatedRankBadge(tier,{26,26},RankBadgeDisplay::Leaderboard);badge->setPosition({22,25});face->addChild(badge,5);
        caption(face,rankTierDisplay(tier),{42,29},width*.27f-42,.32f,tierColor(tier));
        caption(face,detail,{42,12},width*.31f-42,.19f,TextMuted);
        caption(face,rule,{width*.35f,26},width*.37f,.28f);
        caption(face,pool,{width*.74f,26},width*.24f,.24f,TextMuted);
    };
    std::size_t index=0;std::string goldPool;
    for(auto const& band:tiers) {
        auto const tier=band["tier"].asString().unwrapOr("");
        auto const min=band["minInclusive"].asInt().unwrapOr(0);auto const max=band["maxExclusive"].asInt();
        auto const rule=max.isOk()?fmt::format("{} ~ {} LP",min,max.unwrap()-1):fmt::format("{}+ LP",min);
        auto const games=band["promotionGames"].asInt().unwrapOr(0),wins=band["promotionWinsRequired"].asInt().unwrapOr(0);
        auto const detail=games>0?fmt::format(fmt::runtime(i18n::isKorean()?"승격전 {}판 {}승":"Series {} games / {} wins"),games,wins):
            (tier=="GOLD"?(i18n::isKorean()?"인원 / 순위 기준":"Population / position"):"");
        auto const pool=fmt::format("{} / DM {}",band["mainPool"].asInt().unwrapOr(0),band["deathmatchPool"].asInt().unwrapOr(0));
        if(tier=="GOLD")goldPool=pool;
        row(tier,rule,pool,detail,index++);
    }
    auto const master=gold["masterSlots"].asInt().unwrapOr(0);
    row("MASTER",master>0?lpCut(gold["masterCutoffLp"]):(i18n::isKorean()?"없음":"NONE"),goldPool,
        fmt::format(fmt::runtime(i18n::isKorean()?"상위 절반 · {}명":"Upper half · {} players"),master),index++);
    row("CHAMPION",lpCut(gold["championCutoffLp"]),goldPool,i18n::isKorean()?"상위 3명":"TOP 3",index++);
    m_scroll->moveToTop();
}

void RankedTierGuidePopup::onRefresh(CCObject*) { fetch(); }
void RankedTierGuidePopup::onClose(CCObject* sender) {
    if(m_closed)return;m_closed=true;++m_generation;m_request.cancel();Popup::onClose(sender);
}
void RankedTierGuidePopup::onExit() { m_closed=true;++m_generation;m_request.cancel();Popup::onExit(); }
} // namespace corum::ranked::ui
