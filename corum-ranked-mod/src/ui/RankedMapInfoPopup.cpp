#include "RankedMapInfoPopup.hpp"

#include "../HistoryLevelLookup.hpp"
#include "../domain/RankTierPresentation.hpp"
#include "MapDifficultyBadge.hpp"
#include "MapPoolVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

using namespace geode::prelude;
namespace corum::ranked::ui {
using namespace mappool;
namespace {
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
} // namespace

RankedMapInfoPopup* RankedMapInfoPopup::create(MapPoolRow row) {
    auto* popup = new RankedMapInfoPopup;
    popup->m_row = std::move(row);
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool RankedMapInfoPopup::init() {
    auto const window = CCDirector::sharedDirector()->getWinSize();
    auto const size = CCSize{std::min(560.0f, window.width - 24), std::min(294.0f, window.height - 22)};
    if (!Popup::init(size.width, size.height)) return false;
    setID("ranked-map-info-popup"_spr);
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_panel = surface(size, {size.width / 2, size.height / 2}, poolColor(m_row.pool), Surface, 255, true);
    m_mainLayer->addChild(m_panel, 10);
    showMessage(i18n::text("map-info-loading", "Loading map statistics..."), TextMuted);
    fetch();
    return true;
}

void RankedMapInfoPopup::addChrome() {
    auto const size = m_content->getContentSize();
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    m_content->addChild(menu, 20);
    auto* close = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("close", "CLOSE"), {64, 22}, Steel, false),
        this, menu_selector(RankedMapInfoPopup::onClose));
    close->setPosition({44, size.height - 20});
    menu->addChild(close);
    auto* refresh = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("leaderboard-refresh", "REFRESH"), {72, 22}, Accent, false),
        this, menu_selector(RankedMapInfoPopup::onRefresh));
    refresh->setPosition({size.width - 44, size.height - 20});
    menu->addChild(refresh);
    caption(m_content, m_row.title, {size.width / 2, size.height - 16}, 0.46f, size.width - 200, Gold, FontWeight::Black);
}

void RankedMapInfoPopup::showMessage(std::string const& message, ccColor3B color) {
    if (m_content) m_content->removeFromParentAndCleanup(true);
    m_content = CCNode::create();
    m_content->setContentSize(m_panel->getContentSize());
    m_panel->addChild(m_content, 5);
    addChrome();
    auto const size = m_content->getContentSize();
    caption(m_content, message, {size.width / 2, size.height / 2}, 0.28f, size.width - 60, color);
}

void RankedMapInfoPopup::fetch() {
    if (m_closed || m_busy) return;
    m_busy = true;
    auto const generation = ++m_generation;
    auto const id = m_row.canonicalLevelId;
    auto pending = request();
    m_request.spawn(pending.get(RankedRuntime::get().mapPoolUrl(id)), [weak = WeakRef(this), generation, id](web::WebResponse response) {
        queueInMainThread([weak, generation, id, response = std::move(response)] {
            auto self = weak.lock();
            if (!self || self->m_closed || generation != self->m_generation) return;
            self->m_busy = false;
            auto const data = response.json().unwrapOr(matjson::Value());
            if (!response.ok() || !data["stats"].isObject() || text(data["map"]["canonicalLevelId"], 16) != id)
                self->showMessage(i18n::text("map-info-error", "Could not load this map. Press Refresh."), Red);
            else self->showDetail(data);
        });
    });
}

void RankedMapInfoPopup::onRefresh(CCObject*) { fetch(); }

void RankedMapInfoPopup::onViewLevel(CCObject*) {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedMapInfoPopup> keepAlive(this);
    auto const level = levelNumber(m_row.levelId);
    // Close first when the level can open: a pushed native scene would
    // otherwise leave this popup behind with its exit already run.
    if (canOpenHistoryLevel() && level > 0) dismiss();
    showHistoryLevel(level);
}

void RankedMapInfoPopup::showDetail(matjson::Value const& data) {
    m_row = parseRow(data["map"]);
    if (m_content) m_content->removeFromParentAndCleanup(true);
    m_content = CCNode::create();
    m_content->setContentSize(m_panel->getContentSize());
    m_panel->addChild(m_content, 5);
    addChrome();
    auto const size = m_content->getContentSize();
    auto const& map = m_row;
    auto const& stats = data["stats"];
    auto const& mine = data["mine"];

    auto const rank = optionalInteger(data["shareRank"]);
    std::string subtitle = map.creator.empty() ? std::string() : "by " + map.creator + "  ·  ";
    if (!map.levelId.empty()) subtitle += "ID " + map.levelId + "  ·  ";
    subtitle += rank ? fmt::format(fmt::runtime(i18n::text("map-info-rank", "#{} of {} by plays")), *rank, integer(data["mapCount"]))
        : i18n::text("map-info-unplayed", "No rounds on this map yet.");
    caption(m_content, subtitle, {size.width / 2, size.height - 34}, 0.21f, size.width - 190, TextMuted);

    // Left column: the map itself, the viewer's record and VIEW LEVEL.
    constexpr float left = 12, leftWidth = 176;
    auto* card = surface({leftWidth, size.height - 54}, {}, poolColor(map.pool), SurfaceRaised, 245, false);
    card->setAnchorPoint(CCPointZero);
    card->setPosition({left, 10});
    m_content->addChild(card, 2);
    auto const cardHeight = card->getContentSize().height;
    auto* badge = mapDifficultyBadge(levelNumber(map.levelId), metadataOf(map), leftWidth - 16, 20, true, displayedDifficulty(map));
    badge->setPosition({8, cardHeight - 30});
    card->addChild(badge, 5);
    // Two fixed chips side by side; each label shrinks to its chip.
    auto const chipWidth = (leftWidth - 16 - 6) / 2;
    auto const chip = [&](int index, std::string const& label, ccColor3B accent) {
        auto* box = surface({chipWidth, 16}, {8 + chipWidth / 2 + index * (chipWidth + 6), cardHeight - 48}, accent, Ink, 245, false);
        card->addChild(box, 4);
        caption(box, label, {chipWidth / 2, 8}, 0.19f, chipWidth - 8, Text, FontWeight::ExtraBold);
    };
    chip(0, fmt::format("POOL {}", map.pool), poolColor(map.pool));
    chip(1, map.durationSeconds ? fmt::format(fmt::runtime(i18n::text("map-info-length", "Length {}")), mapPoolClock(map.durationSeconds)) : "-", Steel);
    caption(card, i18n::text("map-info-qualifying", "Qualifying progress"), {8, cardHeight - 70}, 0.21f, 110, TextMuted, FontWeight::SemiBold, 0);
    caption(card, mapPoolPercent(map.qualifyingPercent), {leftWidth - 8, cardHeight - 70}, 0.26f, 50, Text, FontWeight::Black, 1);
    auto* rule = drawNode(card);
    fillRect(rule, {8, cardHeight - 80, leftWidth - 16, 1}, Steel, 0.35f);

    caption(card, i18n::text("map-info-mine", "MY RECORD"), {8, cardHeight - 92}, 0.24f, 100, Accent, FontWeight::Black, 0);
    auto const mineWidth = (leftWidth - 16 - 8) / 3;
    auto const mineTile = [&](int index, std::string const& value, std::string const& label, ccColor3B color) {
        auto* box = surface({mineWidth, 38}, {}, Accent, Surface, 245, false);
        box->setAnchorPoint(CCPointZero);
        box->setPosition({8 + index * (mineWidth + 4), cardHeight - 140});
        card->addChild(box, 4);
        caption(box, value, {mineWidth / 2, 24}, 0.3f, mineWidth - 6, color, FontWeight::Black);
        caption(box, label, {mineWidth / 2, 9}, 0.17f, mineWidth - 6, TextMuted);
    };
    mineTile(0, mapPoolNumber(integer(mine["rounds"])), i18n::text("map-info-mine-rounds", "Rounds"), Text);
    mineTile(1, fmt::format(fmt::runtime(i18n::text("map-info-mine-result", "{}W {}D {}L")),
        integer(mine["wins"]), integer(mine["draws"]), integer(mine["losses"])), i18n::text("map-info-mine-result-label", "Result"), Text);
    auto const best = number(mine["bestPercent"]);
    mineTile(2, mapPoolPercent(best), i18n::text("map-info-best", "Best progress"), best && *best >= 100 ? Green : Text);

    auto* viewMenu = CCMenu::create();
    viewMenu->setPosition(CCPointZero);
    card->addChild(viewMenu, 20);
    auto* view = CCMenuItemSpriteExtra::create(buttonFace(i18n::text("history-view-level", "VIEW LEVEL"), {leftWidth - 16, 26}, Accent, false, 0.28f),
        this, menu_selector(RankedMapInfoPopup::onViewLevel));
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
        m_content->addChild(box, 2);
        caption(box, label, {6, 32}, 0.17f, tileWidth - 10, TextMuted, FontWeight::SemiBold, 0);
        caption(box, value, {6, 19}, 0.33f, tileWidth - 10, Text, FontWeight::Black, 0);
        caption(box, detail, {6, 7}, 0.15f, tileWidth - 10, TextMuted, FontWeight::SemiBold, 0);
    };
    auto const totalRounds = integer(data["totals"]["rounds"]);
    headline(0, i18n::text("map-info-share", "Played"), mapPoolPercent(map.share),
        fmt::format(fmt::runtime(i18n::text("map-info-share-detail", "{} / {} rounds")), mapPoolNumber(map.rounds), mapPoolNumber(totalRounds)), Accent);
    headline(1, i18n::text("map-info-ban", "Banned"), mapPoolPercent(map.banRate),
        fmt::format(fmt::runtime(i18n::text("map-info-ban-detail", "{} bans · {} offers")), mapPoolNumber(map.bans), mapPoolNumber(map.banOffers)), Red);
    headline(2, i18n::text("map-info-draw", "Draws"), mapPoolPercent(map.drawRate),
        fmt::format(fmt::runtime(i18n::text("map-info-draw-detail", "{} Death Matches")), mapPoolNumber(map.draws)), Gold);
    headline(3, i18n::text("map-info-clear", "Clears"), mapPoolPercent(number(stats["clearRate"])),
        i18n::text("map-info-clear-detail", "Players who reached 100%"), Green);

    // Where players die: ten 10% bins, the deadliest one in gold.
    auto const middleTop = size.height - 98, middleBottom = 106.0f;
    auto const deathsWidth = rightWidth * 0.6f;
    auto* deaths = surface({deathsWidth, middleTop - middleBottom}, {}, Steel, SurfaceRaised, 245, false);
    deaths->setAnchorPoint(CCPointZero);
    deaths->setPosition({rightX, middleBottom});
    m_content->addChild(deaths, 2);
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
        fillRect(chart, {plotLeft + bin * (barWidth + 3), plotBottom, barWidth, height}, bin == peak ? Gold : Accent, bin == peak ? 1.0f : 0.72f);
    }
    fillRect(chart, {plotLeft, plotBottom - 1, plotWidth, 1}, TextMuted, 0.5f);
    caption(deaths, "0%", {plotLeft, 6}, 0.15f, 24, TextMuted, FontWeight::SemiBold, 0);
    caption(deaths, "50%", {plotLeft + plotWidth / 2, 6}, 0.15f, 24, TextMuted);
    caption(deaths, "100%", {plotLeft + plotWidth, 6}, 0.15f, 30, TextMuted, FontWeight::SemiBold, 1);

    // Played by tier: share of this map's rounds per match tier.
    auto const tiersX = rightX + deathsWidth + 6, tiersWidth = rightWidth - deathsWidth - 6;
    auto* tiers = surface({tiersWidth, middleTop - middleBottom}, {}, Steel, SurfaceRaised, 245, false);
    tiers->setAnchorPoint(CCPointZero);
    tiers->setPosition({tiersX, middleBottom});
    m_content->addChild(tiers, 2);
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
        m_content->addChild(box, 2);
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

void RankedMapInfoPopup::onClose(CCObject*) { dismiss(); }

void RankedMapInfoPopup::dismiss() {
    if (m_closed) return;
    [[maybe_unused]] Ref<RankedMapInfoPopup> keepAlive(this);
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    Popup::onClose(nullptr);
}

void RankedMapInfoPopup::onExit() {
    m_closed = true;
    ++m_generation;
    m_request.cancel();
    if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
    Popup::onExit();
}

} // namespace corum::ranked::ui
