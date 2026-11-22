#include "DebugFixedMapPopup.hpp"

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)

#include "RankedRuntime.hpp"
#include "domain/FixedMapOrder.hpp"
#include "ui/MapPoolVisuals.hpp"
#include "ui/RankedTapScroll.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <array>
#include <cctype>

using namespace geode::prelude;

namespace {
using namespace corum::ranked;
namespace pool = corum::ranked::ui::mappool;

constexpr float kWidth = 460.0f, kHeight = 290.0f, kRowH = 24.0f;
constexpr float kListX = 14.0f, kListW = 250.0f, kListTop = 204.0f, kListBottom = 14.0f;
constexpr float kSideX = 278.0f, kSideW = kWidth - 14.0f - kSideX;
constexpr std::array<char const*, 3> kBotDifficulties {"EASY", "NORMAL", "HARD"};
constexpr std::array<ccColor3B, 6> kPoolColors {{{244, 76, 91}, {55, 220, 234}, {208, 137, 70}, {201, 215, 235}, {255, 207, 74}, {181, 124, 255}}};

// Corum flat button: a raised surface with a centred caption.
CCMenuItemSpriteExtra* flatButton(CCObject* target, std::string const& text, CCSize size, CCPoint at, SEL_MenuHandler selector,
    ccColor3B accent = ui::Steel, ccColor3B fill = ui::SurfaceRaised, ccColor3B ink = ui::Text, float scale = 0.3f) {
    auto* face = ui::surface(size, {}, accent, fill, 255, true);
    auto* caption = ui::menuLabel(text, scale, {size.width / 2.0f, size.height / 2.0f}, ink, ui::FontWeight::ExtraBold);
    ui::fitMenuLabelWidth(caption, size.width - 8.0f, scale, scale * 0.55f); face->addChild(caption, 5);
    auto* item = CCMenuItemSpriteExtra::create(face, target, selector);
    item->setPosition(at);
    return item;
}
CCLabelBMFont* leftLabel(CCNode* parent, std::string const& text, float scale, CCPoint at, ccColor3B color, ui::FontWeight weight, float width) {
    auto* result = ui::label(text, scale, at, color, weight);
    result->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(result, width, scale, scale * 0.5f);
    parent->addChild(result, 4);
    return result;
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Design B: the active map pool on the left (search by title, creator or
// level id; a tap fills the next of R1 / R2 / R3 / DM, a second tap takes it
// out), the order and the opponent on the right. BOT (Easy / Normal / Hard)
// starts a Debug Bot match; a player idle in the lobby gets a challenge that
// scores like any Ranked match.
class DebugFixedMapPopup final : public Popup {
    DebugBotMatchOptions m_bot;
    FixedMapOrder m_order;
    std::vector<MapPoolRow> m_maps;
    std::vector<std::size_t> m_shown;
    std::string m_filter;
    // 0..2 = BOT at kBotDifficulties[m_target], else lobby player m_target - 3.
    int m_target = 1;
    bool m_loaded = false, m_failed = false, m_closed = false;
    TextInput* m_search = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    CCNode* m_side = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_targetLabel = nullptr;
    CCMenu* m_sideMenu = nullptr;
    CCMenuItemSpriteExtra* m_start = nullptr;
    std::uint64_t m_revision = 0;
    async::TaskHolder<web::WebResponse> m_request;

    bool init(DebugBotMatchOptions bot) {
        if (!Popup::init(kWidth, kHeight)) return false;
        m_bot = std::move(bot);
        m_bgSprite->setVisible(false); setID("ranked-debug-fixed-maps"_spr);
        m_mainLayer->addChild(ui::surface({kWidth, kHeight}, {kWidth / 2.0f, kHeight / 2.0f}, ui::Accent, ui::Surface, 255, true), 0);
        auto* title = leftLabel(m_mainLayer, i18n::text("debug-fixed-map", "FIXED MAP BATTLE"), 0.5f, {kListX, kHeight - 24.0f}, ui::Text, ui::FontWeight::Black, 260.0f);
        auto const chipX = kListX + title->getScaledContentSize().width + 12.0f;
        auto* chip = ui::surface({58.0f, 15.0f}, {chipX + 29.0f, kHeight - 24.0f}, ui::Orange, ui::Orange, 255, false);
        chip->addChild(ui::menuLabel("DEV ONLY", 0.2f, {29.0f, 7.5f}, ui::Ink, ui::FontWeight::Black), 5);
        m_mainLayer->addChild(chip, 4);
        leftLabel(m_mainLayer, "No ban phase  /  empty slots are random  /  player challenges score LP", 0.2f, {kListX, kHeight - 44.0f},
            ui::TextMuted, ui::FontWeight::SemiBold, kWidth - 2.0f * kListX);
        leftLabel(m_mainLayer, "MAP POOL  -  tap to fill the next slot", 0.22f, {kListX, kListTop + 34.0f}, ui::Accent, ui::FontWeight::ExtraBold, kListW);

        m_search = TextInput::create(kListW / 0.8f, "Search title / creator / ID");
        m_search->setScale(0.8f);
        m_search->setPosition({kListX + kListW / 2.0f, kListTop + 14.0f});
        m_search->setCallback([this](std::string const& text) { m_filter = lower(text); rebuildList(); });
        m_mainLayer->addChild(m_search, 4);

        m_scroll = ui::createRankedTapScroll({kListW, kListTop - kListBottom}, [weak = WeakRef(this)](CCPoint start, CCPoint end) {
            auto self = weak.lock();
            if (!self || self->m_closed || !self->m_scroll) return;
            auto const at = self->m_scroll->m_contentLayer->convertToNodeSpace(end);
            auto const from = self->m_scroll->m_contentLayer->convertToNodeSpace(start);
            auto const height = self->m_scroll->m_contentLayer->getContentSize().height;
            auto const row = static_cast<int>((height - at.y) / kRowH), fromRow = static_cast<int>((height - from.y) / kRowH);
            if (row != fromRow || row < 0 || static_cast<std::size_t>(row) >= self->m_shown.size()) return;
            self->m_order.pick(self->m_maps[self->m_shown[static_cast<std::size_t>(row)]].canonicalLevelId);
            self->rebuildList(); self->rebuildSide();
        });
        m_scroll->setPosition({kListX, kListBottom});
        m_mainLayer->addChild(m_scroll, 3);

        m_side = CCNode::create(); m_mainLayer->addChild(m_side, 3);
        m_sideMenu = CCMenu::create(); m_sideMenu->setPosition(CCPointZero); m_mainLayer->addChild(m_sideMenu, 5);
        m_status = ui::label("", 0.22f, {kSideX + kSideW / 2.0f, 50.0f}, ui::TextMuted, ui::FontWeight::SemiBold);
        m_mainLayer->addChild(m_status, 4);

        RankedRuntime::get().fetchDebugLobby();
        fetchMaps();
        rebuildList(); rebuildSide();
        schedule(schedule_selector(DebugFixedMapPopup::refresh), 0.25f);
        return true;
    }

    void fetchMaps() {
        auto pending = pool::request();
        m_request.spawn(pending.get(RankedRuntime::get().mapPoolUrl()), [weak = WeakRef(this)](web::WebResponse response) {
            queueInMainThread([weak, response = std::move(response)] {
                auto self = weak.lock();
                if (!self || self->m_closed) return;
                auto const data = response.json().unwrapOr(matjson::Value());
                self->m_loaded = true; self->m_failed = !response.ok() || !data["maps"].isArray();
                if (!self->m_failed) for (auto const& map : data["maps"].asArray().unwrap()) {
                    auto row = pool::parseRow(map);
                    if (!row.canonicalLevelId.empty() && self->m_maps.size() < 500) self->m_maps.push_back(std::move(row));
                }
                std::sort(self->m_maps.begin(), self->m_maps.end(), [](auto const& a, auto const& b) { return a.pool != b.pool ? a.pool < b.pool : a.title < b.title; });
                self->rebuildList(); self->rebuildSide();
            });
        });
    }

    void rebuildList() {
        if (!m_scroll) return;
        m_scroll->m_contentLayer->removeAllChildrenWithCleanup(true);
        m_shown.clear();
        for (std::size_t i = 0; i < m_maps.size(); ++i) {
            auto const& map = m_maps[i];
            if (!m_filter.empty() && lower(map.title).find(m_filter) == std::string::npos && lower(map.creator).find(m_filter) == std::string::npos &&
                map.levelId.find(m_filter) == std::string::npos && map.canonicalLevelId.find(m_filter) == std::string::npos) continue;
            m_shown.push_back(i);
        }
        auto const viewH = kListTop - kListBottom;
        auto const contentH = std::max(viewH, kRowH * static_cast<float>(m_shown.size()));
        m_scroll->m_contentLayer->setContentSize({kListW, contentH});
        if (!m_loaded || m_failed || m_shown.empty()) {
            auto* text = ui::label(!m_loaded ? "Loading map pool..." : m_failed ? "Could not load the map pool" : "No maps match",
                0.26f, {kListW / 2.0f, contentH - 20.0f}, ui::TextMuted, ui::FontWeight::SemiBold);
            m_scroll->m_contentLayer->addChild(text);
        }
        for (std::size_t row = 0; row < m_shown.size(); ++row) {
            auto const& map = m_maps[m_shown[row]];
            auto const slot = m_order.slotOf(map.canonicalLevelId);
            // A row-sized node: the scroll's culling keeps it while on screen.
            auto* line = CCNode::create(); line->setContentSize({kListW, kRowH});
            line->setPosition({0.0f, contentH - kRowH * static_cast<float>(row + 1)});
            m_scroll->m_contentLayer->addChild(line);
            auto const fill = slot >= 0 ? ccColor3B{42, 48, 22} : (row % 2 ? ui::SurfaceRaised : ccColor3B{27, 29, 38});
            auto* bg = CCLayerColor::create({fill.r, fill.g, fill.b, 255}, kListW, kRowH - 2.0f); line->addChild(bg, 0);
            auto const tone = kPoolColors[static_cast<std::size_t>(std::clamp(map.pool, 1, 6) - 1)];
            auto* poolChip = CCLayerColor::create({tone.r, tone.g, tone.b, 255}, 18.0f, 12.0f); poolChip->setPosition({4.0f, 5.0f}); line->addChild(poolChip, 1);
            line->addChild(ui::label(fmt::format("{}", map.pool), 0.2f, {13.0f, 11.0f}, ui::Ink, ui::FontWeight::Black), 2);
            auto* title = ui::label(map.title.empty() ? map.levelId : map.title, 0.24f, {28.0f, 15.0f}, ui::Text, ui::FontWeight::ExtraBold);
            title->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(title, kListW - 70.0f, 0.24f, 0.12f); line->addChild(title, 2);
            auto* meta = ui::label(fmt::format("{} · {}", map.creator, map.levelId), 0.16f, {28.0f, 6.0f}, ui::TextMuted, ui::FontWeight::SemiBold);
            meta->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(meta, kListW - 70.0f, 0.16f, 0.1f); line->addChild(meta, 2);
            if (slot >= 0) {
                line->addChild(ui::label(FixedMapOrder::label(static_cast<std::size_t>(slot)), 0.28f, {kListW - 18.0f, kRowH / 2.0f},
                    slot == static_cast<int>(FixedMapOrder::Deathmatch) ? ui::Gold : ui::Accent, ui::FontWeight::Black), 2);
            }
        }
        m_scroll->scrollToTop();
    }

    std::string mapTitle(std::string const& id) const {
        for (auto const& map : m_maps) if (map.canonicalLevelId == id) return map.title.empty() ? map.levelId : map.title;
        return id;
    }

    void rebuildSide() {
        m_side->removeAllChildrenWithCleanup(true);
        m_sideMenu->removeAllChildrenWithCleanup(true);
        leftLabel(m_side, "MATCH ORDER", 0.22f, {kSideX, kListTop + 34.0f}, ui::Gold, ui::FontWeight::ExtraBold, kSideW);
        for (std::size_t i = 0; i < FixedMapOrder::Slots; ++i) {
            auto const y = kListTop + 12.0f - static_cast<float>(i) * 27.0f;
            auto const deathmatch = i == FixedMapOrder::Deathmatch;
            auto const& id = m_order.slots[i];
            auto* row = ui::surface({kSideW, 23.0f}, {kSideX + kSideW / 2.0f, y}, id.empty() ? ui::Steel : deathmatch ? ui::Gold : ui::Accent, ui::SurfaceRaised, 255, false);
            m_side->addChild(row);
            leftLabel(m_side, FixedMapOrder::label(i), 0.3f, {kSideX + 7.0f, y}, deathmatch ? ui::Gold : ui::Accent, ui::FontWeight::Black, 26.0f);
            leftLabel(m_side, id.empty() ? std::string("random") : mapTitle(id), 0.22f, {kSideX + 34.0f, y},
                id.empty() ? ui::TextMuted : ui::Text, id.empty() ? ui::FontWeight::SemiBold : ui::FontWeight::ExtraBold, kSideW - 62.0f);
            if (!id.empty()) {
                auto* clear = flatButton(this, "X", {18.0f, 17.0f}, {kSideX + kSideW - 12.0f, y}, menu_selector(DebugFixedMapPopup::onClearSlot), ui::Red, ui::Surface, ui::Red, 0.24f);
                clear->setTag(static_cast<int>(i)); m_sideMenu->addChild(clear);
            }
        }
        // Opponent: BOT (Easy / Normal / Hard) or a player idle in the lobby.
        leftLabel(m_side, "OPPONENT", 0.22f, {kSideX, 104.0f}, ui::Gold, ui::FontWeight::ExtraBold, kSideW);
        m_sideMenu->addChild(flatButton(this, "<", {22.0f, 22.0f}, {kSideX + 11.0f, 84.0f}, menu_selector(DebugFixedMapPopup::onPreviousTarget)));
        m_sideMenu->addChild(flatButton(this, ">", {22.0f, 22.0f}, {kSideX + kSideW - 11.0f, 84.0f}, menu_selector(DebugFixedMapPopup::onNextTarget)));
        m_targetLabel = ui::label("", 0.26f, {kSideX + kSideW / 2.0f, 84.0f}, ui::Text, ui::FontWeight::ExtraBold);
        m_side->addChild(m_targetLabel, 4);
        m_sideMenu->addChild(flatButton(this, "Refresh players", {110.0f, 17.0f}, {kSideX + kSideW / 2.0f, 65.0f},
            menu_selector(DebugFixedMapPopup::onRefreshPlayers), ui::Steel, ui::Surface, ui::TextMuted, 0.2f));
        m_start = flatButton(this, "START", {kSideW, 28.0f}, {kSideX + kSideW / 2.0f, 26.0f}, menu_selector(DebugFixedMapPopup::onStart),
            ui::AccentFill, ui::AccentFill, ui::AccentInk, 0.38f);
        m_sideMenu->addChild(m_start);
        refreshTarget();
    }

    [[nodiscard]] bool botTarget() const { return m_target < static_cast<int>(kBotDifficulties.size()); }
    [[nodiscard]] int targetCount() const { return static_cast<int>(kBotDifficulties.size() + RankedRuntime::get().view().debugLobby.size()); }

    void refreshTarget() {
        auto const& lobby = RankedRuntime::get().view().debugLobby;
        if (m_target < 0 || m_target >= targetCount()) m_target = 1;
        auto const bots = static_cast<int>(kBotDifficulties.size());
        auto const text = botTarget() ? fmt::format("BOT  {}", kBotDifficulties[static_cast<std::size_t>(m_target)])
            : fmt::format("{}  {} {} LP", lobby[static_cast<std::size_t>(m_target - bots)].name, lobby[static_cast<std::size_t>(m_target - bots)].tier, lobby[static_cast<std::size_t>(m_target - bots)].lp);
        m_targetLabel->setString(text.c_str());
        m_targetLabel->setColor(botTarget() ? ccColor3B{110, 240, 255} : ui::Accent);
        ui::fitLabelWidth(m_targetLabel, kSideW - 52.0f, 0.26f, 0.12f);
    }

    // The sent challenge's state, until its match starts (the popup closes).
    void refresh(float) {
        auto const& view = RankedRuntime::get().view();
        if (view.stage == RuntimeStage::Matched) { onClose(nullptr); return; }
        if (view.revision != m_revision) { m_revision = view.revision; refreshTarget(); }
        std::string status; ccColor3B tone{169, 174, 189};
        if (!view.debugChallengeError.empty()) { status = view.debugChallengeError; tone = {255, 91, 111}; }
        else if (auto const& out = view.debugOutgoing; out && !botTarget()) {
            status = fmt::format("CHALLENGE TO {}: {}", out->to, out->status);
            tone = out->status == "PENDING" ? ccColor3B{255, 207, 74} : out->status == "ACCEPTED" ? ccColor3B{90, 238, 151} : ccColor3B{255, 91, 111};
        } else if (!botTarget()) status = "Sends a scoring challenge (60 s)";
        else status = view.debugLobby.empty() ? "No debug players idle in the lobby" : fmt::format("{} debug players in the lobby", view.debugLobby.size());
        m_status->setString(status.c_str()); m_status->setColor(tone); ui::fitLabelWidth(m_status, kSideW, 0.22f, 0.11f);
        if (m_start) m_start->setEnabled(view.stage == RuntimeStage::Ready);
    }

    void onClearSlot(CCObject* sender) {
        if (auto* node = typeinfo_cast<CCNode*>(sender)) m_order.clear(static_cast<std::size_t>(node->getTag()));
        rebuildList(); rebuildSide();
    }
    void onPreviousTarget(CCObject*) { m_target = m_target <= 0 ? targetCount() - 1 : m_target - 1; refreshTarget(); }
    void onNextTarget(CCObject*) { m_target = m_target + 1 >= targetCount() ? 0 : m_target + 1; refreshTarget(); }
    void onRefreshPlayers(CCObject*) { RankedRuntime::get().fetchDebugLobby(); }

    void onStart(CCObject*) {
        auto& runtime = RankedRuntime::get();
        if (runtime.view().stage != RuntimeStage::Ready) return;
        auto const rounds = m_order.rounds();
        if (botTarget()) {
            auto options = m_bot;
            options.difficulty = kBotDifficulties[static_cast<std::size_t>(m_target)];
            options.fixedRounds = rounds; options.fixedDeathmatch = m_order.deathmatch();
            if (options.fixedRounds.empty() && options.fixedDeathmatch.empty()) options.fixedRounds = {""}; // all random, still no bans
            runtime.startDebugBotMatch(options);
            onClose(nullptr);
            return;
        }
        auto const& lobby = runtime.view().debugLobby;
        auto const player = static_cast<std::size_t>(m_target) - kBotDifficulties.size();
        if (player >= lobby.size()) return;
        if (auto const& out = runtime.view().debugOutgoing; out && out->status == "PENDING") runtime.cancelDebugChallenge(out->id);
        runtime.sendDebugChallenge(m_bot.password, lobby[player].playerId, rounds.empty() ? std::vector<std::string>{""} : rounds, m_order.deathmatch());
    }

public:
    void onClose(CCObject* sender) override {
        if (m_closed) return;
        m_closed = true; m_request.cancel(); unschedule(schedule_selector(DebugFixedMapPopup::refresh));
        Popup::onClose(sender);
    }
    static DebugFixedMapPopup* create(DebugBotMatchOptions bot) {
        auto* popup = new DebugFixedMapPopup();
        if (popup->init(std::move(bot))) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
};
} // namespace

namespace corum::ranked {
void showDebugFixedMapPopup(DebugBotMatchOptions bot) {
    if (auto* popup = DebugFixedMapPopup::create(std::move(bot))) popup->show();
}
} // namespace corum::ranked

#endif
