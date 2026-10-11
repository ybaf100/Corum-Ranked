#include "RankedLeaderboardPopup.hpp"

#include "../RankedRuntime.hpp"
#include "PlayerIcon.hpp"
#include "RankBadgeAssets.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "StandingVisuals.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <charconv>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
constexpr int kPageSize = 20;
constexpr float kRowHeight = 48, kRowGap = 7;

int accountId(matjson::Value const& value) {
    auto const text = value.asString().unwrapOr("");
    int id = 0;
    auto const result = std::from_chars(text.data(),text.data() + text.size(),id);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? id : 0;
}

void leftCaption(CCNode* node, std::string const& text, CCPoint position, float width,
    float scale, ccColor3B color = Text, FontWeight weight = FontWeight::ExtraBold) {
    auto* caption = menuLabel(text,scale,position,color,weight);
    caption->setAnchorPoint({0,0.5f}); fitMenuLabelWidth(caption,width,scale,0.22f);
    node->addChild(caption,5);
}
}

RankedLeaderboardPopup* RankedLeaderboardPopup::create() {
    auto* popup = new RankedLeaderboardPopup;
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup; return nullptr;
}

bool RankedLeaderboardPopup::init() {
    auto const window = CCDirector::sharedDirector()->getWinSize();
    auto const size = CCSize{std::min(560.0f,window.width - 24),std::min(294.0f,window.height - 22)};
    if (!Popup::init(size.width,size.height)) return false;
    setID("ranked-leaderboard-popup"_spr);
    m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
    auto* panel = surface(size,{size.width / 2,size.height / 2},Gold,Surface,255,true);
    m_mainLayer->addChild(panel,10);
    panel->addChild(menuLabel(i18n::text("leaderboard","LEADERBOARD"),0.51f,
        {size.width / 2,size.height - 20},Gold,FontWeight::Black),5);
    m_caption = menuLabel("",0.26f,{size.width / 2,size.height - 42},TextMuted);
    panel->addChild(m_caption,5);
    auto const viewport = CCSize{size.width - 24,size.height - 109};
    auto const columns = leaderboardColumns(viewport.width - 8);
    leftCaption(panel,"#",{20 + columns.rank - 6,size.height - 63},30,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-player","PLAYER"),{16 + columns.name,size.height - 63},columns.nameWidth,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-status","STATUS"),{16 + columns.status,size.height - 63},columns.statusWidth,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-tier","TIER"),{16 + columns.tierName,size.height - 63},columns.tierWidth,0.23f,TextMuted);
    leftCaption(panel,"LP / O X",{16 + columns.value,size.height - 63},columns.valueWidth,0.23f,TextMuted);
    m_scroll = ScrollLayer::create(viewport); m_scroll->setPosition({12,35});
    m_scroll->setID("ranked-leaderboard-scroll"_spr); panel->addChild(m_scroll,5);
    auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu,20);
    auto button = [&](std::string const& text, float x, SEL_MenuHandler callback) {
        auto* face = surface({68,25},{},Blue,SurfaceRaised,250,true);
        auto* caption = menuLabel(text,0.30f,{34,12.5f}); fitMenuLabelWidth(caption,60,0.30f,0.23f);
        face->addChild(caption,5);
        auto* item = CCMenuItemSpriteExtra::create(face,this,callback); item->setPosition({x,17});
        menu->addChild(item); return item;
    };
    m_previous = button("<",48,menu_selector(RankedLeaderboardPopup::onPrevious));
    m_next = button(">",size.width - 48,menu_selector(RankedLeaderboardPopup::onNext));
    button(i18n::text("close","CLOSE"),size.width / 2,menu_selector(RankedLeaderboardPopup::onClose));
    button(i18n::text("leaderboard-refresh","REFRESH"),size.width / 2 + 80,menu_selector(RankedLeaderboardPopup::onRefresh));
    m_pageLabel = menuLabel("",0.26f,{size.width / 2 - 80,17},TextMuted); panel->addChild(m_pageLabel,5);
    scheduleUpdate(); fetch(0); return true;
}

void RankedLeaderboardPopup::updateButtons() {
    m_previous->setEnabled(!m_busy && m_offset > 0);
    m_next->setEnabled(!m_busy && m_hasMore);
    m_previous->setCascadeOpacityEnabled(true); m_next->setCascadeOpacityEnabled(true);
    m_previous->setOpacity(m_previous->isEnabled() ? 255 : 100);
    m_next->setOpacity(m_next->isEnabled() ? 255 : 100);
}

void RankedLeaderboardPopup::fetch(int offset) {
    if (m_closed || m_busy) return;
    m_busy = true; updateButtons();
    m_caption->setString(i18n::text("leaderboard-loading","Loading leaderboard...").c_str());
    fitMenuLabelWidth(m_caption,m_scroll->getContentSize().width,0.26f,0.22f);
    m_caption->setColor(TextMuted);
    auto const generation = ++m_generation;
    web::WebRequest request; request.timeout(std::chrono::seconds(20));
    m_request.spawn(request.get(RankedRuntime::get().leaderboardUrl(offset,kPageSize)),
        [weak = WeakRef(this),generation,offset](web::WebResponse response) {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            if (!response.ok() || !data["players"].isArray() || data["players"].asArray().unwrap().size() > kPageSize) {
                self->showError(); self->updateButtons(); return;
            }
            self->m_offset = offset; self->showRows(data); self->updateButtons();
        });
}

void RankedLeaderboardPopup::showError() {
    m_caption->setString(i18n::text("leaderboard-error","Could not load the leaderboard. Press Refresh.").c_str());
    fitMenuLabelWidth(m_caption,m_scroll->getContentSize().width,0.26f,0.22f);
    m_caption->setColor(Red);
}

void RankedLeaderboardPopup::showRows(matjson::Value const& data) {
    m_rows.clear(); m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
    auto const players = data["players"].asArray().unwrap();
    auto const viewport = m_scroll->getContentSize();
    auto const width = viewport.width - 8;
    auto const columns = leaderboardColumns(width);
    auto const height = std::max(viewport.height,static_cast<float>(players.size()) * (kRowHeight + kRowGap) + 8);
    m_scroll->m_contentLayer->setContentSize({viewport.width,height});
    auto const total = data["total"].asInt().unwrapOr(0);
    auto const count = i18n::isKorean() ? fmt::format("총 {}명",total) : fmt::format("{} ranked players",total);
    m_caption->setString(count.c_str()); m_caption->setColor(TextMuted);
    fitMenuLabelWidth(m_caption,viewport.width,0.26f,0.22f);
    m_hasMore = data["hasMore"].asBool().unwrapOr(false);
    m_pageLabel->setString(fmt::format("{} - {}",players.empty() ? 0 : m_offset + 1,m_offset + players.size()).c_str());
    auto const local = GJAccountManager::sharedState()->m_accountID;
    for (std::size_t index = 0; index < players.size(); ++index) {
        auto const& player = players[index];
        LeaderboardEntry entry;
        if (auto rank = player["rank"].asInt(); rank.isOk() && rank.unwrap() > 0) entry.rank = static_cast<int>(rank.unwrap());
        if (auto lp = player["visibleRankedScore"].asInt(); lp.isOk()) entry.lp = static_cast<int>(lp.unwrap());
        entry.accountId = accountId(player["gdAccountId"]); entry.bot = player["isBot"].asBool().unwrapOr(false);
        entry.player = player["player"].asString().unwrapOr("?"); entry.tier = player["tier"].asString().unwrapOr("UNRANKED");
        entry.promotion = player["promotion"].isObject(); entry.warning = !entry.promotion && player["demotionWarning"].asBool().unwrapOr(false);
        entry.promotionGames = static_cast<int>(player["promotion"]["gamesRequired"].asInt().unwrapOr(0));
        if (auto marks = player["promotion"]["results"].asArray(); marks.isOk())
            for (auto const& mark : marks.unwrap()) entry.marks.push_back(mark.asString().unwrapOr(""));
        auto const color = tierColor(entry.tier);
        auto const mine = !entry.bot && entry.accountId > 0 && entry.accountId == local;
        auto* row = surface({width,kRowHeight},{},color,mine ? SurfaceSoft : SurfaceRaised,250,true);
        row->setAnchorPoint(CCPointZero); row->setPosition({4,height - 4 - (index + 1) * (kRowHeight + kRowGap)});
        row->setID("ranked-leaderboard-row"_spr); m_scroll->m_contentLayer->addChild(row,1);
        auto* rank = standingNumber(entry.rank,0.38f); rank->setPosition({columns.rank,24});
        if (rank->getContentSize().width > 40) rank->setScale(40 / rank->getContentSize().width);
        row->addChild(rank,5);
        leftCaption(row,entry.player,{columns.name,24},columns.nameWidth,0.36f,mine ? Cyan : Text);
        if (entry.promotion || entry.warning) leftCaption(row,
            entry.promotion ? i18n::text("promotion-match","PROMOTION MATCH") : i18n::text("demotion-warning","DEMOTION WARNING"),
            {columns.status,24},columns.statusWidth,0.26f,entry.promotion ? Green : Red);
        auto* badge = createAnimatedRankBadge(entry.tier,{27,27},RankBadgeDisplay::Matched);
        badge->setPosition({columns.tierIcon,24}); row->addChild(badge,4);
        leftCaption(row,entry.tier,{columns.tierName,24},columns.tierWidth,0.31f,color);
        if (entry.promotion) {
            auto const count = std::clamp(entry.promotionGames,1,8);
            for (int mark = 0; mark < count; ++mark) {
                auto const result = static_cast<std::size_t>(mark) < entry.marks.size() ? entry.marks[mark] : "";
                leftCaption(row,result == "WIN" ? "O" : result == "LOSS" ? "X" : "_",
                    {columns.value + mark * columns.valueWidth / count,24},columns.valueWidth / count,
                    0.31f,result == "WIN" ? Green : result == "LOSS" ? Red : TextMuted);
            }
        } else leftCaption(row,leaderboardValue(entry),{columns.value,24},columns.valueWidth,0.32f);
        Row visual; visual.node = row; visual.entry = std::move(entry);
        if (visual.entry.tier == "GOLD" || visual.entry.promotion) {
            // Reuse existing image-pack glow; no new downloadable art.
            for (int trail = 0; trail < 5; ++trail) if (auto* glow = rankResourceSprite("fx-rank-radial-glow-white.png")) {
                fitRankSprite(glow,{18,18}); glow->setColor(color); glow->setOpacity(static_cast<GLubyte>(180 - trail * 28));
                glow->setBlendFunc({GL_SRC_ALPHA,GL_ONE}); row->addChild(glow,8); visual.trail.push_back(glow);
            }
            auto* shine = CCNode::create(); row->addChild(shine,9); visual.shine = shine;
            for (auto const size : {CCSize{9,1},CCSize{1,9},CCSize{2,2}}) {
                auto* light = CCLayerColor::create({255,255,255,220},size.width,size.height);
                light->setPosition({-size.width / 2,-size.height / 2}); shine->addChild(light);
            }
        }
        m_rows.push_back(std::move(visual));
    }
    if (players.empty()) m_caption->setString(i18n::text("leaderboard-empty","No players yet.").c_str());
    m_scroll->scrollToTop(); update(0);
}

void RankedLeaderboardPopup::update(float dt) {
    if (m_closed || !m_scroll) return;
    m_elapsed += std::max(0.0f,dt);
    auto const viewport = m_scroll->getContentSize();
    auto const local = GJAccountManager::sharedState()->m_accountID;
    for (auto& row : m_rows) {
        auto const position = m_scroll->convertToNodeSpace(row.node->convertToWorldSpace(CCPointZero));
        auto const visible = position.y + kRowHeight >= 0 && position.y <= viewport.height;
        // Only rows in the viewport request GD cosmetics. Hidden rows release
        // their exact delegate instead of starting a request for the whole page.
        if (visible && !row.icon) {
            row.icon = !row.entry.bot && row.entry.accountId > 0 && row.entry.accountId == local
                ? PlayerIcon::createLocal() : PlayerIcon::createRemote(row.entry.bot ? 0 : row.entry.accountId);
            if (row.icon) { row.icon->setScale(0.67f); row.icon->setPosition({leaderboardColumns(row.node->getContentSize().width).icon,24}); row.node->addChild(row.icon,5); }
        } else if (!visible && row.icon) { row.icon->removeFromParentAndCleanup(true); row.icon = nullptr; }
        if (!visible || !row.shine) continue;
        auto const size = row.node->getContentSize();
        for (std::size_t index = 0; index < row.trail.size(); ++index) {
            auto const point = clockwiseBorderPoint(size.width,size.height,m_elapsed / 6 - index * 0.004);
            row.trail[index]->setPosition({point.x,point.y});
        }
        auto const point = clockwiseBorderPoint(size.width,size.height,m_elapsed / 6);
        row.shine->setPosition({point.x,point.y});
    }
}

void RankedLeaderboardPopup::onPrevious(CCObject*) { if (m_offset >= kPageSize) fetch(m_offset - kPageSize); }
void RankedLeaderboardPopup::onNext(CCObject*) { if (m_hasMore) fetch(m_offset + kPageSize); }
void RankedLeaderboardPopup::onRefresh(CCObject*) { fetch(m_offset); }
void RankedLeaderboardPopup::onClose(CCObject*) { dismiss(); }
void RankedLeaderboardPopup::dismiss() {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedLeaderboardPopup> keepAlive(this);
    m_closed = true; ++m_generation; m_request.cancel(); unscheduleUpdate();
    Popup::onClose(nullptr);
}
void RankedLeaderboardPopup::onExit() {
    m_closed = true; ++m_generation; m_request.cancel(); unscheduleUpdate();
    if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
    Popup::onExit();
}
} // namespace corum::ranked::ui
