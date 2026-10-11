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
constexpr float kBorderInset = 0.375f;

void drawBorderStroke(CCDrawNode* node, CCSize size, double phase, float length,
    ccColor3B color, float radius, float opacity) {
    auto const stroke = clockwiseBorderStroke(size.width - 2 * kBorderInset,
        size.height - 2 * kBorderInset,phase,length);
    for (std::size_t index = 0; index < stroke.count; ++index) {
        auto const& segment = stroke.segments[index];
        node->drawSegment({segment.from.x + kBorderInset,segment.from.y + kBorderInset},
            {segment.to.x + kBorderInset,segment.to.y + kBorderInset},radius,
            {color.r / 255.0f,color.g / 255.0f,color.b / 255.0f,opacity});
    }
}

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

RankedLeaderboardPopup* RankedLeaderboardPopup::create(RankedMode mode) {
    auto* popup = new RankedLeaderboardPopup;
    popup->m_mode = mode;
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
    m_caption = menuLabel("",0.26f,{size.width / 2,size.height - 68},TextMuted);
    panel->addChild(m_caption,5);
    auto const viewport = CCSize{size.width - 24,size.height - 134};
    auto const columns = leaderboardColumns(viewport.width - 8);
    leftCaption(panel,"#",{20 + columns.rank - 6,size.height - 88},30,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-player","PLAYER"),{16 + columns.name,size.height - 88},columns.nameWidth,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-status","STATUS"),{16 + columns.status,size.height - 88},columns.statusWidth,0.23f,TextMuted);
    leftCaption(panel,i18n::text("leaderboard-tier","TIER"),{16 + columns.tierName,size.height - 88},columns.tierWidth,0.23f,TextMuted);
    leftCaption(panel,"LP / O X",{16 + columns.value,size.height - 88},columns.valueWidth,0.23f,TextMuted);
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
    auto tab = [&](std::string const& text, float x, RankedMode mode) {
        auto* face = surface({140,23},{},Cyan,SurfaceRaised,250,true);
        auto* caption = menuLabel(text,0.28f,{70,11.5f}); fitMenuLabelWidth(caption,130,0.28f,0.20f); face->addChild(caption,5);
        auto* item = CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedLeaderboardPopup::onMode));
        item->setTag(static_cast<int>(mode)); item->setPosition({x,size.height-45}); menu->addChild(item); return item;
    };
    m_survivalTab = tab("SURVIVAL",size.width/2-76,RankedMode::Survival);
    m_soloTab = tab("SOLO RANKED",size.width/2+76,RankedMode::SoloRanked);
    m_previous = button("<",48,menu_selector(RankedLeaderboardPopup::onPrevious));
    m_next = button(">",size.width - 48,menu_selector(RankedLeaderboardPopup::onNext));
    button(i18n::text("close","CLOSE"),size.width / 2,menu_selector(RankedLeaderboardPopup::onClose));
    m_refresh = button(i18n::text("leaderboard-refresh","REFRESH"),size.width / 2 + 80,menu_selector(RankedLeaderboardPopup::onRefresh));
    m_pageLabel = menuLabel("",0.26f,{size.width / 2 - 80,17},TextMuted); panel->addChild(m_pageLabel,5);
    scheduleUpdate(); fetch(0); return true;
}

void RankedLeaderboardPopup::updateButtons() {
    m_survivalTab->setEnabled(!m_closed && m_mode != RankedMode::Survival);
    m_soloTab->setEnabled(!m_closed && m_mode != RankedMode::SoloRanked);
    for (auto* tab : {m_survivalTab,m_soloTab}) { tab->setCascadeOpacityEnabled(true); tab->setOpacity(tab->isEnabled() ? 180 : 255); }
    m_previous->setEnabled(!m_busy && m_offset > 0);
    m_next->setEnabled(!m_busy && m_hasMore);
    m_refresh->setEnabled(!m_busy && !m_closed);
    for (auto* button : {m_previous,m_next,m_refresh}) {
        button->setCascadeOpacityEnabled(true);
        button->setOpacity(button->isEnabled() ? 255 : 100);
    }
}

void RankedLeaderboardPopup::fetch(int offset) {
    if (m_closed || m_busy) return;
    m_busy = true; updateButtons();
    m_caption->setString(i18n::text("leaderboard-loading","Loading leaderboard...").c_str());
    fitMenuLabelWidth(m_caption,m_scroll->getContentSize().width,0.26f,0.22f);
    m_caption->setColor(TextMuted);
    auto const generation = ++m_generation;
    auto const mode = m_mode;
    web::WebRequest request; request.timeout(std::chrono::seconds(20));
    m_request.spawn(request.get(RankedRuntime::get().leaderboardUrl(offset,kPageSize,mode)),
        [weak = WeakRef(this),generation,offset,mode](web::WebResponse response) {
            // Retire/rebuild rows outside the request callback and keep the
            // single-flight lock until the entire list replacement is done.
            queueInMainThread([weak,generation,offset,mode,response = std::move(response)] {
                auto self = weak.lock();
                if (!self || self->m_closed || generation != self->m_generation || mode != self->m_mode) return;
                ++self->m_generation; // Consume this response exactly once.
                auto const data = response.json().unwrapOr(matjson::Value());
                if (!response.ok() || !data["players"].isArray() || data["players"].asArray().unwrap().size() > kPageSize) {
                    self->showError();
                } else {
                    self->m_offset = offset; self->showRows(data);
                }
                if (self->m_closed) return;
                self->m_busy = false;
                self->updateButtons();
            });
        });
}

void RankedLeaderboardPopup::onMode(CCObject* sender) {
    if (m_closed || !sender) return;
    auto const mode = sender->getTag() == 0 ? RankedMode::Survival : RankedMode::SoloRanked;
    if (mode == m_mode) return;
    ++m_generation; m_request.cancel(); m_busy = false;
    m_mode = mode; m_offset = 0; m_hasMore = false;
    m_rows.clear(); m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
    m_pageLabel->setString("..."); fetch(0);
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
        auto* badge = createAnimatedRankBadge(entry.tier,{27,27},RankBadgeDisplay::Leaderboard);
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
            // A soft continuous frame plus two short highlights. Drawing the
            // perimeter directly avoids bright round blobs and cross shapes.
            auto* glow = CCDrawNode::create(); row->addChild(glow,8);
            auto const lap = 2 * (width + kRowHeight);
            drawBorderStroke(glow,{width,kRowHeight},0,lap,color,2.4f,0.10f);
            drawBorderStroke(glow,{width,kRowHeight},0,lap,color,1.1f,0.24f);
            drawBorderStroke(glow,{width,kRowHeight},0,lap,Text,0.4f,0.48f);
            visual.shine = CCDrawNode::create(); row->addChild(visual.shine,9);
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
        row.shine->clear();
        auto const color = tierColor(row.entry.tier);
        for (auto const opposite : {0.0,0.5}) {
            auto const phase = m_elapsed / 6 + opposite;
            drawBorderStroke(row.shine,size,phase,32,color,2.6f,0.22f);
            drawBorderStroke(row.shine,size,phase,32,Text,1.1f,0.62f);
            drawBorderStroke(row.shine,size,phase,32,{255,255,255},0.45f,0.96f);
        }
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
