#include "RankedMapPoolPopup.hpp"

#include "../HistoryLevelLookup.hpp"
#include "../RankedRuntime.hpp"
#include "../domain/MapDifficulty.hpp"
#include "../domain/RankTierPresentation.hpp"
#include "MapDifficultyBadge.hpp"
#include "RankedTapScroll.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <algorithm>
#include <charconv>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
constexpr float kRowHeight = 30, kRowGap = 4;
constexpr std::size_t kMaxMaps = 200;
constexpr int kListTag = 1, kInfoTag = 2;

ccColor3B poolColor(int pool) {
    switch (pool) {
        case 1: return Green;
        case 2: return Cyan;
        case 3: return Blue;
        case 4: return Gold;
        case 5: return Orange;
        default: return Red;
    }
}

CCLabelBMFont* caption(CCNode* parent, std::string const& text, CCPoint at, float scale, float width,
    ccColor3B color = Text, FontWeight weight = FontWeight::SemiBold, float anchorX = 0.5f) {
    auto* label = menuLabel(text, scale, at, color, weight);
    label->setAnchorPoint({anchorX, 0.5f});
    fitMenuLabelWidth(label, width, scale, scale * 0.55f);
    parent->addChild(label, 6);
    return label;
}

CCNode* buttonFace(std::string const& text, CCSize size, ccColor3B accent, bool selected, float scale = 0.24f,
    ccColor3B color = Text) {
    auto* face = surface(size, {}, selected ? Gold : accent, selected ? SurfaceSoft : SurfaceRaised, 250, true);
    auto* label = menuLabel(text, scale, {size.width / 2, size.height / 2}, selected ? Gold : color, FontWeight::ExtraBold);
    fitMenuLabelWidth(label, size.width - 6, scale, scale * 0.55f);
    face->addChild(label, 6);
    return face;
}

void fillRect(CCDrawNode* node, CCRect rect, ccColor3B color, float alpha = 1.0f) {
    if (rect.size.width <= 0 || rect.size.height <= 0) return;
    CCPoint corners[4] = {
        CCPoint{rect.getMinX(), rect.getMinY()}, CCPoint{rect.getMaxX(), rect.getMinY()},
        CCPoint{rect.getMaxX(), rect.getMaxY()}, CCPoint{rect.getMinX(), rect.getMaxY()}};
    auto const fill = ccc4f(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha);
    node->drawPolygon(corners, 4, fill, 0, fill);
}

CCDrawNode* drawNode(CCNode* parent, int z = 4) {
    auto* node = CCDrawNode::create();
    // Straight alpha: the polygons carry their own alpha.
    node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    parent->addChild(node, z);
    return node;
}

std::optional<double> number(matjson::Value const& value) {
    if (!value.isNumber()) return std::nullopt;
    auto const result = value.asDouble();
    if (!result.isOk() || !std::isfinite(result.unwrap())) return std::nullopt;
    return result.unwrap();
}
int integer(matjson::Value const& value) {
    return static_cast<int>(std::clamp(std::llround(number(value).value_or(0)), -1'000'000'000LL, 1'000'000'000LL));
}
std::optional<int> optionalInteger(matjson::Value const& value) {
    auto const result = number(value);
    if (!result) return std::nullopt;
    return static_cast<int>(std::clamp(std::llround(*result), -1'000'000LL, 1'000'000LL));
}
std::string text(matjson::Value const& value, std::size_t limit = 120) {
    auto result = value.asString().unwrapOr("");
    if (result.size() > limit) result.resize(limit);
    return result;
}

MapPoolRow parseRow(matjson::Value const& value) {
    MapPoolRow row;
    row.canonicalLevelId = text(value["canonicalLevelId"], 16);
    row.levelId = text(value["levelId"], 16);
    row.title = text(value["title"]);
    row.creator = text(value["creator"]);
    row.difficulty = text(value["difficulty"], 32);
    row.kind = text(value["kind"], 16);
    row.gddlColor = text(value["gddlColor"], 16);
    row.difficultyRank = integer(value["difficultyRank"]);
    row.pool = std::clamp(integer(value["pool"]), 1, 6);
    row.qualifyingPercent = number(value["qualifyingPercent"]).value_or(0);
    row.stars = optionalInteger(value["stars"]);
    row.gddlTier = optionalInteger(value["gddlTier"]);
    row.durationSeconds = number(value["durationSeconds"]);
    row.rounds = std::max(0, integer(value["rounds"]));
    row.draws = std::max(0, integer(value["draws"]));
    row.banOffers = std::max(0, integer(value["banOffers"]));
    row.bans = std::max(0, integer(value["bans"]));
    row.share = number(value["share"]);
    row.drawRate = number(value["drawRate"]);
    row.banRate = number(value["banRate"]);
    return row;
}

MapMetadata metadataOf(MapPoolRow const& row) {
    MapMetadata metadata;
    metadata.kind = row.kind.empty() ? "UNKNOWN" : row.kind;
    metadata.stars = row.stars;
    metadata.gddlTier = row.gddlTier;
    metadata.gddlColor = row.gddlColor;
    if (row.durationSeconds) metadata.durationSeconds = *row.durationSeconds;
    return metadata;
}

web::WebRequest mapPoolRequest() {
    web::WebRequest request;
    auto const& token = RankedRuntime::get().sessionToken();
    if (!token.empty()) request.header("Authorization", "Bearer " + token);
    request.followRedirects(false);
    request.timeout(std::chrono::seconds(20));
    return request;
}

int levelNumber(std::string const& id) {
    int value = 0;
    auto const result = std::from_chars(id.data(), id.data() + id.size(), value);
    return result.ec == std::errc{} && result.ptr == id.data() + id.size() && value > 0 ? value : 0;
}

std::string sortLabel(MapPoolSort sort) {
    switch (sort) {
        case MapPoolSort::Difficulty: return i18n::text("map-pool-sort-difficulty", "DIFFICULTY");
        case MapPoolSort::Share: return i18n::text("map-pool-sort-share", "PLAYED");
        case MapPoolSort::Draw: return i18n::text("map-pool-sort-draw", "DRAWS");
        case MapPoolSort::Ban: return i18n::text("map-pool-sort-ban", "BANS");
        case MapPoolSort::Rounds: return i18n::text("map-pool-sort-rounds", "ROUNDS");
    }
    return {};
}

// Row columns from the right edge, so the title gets the remaining width.
struct Columns {
    float rank = 12, pool = 31, title = 46, titleWidth = 0, difficulty = 0, difficultyWidth = 64;
    float barLeft = 0, barWidth = 30, share = 0, draw = 0, ban = 0, rounds = 0, info = 0, view = 0;
    explicit Columns(float width) {
        view = width - 28; info = view - 48; rounds = info - 38; ban = rounds - 30; draw = ban - 32; share = draw - 32;
        barLeft = share - 15 - 4 - barWidth; difficulty = barLeft - 4 - difficultyWidth;
        titleWidth = std::max(40.0f, difficulty - 4 - title);
    }
};
} // namespace

RankedMapPoolPopup* RankedMapPoolPopup::create() {
    auto* popup = new RankedMapPoolPopup;
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool RankedMapPoolPopup::init() {
    auto const window = CCDirector::sharedDirector()->getWinSize();
    auto const size = CCSize{std::min(560.0f, window.width - 24), std::min(294.0f, window.height - 22)};
    if (!Popup::init(size.width, size.height)) return false;
    setID("ranked-map-pool-popup"_spr);
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_panel = surface(size, {size.width / 2, size.height / 2}, Cyan, Surface, 255, true);
    m_mainLayer->addChild(m_panel, 10);
    buildListChrome();
    scheduleUpdate();
    fetchList();
    return true;
}

void RankedMapPoolPopup::buildListChrome() {
    auto const size = m_panel->getContentSize();
    m_listView = CCNode::create();
    m_listView->setContentSize(size);
    m_listView->setTag(kListTag);
    m_panel->addChild(m_listView, 5);
    caption(m_listView, i18n::text("map-pool", "MAP POOL"), {size.width / 2, size.height - 18}, 0.48f, 200, Gold, FontWeight::Black);
    m_caption = caption(m_listView, "", {size.width / 2, size.height - 36}, 0.24f, size.width * 0.5f, TextMuted);
    m_updated = caption(m_listView, "", {size.width - 44, size.height - 37}, 0.19f, 84, TextMuted);

    m_menu = CCMenu::create();
    m_menu->setPosition(CCPointZero);
    m_listView->addChild(m_menu, 20);
    auto* close = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("close", "CLOSE"), {58, 22}, Blue, false),
        this, menu_selector(RankedMapPoolPopup::onClose));
    close->setPosition({40, size.height - 20});
    m_menu->addChild(close);
    m_refresh = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("leaderboard-refresh", "REFRESH"), {72, 22}, Cyan, false),
        this, menu_selector(RankedMapPoolPopup::onRefresh));
    m_refresh->setPosition({size.width - 44, size.height - 20});
    m_menu->addChild(m_refresh);

    m_summary = CCNode::create();
    m_summary->setContentSize({size.width - 24, 26});
    m_summary->setPosition({12, size.height - 74});
    m_listView->addChild(m_summary, 5);

    // Pool filter (left) and order (right), one row above the column captions.
    auto const controlsY = size.height - 92;
    for (int pool = 0; pool <= 6; ++pool) {
        auto const width = pool == 0 ? 36.0f : 28.0f;
        auto* item = CCMenuItemSpriteExtra::create(CCNode::create(), this, menu_selector(RankedMapPoolPopup::onPool));
        item->setTag(pool);
        item->setContentSize({width, 20});
        auto const x = pool == 0 ? 12 + width / 2 : 12 + 36 + 3 + (pool - 1) * 31.0f + width / 2;
        item->setPosition({x, controlsY});
        m_menu->addChild(item);
        m_poolTabs.push_back(item);
    }
    auto right = size.width - 12;
    for (auto index = kMapPoolSorts.size(); index-- > 0;) {
        auto const width = index == 0 ? 54.0f : 46.0f;
        auto* item = CCMenuItemSpriteExtra::create(CCNode::create(), this, menu_selector(RankedMapPoolPopup::onSort));
        item->setTag(static_cast<int>(index));
        item->setContentSize({width, 20});
        item->setPosition({right - width / 2, controlsY});
        right -= width + 3;
        m_menu->addChild(item);
        m_sortTabs.insert(m_sortTabs.begin(), item);
    }
    caption(m_listView, i18n::text("map-pool-order", "ORDER"), {right - 2, controlsY}, 0.2f, 40, TextMuted, FontWeight::SemiBold, 1.0f);

    auto const viewport = CCSize{size.width - 24, controlsY - 30};
    Columns const columns(viewport.width - 8);
    auto const headerY = controlsY - 20;
    auto header = [&](std::string const& label, float x, float width, MapPoolSort sort, float anchor = 0.5f) {
        auto* title = caption(m_listView, label, {16 + x, headerY}, 0.2f, width, TextMuted, FontWeight::ExtraBold, anchor);
        title->setTag(100 + static_cast<int>(sort));
    };
    caption(m_listView, "#", {16 + columns.rank, headerY}, 0.2f, 20, TextMuted, FontWeight::ExtraBold);
    caption(m_listView, i18n::text("map-pool-col-map", "MAP"), {16 + columns.title, headerY}, 0.2f, columns.titleWidth, TextMuted, FontWeight::ExtraBold, 0);
    header(sortLabel(MapPoolSort::Difficulty), columns.difficulty, columns.difficultyWidth, MapPoolSort::Difficulty, 0);
    header(sortLabel(MapPoolSort::Share), columns.barLeft, 70, MapPoolSort::Share, 0);
    header(sortLabel(MapPoolSort::Draw), columns.draw, 32, MapPoolSort::Draw);
    header(sortLabel(MapPoolSort::Ban), columns.ban, 30, MapPoolSort::Ban);
    header(sortLabel(MapPoolSort::Rounds), columns.rounds, 36, MapPoolSort::Rounds);

    m_scroll = createRankedTapScroll(viewport, [weak = WeakRef(this)](CCPoint start, CCPoint end) {
        auto self = weak.lock();
        if (!self || self->m_closed || self->m_detailIndex) return;
        auto const inside = [](CCNode* node, CCPoint at) {
            if (!node) return false;
            auto const p = node->convertToNodeSpace(at);
            auto const s = node->getContentSize();
            return p.x >= 0 && p.y >= 0 && p.x <= s.width && p.y <= s.height;
        };
        for (auto const& hit : self->m_hits) {
            if (!inside(hit.row, start) || !inside(hit.row, end)) continue;
            auto const view = inside(hit.view, end);
            auto const index = hit.index;
            auto const generation = self->m_generation;
            queueInMainThread([weak, index, view, generation] {
                auto popup = weak.lock();
                if (!popup || popup->m_closed || popup->m_detailIndex || generation != popup->m_generation) return;
                if (view) popup->viewLevel(index); else popup->openInfo(index);
            });
            break;
        }
    });
    m_scroll->setPosition({12, 8});
    m_scroll->setID("ranked-map-pool-scroll"_spr);
    m_listView->addChild(m_scroll, 5);
    updateControls();
}

void RankedMapPoolPopup::updateControls() {
    if (m_closed) return;
    for (auto* item : m_poolTabs) {
        auto const pool = item->getTag();
        auto const selected = pool == m_pool;
        item->setNormalImage(buttonFace(pool == 0 ? i18n::text("map-pool-all", "ALL") : fmt::format("P{}", pool),
            item->getContentSize(), pool == 0 ? Blue : poolColor(pool), selected, 0.22f, pool == 0 ? Text : poolColor(pool)));
        item->setEnabled(!m_closed);
    }
    for (auto* item : m_sortTabs) {
        auto const sort = kMapPoolSorts[static_cast<std::size_t>(item->getTag())];
        item->setNormalImage(buttonFace(sortLabel(sort), item->getContentSize(), Blue, sort == m_sort, 0.21f));
        if (auto* header = m_listView->getChildByTag(100 + static_cast<int>(sort)))
            if (auto* label = typeinfo_cast<CCLabelBMFont*>(header)) label->setColor(sort == m_sort ? Gold : TextMuted);
    }
    m_refresh->setEnabled(!m_busy && !m_closed);
    m_refresh->setCascadeOpacityEnabled(true);
    m_refresh->setOpacity(m_refresh->isEnabled() ? 255 : 110);
}

void RankedMapPoolPopup::fetchList() {
    if (m_closed || m_busy) return;
    m_busy = true;
    updateControls();
    if (!m_hasList) {
        m_caption->setString(i18n::text("map-pool-loading", "Loading map pool...").c_str());
        m_caption->setColor(TextMuted);
    }
    auto const generation = ++m_generation;
    auto request = mapPoolRequest();
    m_request.spawn(request.get(RankedRuntime::get().mapPoolUrl()), [weak = WeakRef(this), generation](web::WebResponse response) {
        queueInMainThread([weak, generation, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            auto const maps = data["maps"].asArray();
            if (!response.ok() || !maps.isOk() || maps.unwrap().size() > kMaxMaps) {
                self->showListError();
            } else {
                self->m_rows.clear();
                for (auto const& map : maps.unwrap()) {
                    auto row = parseRow(map);
                    if (!row.canonicalLevelId.empty()) self->m_rows.push_back(std::move(row));
                }
                self->m_totalRounds = std::max(0, integer(data["totals"]["rounds"]));
                self->m_totalDrawRate = number(data["totals"]["drawRate"]);
                self->m_mostPlayed = text(data["mostPlayed"], 16);
                self->m_mostBanned = text(data["mostBanned"], 16);
                self->m_hasList = true;
                self->m_receivedAt = std::chrono::steady_clock::now();
                self->m_shownMinutes = -1;
                self->showList();
            }
            self->updateControls();
            if (!self->m_detailIndex) return;
            // Rows may have moved or left the pool: follow the open map by ID.
            auto const open = std::find_if(self->m_rows.begin(), self->m_rows.end(),
                [&](auto const& row) { return row.canonicalLevelId == self->m_detailId; });
            if (open == self->m_rows.end()) { self->backToList(); return; }
            self->m_detailIndex = static_cast<std::size_t>(open - self->m_rows.begin());
            self->fetchDetail();
        });
    });
}

void RankedMapPoolPopup::showListError() {
    m_caption->setString(i18n::text("map-pool-error", "Could not load the map pool. Press Refresh.").c_str());
    fitMenuLabelWidth(m_caption, m_panel->getContentSize().width * 0.5f, 0.24f, 0.14f);
    m_caption->setColor(Red);
}

void RankedMapPoolPopup::updateSummary() {
    m_summary->removeAllChildrenWithCleanup(true);
    auto const size = m_summary->getContentSize();
    auto const width = (size.width - 3 * 6) / 4;
    auto const titleOf = [&](std::string const& id) {
        auto row = std::find_if(m_rows.begin(), m_rows.end(), [&](auto const& entry) { return entry.canonicalLevelId == id; });
        return row == m_rows.end() ? i18n::text("map-pool-none", "None yet") : row->title;
    };
    auto const tile = [&](int index, std::string const& label, std::string const& value, ccColor3B accent) {
        auto* box = surface({width, size.height}, {}, accent, SurfaceRaised, 240, false);
        box->setAnchorPoint(CCPointZero);
        box->setPosition({index * (width + 6), 0});
        m_summary->addChild(box);
        caption(box, label, {7, size.height - 8}, 0.17f, width - 12, TextMuted, FontWeight::SemiBold, 0);
        caption(box, value, {7, 9}, 0.26f, width - 12, Text, FontWeight::Black, 0);
    };
    tile(0, i18n::text("map-pool-total-rounds", "Rounds played"), mapPoolNumber(m_totalRounds), Blue);
    tile(1, i18n::text("map-pool-total-draws", "Draw rate"), mapPoolPercent(m_totalDrawRate), Blue);
    tile(2, i18n::text("map-pool-most-played", "Most played"), titleOf(m_mostPlayed), Gold);
    tile(3, i18n::text("map-pool-most-banned", "Most banned"), titleOf(m_mostBanned), Red);
}

void RankedMapPoolPopup::showList() {
    m_caption->setString(fmt::format(fmt::runtime(i18n::text("map-pool-summary", "{} active maps · {} rounds")),
        m_rows.size(), mapPoolNumber(m_totalRounds)).c_str());
    fitMenuLabelWidth(m_caption, m_panel->getContentSize().width * 0.5f, 0.24f, 0.14f);
    m_caption->setColor(TextMuted);
    updateSummary();
    rebuildRows();
}

void RankedMapPoolPopup::rebuildRows() {
    if (!m_scroll) return;
    m_hits.clear();
    m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
    auto const viewport = m_scroll->getContentSize();
    auto const width = viewport.width - 8;
    Columns const columns(width);
    auto const order = mapPoolOrder(m_rows, m_pool, m_sort);
    auto const height = std::max(viewport.height, static_cast<float>(order.size()) * (kRowHeight + kRowGap) + 8);
    m_scroll->m_contentLayer->setContentSize({viewport.width, height});
    double maxShare = 0;
    for (auto const& row : m_rows) maxShare = std::max(maxShare, row.share.value_or(0));
    if (order.empty() && m_hasList)
        caption(m_scroll->m_contentLayer, i18n::text("map-pool-empty", "No maps in the pool."),
            {viewport.width / 2, height - 24}, 0.26f, viewport.width - 20, TextMuted);
    for (std::size_t position = 0; position < order.size(); ++position) {
        auto const index = order[position];
        auto const& map = m_rows[index];
        auto* row = surface({width, kRowHeight}, {}, poolColor(map.pool), SurfaceRaised, 250, false);
        row->setAnchorPoint(CCPointZero);
        row->setPosition({4, height - 4 - (position + 1) * (kRowHeight + kRowGap)});
        row->setID("ranked-map-pool-row"_spr);
        m_scroll->m_contentLayer->addChild(row, 1);
        auto const ranked = m_sort != MapPoolSort::Difficulty && position < 3;
        caption(row, std::to_string(position + 1), {columns.rank, kRowHeight / 2}, 0.26f, 20, ranked ? Gold : TextMuted, FontWeight::Black);
        auto* chip = surface({22, 15}, {columns.pool, kRowHeight / 2}, poolColor(map.pool), Ink, 250, false);
        row->addChild(chip, 4);
        caption(chip, fmt::format("P{}", map.pool), {11, 7.5f}, 0.2f, 18, poolColor(map.pool), FontWeight::Black);
        caption(row, map.title, {columns.title, 20}, 0.29f, columns.titleWidth, Text, FontWeight::Black, 0);
        caption(row, map.creator.empty() ? "-" : "by " + map.creator, {columns.title, 9}, 0.18f, columns.titleWidth, TextMuted, FontWeight::SemiBold, 0);
        auto* badge = mapDifficultyBadge(levelNumber(map.levelId), metadataOf(map), columns.difficultyWidth, 17, true, recordedMapDifficulty(map.difficulty));
        badge->setPosition({columns.difficulty, (kRowHeight - 17) / 2});
        row->addChild(badge, 5);
        auto* bars = drawNode(row);
        fillRect(bars, {columns.barLeft, 11, columns.barWidth, 8}, Ink);
        auto const fill = maxShare > 0 ? static_cast<float>(map.share.value_or(0) / maxShare) * columns.barWidth : 0.0f;
        fillRect(bars, {columns.barLeft, 11, fill, 8}, Cyan);
        caption(row, mapPoolPercent(map.share), {columns.share, kRowHeight / 2}, 0.22f, 30, Text, FontWeight::ExtraBold);
        caption(row, mapPoolPercent(map.drawRate), {columns.draw, kRowHeight / 2}, 0.22f, 32, Text, FontWeight::ExtraBold);
        caption(row, mapPoolPercent(map.banRate), {columns.ban, kRowHeight / 2}, 0.22f, 30, Text, FontWeight::ExtraBold);
        caption(row, mapPoolNumber(map.rounds), {columns.rounds, kRowHeight / 2}, 0.22f, 34, TextMuted, FontWeight::ExtraBold);
        auto* info = buttonFace(i18n::text("map-pool-info", "INFO"), {44, 20}, Gold, false, 0.2f, Gold);
        info->setPosition({columns.info, kRowHeight / 2});
        row->addChild(info, 6);
        auto* view = buttonFace(i18n::text("history-view-level", "VIEW LEVEL"), {44, 20}, Cyan, false, 0.2f, Cyan);
        view->setPosition({columns.view, kRowHeight / 2});
        row->addChild(view, 6);
        m_hits.push_back({row, info, view, index});
    }
    m_scroll->scrollToTop();
}

void RankedMapPoolPopup::onPool(CCObject* sender) {
    if (m_closed || !sender || m_detailIndex) return;
    auto const pool = std::clamp(sender->getTag(), 0, 6);
    if (pool == m_pool) return;
    m_pool = pool;
    updateControls();
    rebuildRows();
}

void RankedMapPoolPopup::onSort(CCObject* sender) {
    if (m_closed || !sender || m_detailIndex) return;
    auto const index = static_cast<std::size_t>(std::clamp(sender->getTag(), 0, static_cast<int>(kMapPoolSorts.size()) - 1));
    if (kMapPoolSorts[index] == m_sort) return;
    m_sort = kMapPoolSorts[index];
    updateControls();
    rebuildRows();
}

void RankedMapPoolPopup::onRefresh(CCObject*) { fetchList(); }

void RankedMapPoolPopup::viewLevel(std::size_t index) {
    if (m_closed || index >= m_rows.size()) return;
    // Same flow as the history VIEW LEVEL button: busy check, lookup popup,
    // then the native level scene. This popup stays for the way back.
    showHistoryLevel(levelNumber(m_rows[index].levelId));
}

void RankedMapPoolPopup::onViewLevel(CCObject*) {
    if (m_detailIndex) viewLevel(*m_detailIndex);
}

void RankedMapPoolPopup::openInfo(std::size_t index) {
    if (m_closed || index >= m_rows.size()) return;
    m_detailIndex = index;
    m_detailId = m_rows[index].canonicalLevelId;
    m_listView->setVisible(false);
    // The tap scroll only answers touches inside its bounds: park it off the
    // panel so it cannot take touches meant for the info view's buttons.
    m_scroll->setPositionX(-100000);
    if (m_infoView) m_infoView->removeFromParentAndCleanup(true);
    m_infoView = CCNode::create();
    m_infoView->setContentSize(m_panel->getContentSize());
    m_infoView->setTag(kInfoTag);
    m_panel->addChild(m_infoView, 6);
    showDetailMessage(i18n::text("map-info-loading", "Loading map statistics..."), TextMuted);
    fetchDetail();
}

void RankedMapPoolPopup::backToList() {
    if (m_closed) return;
    ++m_generation;
    m_request.cancel();
    m_busy = false;
    m_detailIndex.reset();
    m_detailId.clear();
    if (m_infoView) { m_infoView->removeFromParentAndCleanup(true); m_infoView = nullptr; }
    m_listView->setVisible(true);
    m_scroll->setPositionX(12);
    updateControls();
}

void RankedMapPoolPopup::onBack(CCObject*) { backToList(); }

void RankedMapPoolPopup::fetchDetail() {
    if (m_closed || !m_detailIndex || *m_detailIndex >= m_rows.size()) return;
    m_busy = true;
    auto const generation = ++m_generation;
    auto request = mapPoolRequest();
    auto const id = m_rows[*m_detailIndex].canonicalLevelId;
    m_request.spawn(request.get(RankedRuntime::get().mapPoolUrl(id)), [weak = WeakRef(this), generation, id](web::WebResponse response) {
        queueInMainThread([weak, generation, id, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation || !self->m_detailIndex) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            if (!response.ok() || !data["stats"].isObject() || text(data["map"]["canonicalLevelId"], 16) != id)
                self->showDetailMessage(i18n::text("map-info-error", "Could not load this map. Press Refresh."), Red);
            else self->showDetail(data);
        });
    });
}

void RankedMapPoolPopup::showDetailMessage(std::string const& message, ccColor3B color) {
    if (!m_infoView || !m_detailIndex) return;
    m_infoView->removeAllChildrenWithCleanup(true);
    auto const size = m_infoView->getContentSize();
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_infoView->addChild(menu, 20);
    auto* back = CCMenuItemSpriteExtra::create(buttonFace("< " + i18n::text("map-pool", "MAP POOL"), {70, 22}, Blue, false),
        this, menu_selector(RankedMapPoolPopup::onBack));
    back->setPosition({48, size.height - 20});
    menu->addChild(back);
    auto* refresh = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("leaderboard-refresh", "REFRESH"), {72, 22}, Cyan, false),
        this, menu_selector(RankedMapPoolPopup::onRefresh));
    refresh->setPosition({size.width - 44, size.height - 20});
    menu->addChild(refresh);
    auto const& map = m_rows[*m_detailIndex];
    caption(m_infoView, map.title, {size.width / 2, size.height - 18}, 0.46f, size.width - 200, Gold, FontWeight::Black);
    caption(m_infoView, message, {size.width / 2, size.height / 2}, 0.28f, size.width - 60, color);
}

void RankedMapPoolPopup::showDetail(matjson::Value const& data) {
    if (!m_infoView || !m_detailIndex) return;
    m_infoView->removeAllChildrenWithCleanup(true);
    auto const size = m_infoView->getContentSize();
    auto map = parseRow(data["map"]);
    m_rows[*m_detailIndex] = map; // the detail read is newer than the list row
    auto const& stats = data["stats"];
    auto const& mine = data["mine"];

    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_infoView->addChild(menu, 20);
    auto* back = CCMenuItemSpriteExtra::create(buttonFace("< " + i18n::text("map-pool", "MAP POOL"), {70, 22}, Blue, false),
        this, menu_selector(RankedMapPoolPopup::onBack));
    back->setPosition({48, size.height - 20});
    menu->addChild(back);
    auto* refresh = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("leaderboard-refresh", "REFRESH"), {72, 22}, Cyan, false),
        this, menu_selector(RankedMapPoolPopup::onRefresh));
    refresh->setPosition({size.width - 44, size.height - 20});
    menu->addChild(refresh);

    caption(m_infoView, map.title, {size.width / 2, size.height - 16}, 0.46f, size.width - 200, Gold, FontWeight::Black);
    auto const rank = optionalInteger(data["shareRank"]);
    auto const subtitle = (map.creator.empty() ? std::string() : "by " + map.creator + "  ·  ") + (rank
        ? fmt::format(fmt::runtime(i18n::text("map-info-rank", "#{} of {} by plays")), *rank, integer(data["mapCount"]))
        : i18n::text("map-info-unplayed", "No rounds on this map yet."));
    caption(m_infoView, subtitle, {size.width / 2, size.height - 34}, 0.21f, size.width - 200, TextMuted);

    // Left column: the map itself, the viewer's record and VIEW LEVEL.
    constexpr float left = 12, leftWidth = 176;
    auto* card = surface({leftWidth, size.height - 54}, {}, poolColor(map.pool), SurfaceRaised, 245, false);
    card->setAnchorPoint(CCPointZero);
    card->setPosition({left, 10});
    m_infoView->addChild(card, 2);
    auto const cardHeight = card->getContentSize().height;
    auto* badge = mapDifficultyBadge(levelNumber(map.levelId), metadataOf(map), leftWidth - 16, 20, true, recordedMapDifficulty(map.difficulty));
    badge->setPosition({8, cardHeight - 30});
    card->addChild(badge, 5);
    auto chipX = 8.0f;
    auto const chip = [&](std::string const& label, ccColor3B accent) {
        auto const width = std::max(34.0f, 10.0f + static_cast<float>(label.size()) * 5.2f);
        auto* box = surface({width, 16}, {chipX + width / 2, cardHeight - 48}, accent, Ink, 245, false);
        card->addChild(box, 4);
        caption(box, label, {width / 2, 8}, 0.19f, width - 6, Text, FontWeight::ExtraBold);
        chipX += width + 4;
    };
    chip(fmt::format("POOL {}", map.pool), poolColor(map.pool));
    if (map.durationSeconds) chip(fmt::format(fmt::runtime(i18n::text("map-info-length", "Length {}")), mapPoolClock(map.durationSeconds)), Blue);
    if (!map.levelId.empty()) chip("ID " + map.levelId, Blue);
    caption(card, i18n::text("map-info-qualifying", "Qualifying progress"), {8, cardHeight - 70}, 0.21f, 110, TextMuted, FontWeight::SemiBold, 0);
    caption(card, mapPoolPercent(map.qualifyingPercent), {leftWidth - 8, cardHeight - 70}, 0.26f, 50, Text, FontWeight::Black, 1);
    auto* rule = drawNode(card);
    fillRect(rule, {8, cardHeight - 80, leftWidth - 16, 1}, Blue, 0.35f);

    caption(card, i18n::text("map-info-mine", "MY RECORD"), {8, cardHeight - 92}, 0.24f, 100, Cyan, FontWeight::Black, 0);
    auto const mineWidth = (leftWidth - 16 - 8) / 3;
    auto const mineTile = [&](int index, std::string const& value, std::string const& label, ccColor3B color) {
        auto* box = surface({mineWidth, 38}, {}, Cyan, Surface, 245, false);
        box->setAnchorPoint(CCPointZero);
        box->setPosition({8 + index * (mineWidth + 4), cardHeight - 140});
        card->addChild(box, 4);
        caption(box, value, {mineWidth / 2, 24}, 0.3f, mineWidth - 6, color, FontWeight::Black);
        caption(box, label, {mineWidth / 2, 9}, 0.17f, mineWidth - 6, TextMuted);
    };
    auto const myRounds = integer(mine["rounds"]);
    mineTile(0, mapPoolNumber(myRounds), i18n::text("map-info-mine-rounds", "Rounds"), Text);
    mineTile(1, fmt::format(fmt::runtime(i18n::text("map-info-mine-result", "{}W {}D {}L")),
        integer(mine["wins"]), integer(mine["draws"]), integer(mine["losses"])), i18n::text("map-info-mine-result-label", "Result"), Text);
    auto const best = number(mine["bestPercent"]);
    mineTile(2, mapPoolPercent(best), i18n::text("map-info-best", "Best progress"), best && *best >= 100 ? Green : Text);

    auto* viewMenu = CCMenu::create();
    viewMenu->setPosition(CCPointZero);
    card->addChild(viewMenu, 20);
    auto* view = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("history-view-level", "VIEW LEVEL"), {leftWidth - 16, 26}, Cyan, false, 0.28f),
        this, menu_selector(RankedMapPoolPopup::onViewLevel));
    view->setPosition({leftWidth / 2, 20});
    viewMenu->addChild(view);

    // Right column: headline rates, death distribution, tiers and records.
    auto const rightX = left + leftWidth + 10;
    auto const rightWidth = size.width - rightX - 12;
    auto const tileWidth = (rightWidth - 3 * 6) / 4;
    auto const headline = [&](int index, std::string const& label, std::string const& value, std::string const& detail, ccColor3B accent) {
        auto* box = surface({tileWidth, 40}, {}, accent, SurfaceRaised, 245, false);
        box->setAnchorPoint(CCPointZero);
        box->setPosition({rightX + index * (tileWidth + 6), size.height - 92});
        m_infoView->addChild(box, 2);
        caption(box, label, {6, 32}, 0.17f, tileWidth - 10, TextMuted, FontWeight::SemiBold, 0);
        caption(box, value, {6, 19}, 0.33f, tileWidth - 10, Text, FontWeight::Black, 0);
        caption(box, detail, {6, 7}, 0.15f, tileWidth - 10, TextMuted, FontWeight::SemiBold, 0);
    };
    auto const totalRounds = integer(data["totals"]["rounds"]);
    headline(0, i18n::text("map-info-share", "Played"), mapPoolPercent(map.share),
        fmt::format(fmt::runtime(i18n::text("map-info-share-detail", "{} / {} rounds")), mapPoolNumber(map.rounds), mapPoolNumber(totalRounds)), Cyan);
    headline(1, i18n::text("map-info-ban", "Banned"), mapPoolPercent(map.banRate),
        fmt::format(fmt::runtime(i18n::text("map-info-ban-detail", "{} bans · {} offers")), mapPoolNumber(map.bans), mapPoolNumber(map.banOffers)), Red);
    headline(2, i18n::text("map-info-draw", "Draws"), mapPoolPercent(map.drawRate),
        fmt::format(fmt::runtime(i18n::text("map-info-draw-detail", "{} Death Matches")), mapPoolNumber(map.draws)), Gold);
    headline(3, i18n::text("map-info-clear", "Clears"), mapPoolPercent(number(stats["clearRate"])),
        i18n::text("map-info-clear-detail", "Players who reached 100%"), Green);

    // Where players die: ten 10% bins, the deadliest one in gold.
    auto const middleTop = size.height - 98, middleBottom = 106.0f;
    auto const deathsWidth = rightWidth * 0.6f;
    auto* deaths = surface({deathsWidth, middleTop - middleBottom}, {}, Blue, SurfaceRaised, 245, false);
    deaths->setAnchorPoint(CCPointZero);
    deaths->setPosition({rightX, middleBottom});
    m_infoView->addChild(deaths, 2);
    auto const deathsHeight = deaths->getContentSize().height;
    caption(deaths, i18n::text("map-info-deaths", "WHERE PLAYERS DIE"), {7, deathsHeight - 9}, 0.22f, deathsWidth - 14, Text, FontWeight::Black, 0);
    std::vector<double> bins;
    if (auto list = stats["deathBins"].asArray(); list.isOk())
        for (auto const& value : list.unwrap()) if (bins.size() < 10) bins.push_back(std::max(0.0, number(value).value_or(0)));
    bins.resize(10, 0);
    auto const peak = mapPoolDeathPeak(bins);
    auto const range = [](int bin) { return fmt::format("{}-{}%", bin * 10, bin * 10 + 10); };
    caption(deaths, peak < 0 ? i18n::text("map-info-deaths-none", "No deaths recorded yet.")
        : fmt::format(fmt::runtime(i18n::text("map-info-deaths-peak", "Deadliest {} · {}")), range(peak), mapPoolPercent(bins[static_cast<std::size_t>(peak)])),
        {7, deathsHeight - 21}, 0.19f, deathsWidth - 14, peak < 0 ? TextMuted : Gold, FontWeight::ExtraBold, 0);
    auto* chart = drawNode(deaths);
    auto const plotLeft = 8.0f, plotBottom = 13.0f, plotWidth = deathsWidth - 16, plotHeight = deathsHeight - 42;
    auto const highest = peak < 0 ? 1.0 : bins[static_cast<std::size_t>(peak)];
    auto const barWidth = (plotWidth - 9 * 3) / 10;
    for (int bin = 0; bin < 10; ++bin) {
        auto const value = bins[static_cast<std::size_t>(bin)];
        auto const height = value > 0 ? std::max(2.0f, static_cast<float>(value / highest) * plotHeight) : 0.0f;
        fillRect(chart, {plotLeft + bin * (barWidth + 3), plotBottom, barWidth, height}, bin == peak ? Gold : Cyan, bin == peak ? 1.0f : 0.72f);
    }
    fillRect(chart, {plotLeft, plotBottom - 1, plotWidth, 1}, TextMuted, 0.5f);
    caption(deaths, "0%", {plotLeft, 6}, 0.15f, 24, TextMuted, FontWeight::SemiBold, 0);
    caption(deaths, "50%", {plotLeft + plotWidth / 2, 6}, 0.15f, 24, TextMuted);
    caption(deaths, "100%", {plotLeft + plotWidth, 6}, 0.15f, 30, TextMuted, FontWeight::SemiBold, 1);

    // Played by tier: share of this map's rounds per match tier.
    auto const tiersX = rightX + deathsWidth + 6, tiersWidth = rightWidth - deathsWidth - 6;
    auto* tiers = surface({tiersWidth, middleTop - middleBottom}, {}, Blue, SurfaceRaised, 245, false);
    tiers->setAnchorPoint(CCPointZero);
    tiers->setPosition({tiersX, middleBottom});
    m_infoView->addChild(tiers, 2);
    caption(tiers, i18n::text("map-info-tiers", "PLAYED BY TIER"), {7, deathsHeight - 9}, 0.22f, tiersWidth - 14, Text, FontWeight::Black, 0);
    auto* tierBars = drawNode(tiers);
    auto tierY = deathsHeight - 24;
    if (auto list = stats["tiers"].asArray(); list.isOk() && !list.unwrap().empty()) {
        for (auto const& entry : list.unwrap()) {
            if (tierY < 8) break;
            auto const tier = text(entry["tier"], 16);
            auto const share = number(entry["share"]).value_or(0);
            auto const color = tierColor(tier);
            caption(tiers, rankTierDisplay(tier), {7, tierY}, 0.18f, tiersWidth * 0.5f, color, FontWeight::Black, 0);
            caption(tiers, mapPoolPercent(share), {tiersWidth - 7, tierY}, 0.18f, 40, Text, FontWeight::ExtraBold, 1);
            fillRect(tierBars, {7, tierY - 9, tiersWidth - 14, 4}, Ink);
            fillRect(tierBars, {7, tierY - 9, static_cast<float>(std::clamp(share, 0.0, 100.0) / 100.0) * (tiersWidth - 14), 4}, color);
            tierY -= 17;
        }
    } else caption(tiers, "-", {tiersWidth / 2, deathsHeight / 2 - 6}, 0.26f, 30, TextMuted);

    // Records: six small tiles in two rows.
    auto const factWidth = (rightWidth - 2 * 6) / 3;
    auto const fact = [&](int index, std::string const& label, std::string const& value, std::string const& detail) {
        auto* box = surface({factWidth, 42}, {}, Gold, SurfaceRaised, 245, false);
        box->setAnchorPoint(CCPointZero);
        box->setPosition({rightX + (index % 3) * (factWidth + 6), 56.0f - (index / 3) * 46.0f});
        m_infoView->addChild(box, 2);
        caption(box, label, {6, 34}, 0.16f, factWidth - 10, TextMuted, FontWeight::SemiBold, 0);
        caption(box, value, {6, 21}, 0.29f, factWidth - 10, Text, FontWeight::Black, 0);
        caption(box, detail, {6, 8}, 0.15f, factWidth - 10, TextMuted, FontWeight::SemiBold, 0);
    };
    auto const& comeback = stats["comeback"];
    auto const firstClears = integer(stats["firstAttemptClears"]);
    fact(0, i18n::text("map-info-average-best", "Best progress per round"), mapPoolPercent(number(stats["averageBestPercent"])), "");
    fact(1, i18n::text("map-info-average-attempts", "Attempts per round"), mapPoolDecimal(number(stats["averageAttempts"])), "");
    fact(2, i18n::text("map-info-first-clear", "First-attempt clears"), mapPoolPercent(number(stats["firstAttemptClearRate"])),
        firstClears > 0 ? mapPoolNumber(firstClears) : "");
    fact(3, i18n::text("map-info-comeback", "Comebacks (round / DM)"),
        mapPoolPercent(number(comeback["normal"]["rate"])) + " / " + mapPoolPercent(number(comeback["deathmatch"]["rate"])),
        i18n::text("map-info-comeback-detail", "50 pts / 10% behind"));
    auto const& fastest = stats["fastestClear"];
    fact(4, i18n::text("map-info-fastest", "Fastest clear"), fastest.isObject() ? text(fastest["player"], 32) : "-",
        fastest.isObject() ? fmt::format(fmt::runtime(i18n::text("map-info-fastest-detail", "attempt {}")), integer(fastest["attempt"])) : "");
    auto const& longest = stats["longestDeathmatch"];
    fact(5, i18n::text("map-info-longest", "Longest Death Match"), longest.isObject() ? mapPoolClock(number(longest["seconds"])) : "-",
        longest.isObject() ? fmt::format(fmt::runtime(i18n::text("map-info-longest-detail", "{} won")), text(longest["winner"], 32)) : "");
}

void RankedMapPoolPopup::update(float) {
    if (m_closed || !m_hasList || !m_updated) return;
    auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_receivedAt).count();
    auto const minutes = mapPoolUpdatedMinutes(elapsed);
    if (minutes == m_shownMinutes) return;
    m_shownMinutes = minutes;
    m_updated->setString((minutes == 0 ? i18n::text("map-pool-updated-now", "Updated just now")
        : fmt::format(fmt::runtime(i18n::text("map-pool-updated-minutes", "Updated {} min ago")), minutes)).c_str());
    fitMenuLabelWidth(m_updated, 84, 0.19f, 0.12f);
}

void RankedMapPoolPopup::onClose(CCObject*) { dismiss(); }

void RankedMapPoolPopup::dismiss() {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedMapPoolPopup> keepAlive(this);
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    unscheduleUpdate();
    Popup::onClose(nullptr);
}

void RankedMapPoolPopup::onExit() {
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    unscheduleUpdate();
    if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
    Popup::onExit();
}

} // namespace corum::ranked::ui
