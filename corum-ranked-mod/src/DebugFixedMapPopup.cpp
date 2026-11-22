#include "DebugFixedMapPopup.hpp"

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)

#include "RankedRuntime.hpp"
#include "domain/FixedMapOrder.hpp"
#include "ui/MapPoolVisuals.hpp"
#include "ui/RankedTapScroll.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace {
using namespace corum::ranked;
namespace pool = corum::ranked::ui::mappool;

constexpr float kWidth = 460.0f, kHeight = 290.0f, kRowH = 24.0f;
constexpr float kListX = 12.0f, kListW = 252.0f, kListTop = 228.0f, kListBottom = 14.0f;
constexpr float kSideX = 276.0f, kSideW = kWidth - 12.0f - kSideX;
constexpr std::array<ccColor3B, 6> kPoolColors {{{244, 76, 91}, {55, 220, 234}, {208, 137, 70}, {201, 215, 235}, {255, 207, 74}, {181, 124, 255}}};

CCMenuItemSpriteExtra* gdButton(CCObject* target, char const* text, CCPoint at, SEL_MenuHandler selector, float scale = 0.5f) {
    auto* sprite = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_01.png", 0.72f);
    sprite->setScale(scale);
    auto* item = CCMenuItemSpriteExtra::create(sprite, target, selector);
    item->setPosition(at);
    return item;
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Design B: the active map pool on the left (search by title, creator or
// level id; a tap fills the next of R1 / R2 / R3 / DM, a second tap takes it
// out), the order and the opponent on the right. BOT starts a Debug Bot match;
// a player idle in the lobby gets a challenge that scores like any Ranked match.
class DebugFixedMapPopup final : public Popup {
    DebugBotMatchOptions m_bot;
    FixedMapOrder m_order;
    std::vector<MapPoolRow> m_maps;
    std::vector<std::size_t> m_shown;
    std::string m_filter;
    int m_target = -1; // -1 = BOT, else an index into the runtime's debug lobby
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
        setTitle("FIXED MAP BATTLE", "goldFont.fnt", 0.6f, 20.0f);
        auto* note = CCLabelBMFont::create("NO BAN PHASE - EMPTY SLOTS ARE RANDOM - PLAYER CHALLENGES SCORE LP", "bigFont.fnt");
        note->setColor(ccc3(255, 145, 105)); note->setPosition({kWidth / 2.0f, kHeight - 40.0f});
        note->limitLabelWidth(kWidth - 30.0f, 0.24f, 0.16f); m_mainLayer->addChild(note, 3);

        m_search = TextInput::create(kListW, "Search title / creator / ID");
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
        m_status = CCLabelBMFont::create("", "bigFont.fnt");
        m_status->setPosition({kSideX + kSideW / 2.0f, 70.0f}); m_mainLayer->addChild(m_status, 4);

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
            auto* text = CCLabelBMFont::create(!m_loaded ? "Loading map pool..." : m_failed ? "Could not load the map pool" : "No maps match", "bigFont.fnt");
            text->setScale(0.3f); text->setPosition({kListW / 2.0f, contentH - 20.0f}); text->setOpacity(160);
            m_scroll->m_contentLayer->addChild(text);
        }
        for (std::size_t row = 0; row < m_shown.size(); ++row) {
            auto const& map = m_maps[m_shown[row]];
            auto const slot = m_order.slotOf(map.canonicalLevelId);
            // A row-sized node: the scroll's culling keeps it while on screen.
            auto* line = CCNode::create(); line->setContentSize({kListW, kRowH});
            line->setPosition({0.0f, contentH - kRowH * static_cast<float>(row + 1)});
            m_scroll->m_contentLayer->addChild(line);
            auto const fill = slot >= 0 ? ccColor3B{42, 48, 22} : (row % 2 ? ccColor3B{32, 35, 46} : ccColor3B{27, 29, 38});
            auto* bg = CCLayerColor::create({fill.r, fill.g, fill.b, 255}, kListW, kRowH - 2.0f); line->addChild(bg, 0);
            auto const tone = kPoolColors[static_cast<std::size_t>(std::clamp(map.pool, 1, 6) - 1)];
            auto* poolChip = CCLayerColor::create({tone.r, tone.g, tone.b, 255}, 18.0f, 12.0f); poolChip->setPosition({4.0f, 5.0f}); line->addChild(poolChip, 1);
            auto* poolText = CCLabelBMFont::create(fmt::format("{}", map.pool).c_str(), "bigFont.fnt"); poolText->setScale(0.22f);
            poolText->setColor({13, 14, 18}); poolText->setPosition({13.0f, 11.0f}); line->addChild(poolText, 2);
            auto* title = ui::label(map.title.empty() ? map.levelId : map.title, 0.24f, {28.0f, 15.0f}, ui::Text, ui::FontWeight::ExtraBold);
            title->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(title, kListW - 70.0f, 0.24f, 0.12f); line->addChild(title, 2);
            auto* meta = ui::label(fmt::format("{} · {}", map.creator, map.levelId), 0.16f, {28.0f, 6.0f}, ui::TextMuted, ui::FontWeight::SemiBold);
            meta->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(meta, kListW - 70.0f, 0.16f, 0.1f); line->addChild(meta, 2);
            if (slot >= 0) {
                auto* tag = CCLabelBMFont::create(FixedMapOrder::label(static_cast<std::size_t>(slot)), "bigFont.fnt");
                tag->setScale(0.3f); tag->setColor({200, 255, 46}); tag->setPosition({kListW - 18.0f, kRowH / 2.0f}); line->addChild(tag, 2);
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
        auto* heading = CCLabelBMFont::create("ORDER", "goldFont.fnt");
        heading->setScale(0.5f); heading->setAnchorPoint({0.0f, 0.5f}); heading->setPosition({kSideX, kListTop + 14.0f}); m_side->addChild(heading);
        for (std::size_t i = 0; i < FixedMapOrder::Slots; ++i) {
            auto const y = kListTop - 14.0f - static_cast<float>(i) * 26.0f;
            auto* bg = CCLayerColor::create({32, 35, 46, 255}, kSideW, 22.0f); bg->setPosition({kSideX, y - 11.0f}); m_side->addChild(bg);
            auto* slot = CCLabelBMFont::create(FixedMapOrder::label(i), "bigFont.fnt");
            slot->setScale(0.32f); slot->setColor(i == FixedMapOrder::Deathmatch ? ccColor3B{255, 207, 74} : ccColor3B{200, 255, 46});
            slot->setPosition({kSideX + 14.0f, y}); m_side->addChild(slot);
            auto const& id = m_order.slots[i];
            auto* name = ui::label(id.empty() ? std::string("RANDOM") : mapTitle(id), 0.22f, {kSideX + 30.0f, y},
                id.empty() ? ui::TextMuted : ui::Text, ui::FontWeight::ExtraBold);
            name->setAnchorPoint({0.0f, 0.5f}); ui::fitLabelWidth(name, kSideW - 56.0f, 0.22f, 0.1f); m_side->addChild(name);
            if (!id.empty()) {
                auto* clear = gdButton(this, "X", {kSideX + kSideW - 10.0f, y}, menu_selector(DebugFixedMapPopup::onClearSlot), 0.36f);
                clear->setTag(static_cast<int>(i)); m_sideMenu->addChild(clear);
            }
        }
        // Opponent: BOT (the Debug Bot settings) or a player idle in the lobby.
        auto const targetY = kListTop - 14.0f - 4.0f * 26.0f - 6.0f;
        m_sideMenu->addChild(gdButton(this, "<", {kSideX + 10.0f, targetY}, menu_selector(DebugFixedMapPopup::onPreviousTarget), 0.4f));
        m_sideMenu->addChild(gdButton(this, ">", {kSideX + kSideW - 10.0f, targetY}, menu_selector(DebugFixedMapPopup::onNextTarget), 0.4f));
        m_targetLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_targetLabel->setPosition({kSideX + kSideW / 2.0f, targetY}); m_side->addChild(m_targetLabel);
        m_sideMenu->addChild(gdButton(this, "Refresh players", {kSideX + kSideW / 2.0f, targetY - 22.0f}, menu_selector(DebugFixedMapPopup::onRefreshPlayers), 0.38f));
        m_start = gdButton(this, "START", {kSideX + kSideW / 2.0f, 34.0f}, menu_selector(DebugFixedMapPopup::onStart), 0.62f);
        m_sideMenu->addChild(m_start);
        refreshTarget();
    }

    void refreshTarget() {
        auto const& lobby = RankedRuntime::get().view().debugLobby;
        if (m_target >= static_cast<int>(lobby.size())) m_target = -1;
        auto const text = m_target < 0 ? fmt::format("VS BOT {}", m_bot.difficulty)
            : fmt::format("VS {} ({} {} LP)", lobby[static_cast<std::size_t>(m_target)].name, lobby[static_cast<std::size_t>(m_target)].tier, lobby[static_cast<std::size_t>(m_target)].lp);
        m_targetLabel->setString(text.c_str());
        m_targetLabel->setColor(m_target < 0 ? ccColor3B{110, 240, 255} : ccColor3B{200, 255, 46});
        m_targetLabel->limitLabelWidth(kSideW - 44.0f, 0.3f, 0.14f);
    }

    // The sent challenge's state, until its match starts (the popup closes).
    void refresh(float) {
        auto const& view = RankedRuntime::get().view();
        if (view.stage == RuntimeStage::Matched) { onClose(nullptr); return; }
        if (view.revision != m_revision) { m_revision = view.revision; refreshTarget(); }
        std::string status; ccColor3B tone{169, 174, 189};
        if (!view.debugChallengeError.empty()) { status = view.debugChallengeError; tone = {255, 91, 111}; }
        else if (auto const& out = view.debugOutgoing; out && m_target >= 0) {
            status = fmt::format("CHALLENGE TO {}: {}", out->to, out->status);
            tone = out->status == "PENDING" ? ccColor3B{255, 207, 74} : out->status == "ACCEPTED" ? ccColor3B{90, 238, 151} : ccColor3B{255, 91, 111};
        } else if (m_target >= 0) status = view.debugLobby.empty() ? "NO DEBUG PLAYERS IDLE IN THE LOBBY" : "SENDS A SCORING CHALLENGE (60 S)";
        else status = "STARTS A DEBUG BOT MATCH ON THESE MAPS";
        m_status->setString(status.c_str()); m_status->setColor(tone); m_status->limitLabelWidth(kSideW, 0.26f, 0.12f);
        if (m_start) m_start->setEnabled(view.stage == RuntimeStage::Ready);
    }

    void onClearSlot(CCObject* sender) {
        if (auto* node = typeinfo_cast<CCNode*>(sender)) m_order.clear(static_cast<std::size_t>(node->getTag()));
        rebuildList(); rebuildSide();
    }
    void onPreviousTarget(CCObject*) {
        auto const count = static_cast<int>(RankedRuntime::get().view().debugLobby.size());
        m_target = m_target <= -1 ? count - 1 : m_target - 1; refreshTarget();
    }
    void onNextTarget(CCObject*) {
        auto const count = static_cast<int>(RankedRuntime::get().view().debugLobby.size());
        m_target = m_target + 1 >= count ? -1 : m_target + 1; refreshTarget();
    }
    void onRefreshPlayers(CCObject*) { RankedRuntime::get().fetchDebugLobby(); }

    void onStart(CCObject*) {
        auto& runtime = RankedRuntime::get();
        if (runtime.view().stage != RuntimeStage::Ready) return;
        auto const rounds = m_order.rounds();
        if (m_target < 0) {
            auto options = m_bot;
            options.fixedRounds = rounds; options.fixedDeathmatch = m_order.deathmatch();
            if (options.fixedRounds.empty() && options.fixedDeathmatch.empty()) options.fixedRounds = {""}; // all random, still no bans
            runtime.startDebugBotMatch(options);
            onClose(nullptr);
            return;
        }
        auto const& lobby = runtime.view().debugLobby;
        if (m_target >= static_cast<int>(lobby.size())) return;
        if (auto const& out = runtime.view().debugOutgoing; out && out->status == "PENDING") runtime.cancelDebugChallenge(out->id);
        runtime.sendDebugChallenge(m_bot.password, lobby[static_cast<std::size_t>(m_target)].playerId, rounds.empty() ? std::vector<std::string>{""} : rounds, m_order.deathmatch());
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
