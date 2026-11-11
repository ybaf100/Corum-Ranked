#include "RankedMapPoolView.hpp"

#include "../HistoryLevelLookup.hpp"
#include "../RankedMapDetails.hpp"
#include "MapDifficultyBadge.hpp"
#include "MapPoolVisuals.hpp"
#include "RankedHistoryFilterPopup.hpp"
#include "RankedMapInfoPopup.hpp"
#include "RankedTapScroll.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <Geode/cocos/misc_nodes/CCClippingNode.h>

using namespace geode::prelude;
namespace corum::ranked::ui {
using namespace mappool;

namespace {
constexpr float kRowHeight = 30, kRowGap = 4;
constexpr float kTabWidth = 46, kTabGap = 4, kTabHeight = 24;
constexpr std::size_t kMaxMaps = 200;

// Row columns from the right edge. The map column holds two lines: the
// title over the difficulty badge and creator, so the title is never cut short.
struct Columns {
    float rank = 12, pool = 31, title = 46, titleWidth = 0, difficultyWidth = 70;
    float barLeft = 0, barWidth = 30, share = 0, draw = 0, ban = 0, rounds = 0, info = 0, view = 0;
    explicit Columns(float width) {
        view = width - 28; info = view - 52; rounds = info - 44; ban = rounds - 36; draw = ban - 38; share = draw - 36;
        barLeft = share - 16 - 3 - barWidth;
        titleWidth = std::max(40.0f, barLeft - 8 - title);
    }
};

// Shrink to a floor, then cut with "..." so a long title never runs into the
// next column.
void fitTitle(CCLabelBMFont* label, std::string const& value, float width, float scale) {
    fitMenuLabelWidth(label, width, scale, scale * 0.7f);
    if (label->getScaledContentSize().width <= width) return;
    std::size_t characters = 0;
    for (std::size_t bytes = 0; bytes < value.size(); bytes = mapPoolUtf8Prefix(value, ++characters)) {}
    while (characters > 1 && label->getScaledContentSize().width > width) {
        --characters;
        label->setString((value.substr(0, mapPoolUtf8Prefix(value, characters)) + "...").c_str());
    }
}

bool visibleChain(CCNode* node) {
    for (; node; node = node->getParent()) if (!node->isVisible()) return false;
    return true;
}
} // namespace

// Pool filter tabs in a clipped strip about five and a half tabs wide. A drag
// scrolls it sideways; an unmoved tap selects a tab.
class MapPoolTabStrip final : public CCLayer {
public:
    static MapPoolTabStrip* create(float width, std::function<void(int)> tap) {
        auto* strip = new MapPoolTabStrip;
        if (strip->init(width, std::move(tap))) { strip->autorelease(); return strip; }
        delete strip;
        return nullptr;
    }
    void setSelected(int pool) {
        for (auto* tab : m_tabs) tab->removeFromParentAndCleanup(true);
        m_tabs.clear();
        for (int index = 0; index < kMapPoolTabs; ++index) {
            auto const selected = index == pool;
            auto* face = buttonFace(index == 0 ? i18n::text("map-pool-all", "ALL") : fmt::format("P{}", index),
                {kTabWidth, kTabHeight}, index == 0 ? Steel : poolColor(index), selected, 0.24f, index == 0 ? Text : poolColor(index));
            face->setAnchorPoint(CCPointZero);
            face->setPosition({index * (kTabWidth + kTabGap), 0});
            face->setTag(index);
            m_content->addChild(face);
            m_tabs.push_back(face);
        }
    }
    void registerWithTouchDispatcher() override {
        // Above the lobby menus (-128) inside the strip only; it swallows its own touches.
        CCTouchDispatcher::get()->addTargetedDelegate(this, -129, true);
    }
    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (m_touch != -1 || !visibleChain(this)) return false;
        auto const local = convertToNodeSpace(touch->getLocation());
        if (local.x < 0 || local.y < 0 || local.x > m_width || local.y > kTabHeight) return false;
        m_touch = touch->getID();
        m_start = touch->getLocation();
        m_startOffset = m_offset;
        m_moved = false;
        return true;
    }
    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (touch->getID() != m_touch) return;
        auto const dx = touch->getLocation().x - m_start.x;
        if (std::abs(dx) > 6) m_moved = true;
        if (!m_moved) return;
        m_offset = mapPoolTabScroll(m_startOffset + dx, m_contentWidth, m_width);
        m_content->setPositionX(m_offset);
    }
    void ccTouchEnded(CCTouch* touch, CCEvent*) override {
        if (touch->getID() != m_touch) return;
        m_touch = -1;
        if (m_moved || !m_tap) return;
        auto const at = m_content->convertToNodeSpace(touch->getLocation());
        for (auto* tab : m_tabs) {
            auto const position = tab->getPosition();
            if (at.x >= position.x && at.x <= position.x + kTabWidth && at.y >= 0 && at.y <= kTabHeight) {
                auto const pool = tab->getTag();
                auto tap = m_tap;
                queueInMainThread([tap, pool] { tap(pool); });
                break;
            }
        }
    }
    void ccTouchCancelled(CCTouch* touch, CCEvent*) override { if (touch->getID() == m_touch) m_touch = -1; }

private:
    bool init(float width, std::function<void(int)> tap) {
        if (!CCLayer::init()) return false;
        m_width = width;
        m_tap = std::move(tap);
        m_contentWidth = kMapPoolTabs * kTabWidth + (kMapPoolTabs - 1) * kTabGap;
        setContentSize({width, kTabHeight});
        ignoreAnchorPointForPosition(false);
        setAnchorPoint(CCPointZero);
        auto* mask = CCDrawNode::create();
        CCPoint points[4] = {CCPoint{0, -4}, CCPoint{width, -4}, CCPoint{width, kTabHeight + 4}, CCPoint{0, kTabHeight + 4}};
        mask->drawPolygon(points, 4, {1, 1, 1, 1}, 0, {1, 1, 1, 1});
        auto* clip = CCClippingNode::create(mask);
        clip->setContentSize({width, kTabHeight});
        addChild(clip);
        m_content = CCNode::create();
        m_content->setContentSize({m_contentWidth, kTabHeight});
        clip->addChild(m_content);
        setTouchEnabled(true);
        return true;
    }
    CCNode* m_content = nullptr;
    std::vector<CCNode*> m_tabs;
    std::function<void(int)> m_tap;
    float m_width = 0, m_contentWidth = 0, m_offset = 0, m_startOffset = 0;
    CCPoint m_start{};
    int m_touch = -1;
    bool m_moved = false;
};

RankedMapPoolView* RankedMapPoolView::create(CCSize screen) {
    auto* view = new RankedMapPoolView;
    if (view->init(screen)) { view->autorelease(); return view; }
    delete view;
    return nullptr;
}

bool RankedMapPoolView::init(CCSize screen) {
    if (!CCNode::init()) return false;
    setID("ranked-map-pool-view"_spr);
    m_screen = screen;
    setContentSize(screen);
    m_width = std::min(760.0f, screen.width - 48);
    m_left = (screen.width - m_width) / 2;
    auto const top = screen.height;

    m_summary = CCNode::create();
    m_summary->setContentSize({m_width, 24});
    m_summary->setPosition({m_left, top - 90});
    addChild(m_summary, 5);

    m_menu = CCMenu::create();
    m_menu->setPosition(CCPointZero);
    addChild(m_menu, 20);

    // Pool filter strip (left) and the order picker (right).
    auto const controlsY = top - 107;
    m_tabs = MapPoolTabStrip::create(mapPoolTabStripWidth(kTabWidth, kTabGap), [weak = WeakRef(this)](int pool) {
        if (auto self = weak.lock(); self && !self->m_closed) self->selectPool(pool);
    });
    m_tabs->setPosition({m_left, controlsY - kTabHeight / 2});
    m_tabs->setSelected(m_pool);
    addChild(m_tabs, 10);
    // ORDER: <current> opens the picker; the toggle beside it flips the direction.
    auto const free = m_width - m_tabs->getContentSize().width - 12;
    auto const directionWidth = std::min(84.0f, free * 0.38f), sortWidth = std::min(170.0f, free - directionWidth - 6);
    m_sortButton = CCMenuItemSpriteExtra::create(CCNode::create(), this, menu_selector(RankedMapPoolView::onSort));
    m_sortButton->setContentSize({sortWidth, kTabHeight});
    m_sortButton->setPosition({m_left + m_width - directionWidth - 6 - sortWidth / 2, controlsY});
    m_menu->addChild(m_sortButton);
    m_directionButton = CCMenuItemSpriteExtra::create(CCNode::create(), this, menu_selector(RankedMapPoolView::onDirection));
    m_directionButton->setContentSize({directionWidth, kTabHeight});
    m_directionButton->setPosition({m_left + m_width - directionWidth / 2, controlsY});
    m_menu->addChild(m_directionButton);

    auto const viewport = CCSize{m_width, std::max(60.0f, top - 178)};
    Columns const columns(viewport.width - 8);
    auto const headerY = top - 127;
    auto header = [&](std::string const& label, float x, float width, float anchor) {
        m_headers.push_back(caption(this, label, {m_left + 4 + x, headerY}, 0.22f, width, TextMuted, FontWeight::ExtraBold, anchor));
    };
    header("#", columns.rank, 20, 0.5f);
    // The map column doubles as the difficulty column (badge under the title).
    header(i18n::text("map-pool-col-map", "MAP / DIFFICULTY"), columns.title, columns.titleWidth, 0);
    header(sortLabel(MapPoolSort::Share), columns.barLeft, 80, 0);
    header(sortLabel(MapPoolSort::Draw), columns.draw, 36, 0.5f);
    header(sortLabel(MapPoolSort::Ban), columns.ban, 32, 0.5f);
    header(sortLabel(MapPoolSort::Rounds), columns.rounds, 40, 0.5f);

    m_scroll = createRankedTapScroll(viewport, [weak = WeakRef(this)](CCPoint start, CCPoint end) {
        auto self = weak.lock();
        if (!self || self->m_closed) return;
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
                auto owner = weak.lock();
                if (!owner || owner->m_closed || generation != owner->m_generation) return;
                if (view) owner->viewLevel(index); else owner->openInfo(index);
            });
            break;
        }
    });
    m_scroll->setPosition({m_left, 43});
    m_scroll->setID("ranked-map-pool-scroll"_spr);
    addChild(m_scroll, 5);

    m_caption = caption(this, "", {m_left, 22}, 0.24f, m_width * 0.36f, TextMuted, FontWeight::SemiBold, 0);
    m_updated = caption(this, "", {m_left + m_width, 22}, 0.22f, m_width * 0.30f, TextMuted, FontWeight::SemiBold, 1);
    m_refresh = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("leaderboard-refresh", "REFRESH"), {96, 26}, Accent, false),
        this, menu_selector(RankedMapPoolView::onRefresh));
    m_refresh->setPosition({screen.width / 2, 22});
    m_menu->addChild(m_refresh);

    updateControls();
    scheduleUpdate();
    fetch();
    return true;
}

void RankedMapPoolView::updateControls() {
    if (m_closed) return;
    m_sortButton->setNormalImage(buttonFace(fmt::format(fmt::runtime(i18n::text("map-pool-order-button", "ORDER: {}")), sortLabel(m_sort)),
        m_sortButton->getContentSize(), Steel, false, 0.25f));
    m_directionButton->setNormalImage(buttonFace(m_ascending ? i18n::text("map-pool-ascending", "ASCENDING") : i18n::text("map-pool-descending", "DESCENDING"),
        m_directionButton->getContentSize(), Accent, false, 0.23f, Accent));
    // Column captions: the active order is gold (index 1.. follow kMapPoolSorts).
    for (std::size_t index = 0; index < kMapPoolSorts.size() && index + 1 < m_headers.size(); ++index)
        m_headers[index + 1]->setColor(kMapPoolSorts[index] == m_sort ? Gold : TextMuted);
    m_refresh->setEnabled(!m_busy);
    m_refresh->setCascadeOpacityEnabled(true);
    m_refresh->setOpacity(m_busy ? 110 : 255);
}

void RankedMapPoolView::fetch() {
    if (m_closed || m_busy) return;
    m_busy = true;
    updateControls();
    if (!m_hasList) {
        m_caption->setString(i18n::text("map-pool-loading", "Loading map pool...").c_str());
        m_caption->setColor(TextMuted);
    }
    auto const generation = ++m_generation;
    auto pending = request();
    m_request.spawn(pending.get(RankedRuntime::get().mapPoolUrl()), [weak = WeakRef(this), generation](web::WebResponse response) {
        queueInMainThread([weak, generation, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            auto const maps = data["maps"].asArray();
            if (!response.ok() || !maps.isOk() || maps.unwrap().size() > kMaxMaps) {
                self->showError();
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
                self->m_caption->setString(fmt::format(fmt::runtime(i18n::text("map-pool-summary", "{} active maps · {} rounds")),
                    self->m_rows.size(), mapPoolNumber(self->m_totalRounds)).c_str());
                fitMenuLabelWidth(self->m_caption, self->m_width * 0.36f, 0.24f, 0.13f);
                self->m_caption->setColor(TextMuted);
                self->applyNativeDifficulties();
                self->updateSummary();
                self->rebuildRows();
            }
            self->updateControls();
        });
    });
}

void RankedMapPoolView::showError() {
    m_caption->setString(i18n::text("map-pool-error", "Could not load the map pool. Press Refresh.").c_str());
    fitMenuLabelWidth(m_caption, m_width * 0.36f, 0.24f, 0.12f);
    m_caption->setColor(Red);
}

void RankedMapPoolView::updateSummary() {
    m_summary->removeAllChildrenWithCleanup(true);
    auto const size = m_summary->getContentSize();
    auto const width = (size.width - 3 * 6) / 4;
    auto const titleOf = [&](std::string const& id) {
        auto row = std::find_if(m_rows.begin(), m_rows.end(), [&](auto const& entry) { return entry.canonicalLevelId == id; });
        return row == m_rows.end() ? i18n::text("map-pool-none", "None yet") : row->title;
    };
    auto const tile = [&](int index, std::string const& label, std::string const& value, ccColor3B accent) {
        // H5 summary (.panel): the caption over the 24 px value; the accent
        // marks the top edge.
        auto* box = CCLayerColor::create({Surface.r, Surface.g, Surface.b, 255}, width, size.height);
        box->setPosition({index * (width + 6), 0});
        m_summary->addChild(box);
        auto* edge = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, width, 1.0f);
        edge->setPosition({0, size.height - 1}); box->addChild(edge, 1);
        caption(box, label, {7, size.height * 0.72f}, 13.f / 64.f, width - 14, TextMuted, FontWeight::SemiBold, 0);
        caption(box, value, {7, size.height * 0.30f}, 24.f / 64.f, width - 14, Text, FontWeight::Black, 0);
    };
    tile(0, i18n::text("map-pool-total-rounds", "Rounds played"), mapPoolNumber(m_totalRounds), Steel);
    tile(1, i18n::text("map-pool-total-draws", "Draw rate"), mapPoolPercent(m_totalDrawRate), Steel);
    tile(2, i18n::text("map-pool-most-played", "Most played"), titleOf(m_mostPlayed), Gold);
    tile(3, i18n::text("map-pool-most-banned", "Most banned"), titleOf(m_mostBanned), Red);
}

void RankedMapPoolView::rebuildRows(bool keepOffset) {
    if (!m_scroll) return;
    auto const offset = m_scroll->m_contentLayer->getPositionY();
    auto const previousHeight = m_scroll->m_contentLayer->getContentSize().height;
    m_hits.clear();
    m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
    auto const viewport = m_scroll->getContentSize();
    auto const width = viewport.width - 8;
    Columns const columns(width);
    auto const order = mapPoolOrder(m_rows, m_pool, m_sort, m_ascending);
    auto const height = std::max(viewport.height, static_cast<float>(order.size()) * (kRowHeight + kRowGap) + 8);
    m_scroll->m_contentLayer->setContentSize({viewport.width, height});
    double maxShare = 0;
    for (auto const& row : m_rows) maxShare = std::max(maxShare, row.share.value_or(0));
    if (order.empty() && m_hasList)
        caption(m_scroll->m_contentLayer, i18n::text("map-pool-empty", "No maps in the pool."),
            {viewport.width / 2, height - 24}, 0.28f, viewport.width - 20, TextMuted);
    for (std::size_t position = 0; position < order.size(); ++position) {
        auto const index = order[position];
        auto const& map = m_rows[index];
        // H5 row (.tr): a flat surface with the pool colour on the left edge.
        auto* row = CCLayerColor::create({Surface.r, Surface.g, Surface.b, 255}, width, kRowHeight);
        auto const tone = poolColor(map.pool);
        auto* edge = CCLayerColor::create({tone.r, tone.g, tone.b, 255}, 2.0f, kRowHeight); row->addChild(edge, 1);
        row->setPosition({4, height - 4 - (position + 1) * (kRowHeight + kRowGap)});
        row->setID("ranked-map-pool-row"_spr);
        m_scroll->m_contentLayer->addChild(row, 1);
        auto const ranked = m_sort != MapPoolSort::Difficulty && !m_ascending && position < 3;
        caption(row, std::to_string(position + 1), {columns.rank, kRowHeight / 2}, 0.26f, 20, ranked ? Gold : TextMuted, FontWeight::Black);
        auto* chip = surface({22, 15}, {columns.pool, kRowHeight / 2}, poolColor(map.pool), Ink, 250, false);
        row->addChild(chip, 4);
        caption(chip, fmt::format("P{}", map.pool), {11, 7.5f}, 0.2f, 18, poolColor(map.pool), FontWeight::Black);
        auto* title = caption(row, map.title, {columns.title, 21}, 0.29f, columns.titleWidth, Text, FontWeight::Black, 0);
        fitTitle(title, map.title, columns.titleWidth, 0.29f);
        auto* badge = mapDifficultyBadge(levelNumber(map.levelId), metadataOf(map), columns.difficultyWidth, 12, true, displayedDifficulty(map));
        badge->setPosition({columns.title, 2});
        row->addChild(badge, 5);
        auto const creatorX = columns.title + columns.difficultyWidth + 6;
        auto* creator = caption(row, map.creator.empty() ? "-" : "by " + map.creator, {creatorX, 8}, 0.18f,
            std::max(20.0f, columns.title + columns.titleWidth - creatorX), TextMuted, FontWeight::SemiBold, 0);
        fitTitle(creator, map.creator.empty() ? "-" : "by " + map.creator, std::max(20.0f, columns.title + columns.titleWidth - creatorX), 0.18f);
        bar(row, {columns.barLeft, 11, columns.barWidth, 8}, Ink);
        auto const fill = maxShare > 0 ? static_cast<float>(map.share.value_or(0) / maxShare) * columns.barWidth : 0.0f;
        if (fill > 0) bar(row, {columns.barLeft, 11, fill, 8}, Accent, 255, 5);
        caption(row, mapPoolPercent(map.share), {columns.share, kRowHeight / 2}, 0.22f, 32, Text, FontWeight::ExtraBold);
        caption(row, mapPoolPercent(map.drawRate), {columns.draw, kRowHeight / 2}, 0.22f, 36, Text, FontWeight::ExtraBold);
        caption(row, mapPoolPercent(map.banRate), {columns.ban, kRowHeight / 2}, 0.22f, 32, Text, FontWeight::ExtraBold);
        caption(row, mapPoolNumber(map.rounds), {columns.rounds, kRowHeight / 2}, 0.22f, 40, TextMuted, FontWeight::ExtraBold);
        auto* info = buttonFace(i18n::text("map-pool-info", "INFO"), {48, 22}, Gold, false, 0.21f, Gold);
        info->setPosition({columns.info, kRowHeight / 2});
        row->addChild(info, 6);
        auto* view = buttonFace(i18n::text("history-view-level", "VIEW LEVEL"), {48, 22}, Accent, false, 0.21f, Accent);
        view->setPosition({columns.view, kRowHeight / 2});
        row->addChild(view, 6);
        m_hits.push_back({row, info, view, index});
    }
    m_scroll->scrollToTop();
    if (keepOffset && previousHeight == height) m_scroll->m_contentLayer->setPositionY(offset);
}

void RankedMapPoolView::selectPool(int pool) {
    pool = std::clamp(pool, 0, kMapPoolTabs - 1);
    if (pool == m_pool) return;
    m_pool = pool;
    m_tabs->setSelected(m_pool);
    rebuildRows();
}

void RankedMapPoolView::onSort(CCObject*) {
    if (m_closed) return;
    if (m_order) if (auto popup = m_order->lock(); popup && popup->getParent()) return;
    std::vector<RankedHistoryFilterChoice> choices;
    for (auto const sort : kMapPoolSorts) choices.push_back({sortKey(sort), sortLabel(sort)});
    auto* popup = RankedHistoryFilterPopup::create(std::move(choices), sortKey(m_sort), [weak = WeakRef(this)](std::string selected) {
        auto self = weak.lock();
        if (!self || self->m_closed) return;
        self->m_order.reset();
        for (auto const sort : kMapPoolSorts) {
            if (selected != sortKey(sort) || sort == self->m_sort) continue;
            self->m_sort = sort;
            self->m_ascending = mapPoolDefaultAscending(sort);
            self->updateControls();
            self->rebuildRows();
        }
    }, i18n::text("map-pool-order", "ORDER"));
    if (popup) { m_order.emplace(popup); popup->show(); }
}

void RankedMapPoolView::onRefresh(CCObject*) { fetch(); }

void RankedMapPoolView::onDirection(CCObject*) {
    if (m_closed) return;
    m_ascending = !m_ascending;
    updateControls();
    rebuildRows();
}

std::vector<int> RankedMapPoolView::unresolvedLevels() const {
    std::vector<int> ids;
    for (auto const& row : m_rows)
        if (row.gdDifficulty.empty() && ids.size() < 16)
            if (auto const id = levelNumber(row.levelId); id > 0) ids.push_back(id);
    return ids;
}

// The server could not read some levels from GD (custom non-demon levels):
// fill their face and difficulty order from the game's own GD lookup.
bool RankedMapPoolView::applyNativeDifficulties() {
    auto changed = false;
    for (auto& row : m_rows) {
        if (!row.gdDifficulty.empty()) continue;
        auto const native = RankedMapDetails::get().difficulty(levelNumber(row.levelId));
        if (native.label == "N/A") continue;
        row.gdDifficulty = std::string(native.label);
        row.difficultyRank = mapPoolRankFromNative(native.nativeCode);
        changed = true;
    }
    return changed;
}

void RankedMapPoolView::openInfo(std::size_t index) {
    if (m_closed || index >= m_rows.size()) return;
    if (m_info) if (auto popup = m_info->lock(); popup && popup->getParent()) return;
    if (auto* popup = RankedMapInfoPopup::create(m_rows[index])) { m_info.emplace(popup); popup->show(); }
}

void RankedMapPoolView::viewLevel(std::size_t index) {
    if (m_closed || index >= m_rows.size()) return;
    // Same flow as the history VIEW LEVEL button: busy check, lookup popup,
    // then the native level scene; back returns to this page.
    showHistoryLevel(levelNumber(m_rows[index].levelId));
}

void RankedMapPoolView::update(float) {
    if (m_closed || !m_hasList || !m_updated) return;
    if (auto const revision = RankedMapDetails::get().revision(); revision != m_detailsRevision) {
        m_detailsRevision = revision;
        if (applyNativeDifficulties()) rebuildRows(true);
    }
    auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_receivedAt).count();
    auto const minutes = mapPoolUpdatedMinutes(elapsed);
    if (minutes == m_shownMinutes) return;
    m_shownMinutes = minutes;
    m_updated->setString((minutes == 0 ? i18n::text("map-pool-updated-now", "Updated just now")
        : fmt::format(fmt::runtime(i18n::text("map-pool-updated-minutes", "Updated {} min ago")), minutes)).c_str());
    fitMenuLabelWidth(m_updated, m_width * 0.30f, 0.22f, 0.12f);
}

void RankedMapPoolView::closePopups() {
    if (m_info) if (auto popup = m_info->lock()) popup->dismiss();
    m_info.reset();
    if (m_order) if (auto popup = m_order->lock()) popup->dismiss();
    m_order.reset();
}

void RankedMapPoolView::cleanup() {
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    closePopups();
    CCNode::cleanup();
}

} // namespace corum::ranked::ui
