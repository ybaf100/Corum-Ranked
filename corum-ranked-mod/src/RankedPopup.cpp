#include "RankedPopup.hpp"

#include "RankedRuntime.hpp"

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "debug/DebugBotPopup.hpp"
#endif

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextArea.hpp>

#include <algorithm>
#include <limits>
#include <string>

using namespace geode::prelude;
using corum::ranked::MatchView;
using corum::ranked::RankedRuntime;
using corum::ranked::RuntimeStage;
using corum::ranked::RuntimeView;
using corum::ranked::stageName;

namespace {

std::string shorten(std::string value, std::size_t maximum) {
    if (value.size() <= maximum) return value;
    if (maximum <= 3) return value.substr(0, maximum);
    value.resize(maximum - 3);
    return value + "...";
}

char const* buttonBackground(bool danger = false) {
    return danger ? "GJ_button_06.png" : "GJ_button_01.png";
}

class CorumRankedPopup final : public Popup, public LevelDownloadDelegate {
protected:
    CCNode* m_content = nullptr;
    CCMenu* m_actionMenu = nullptr;
    CCLabelBMFont* m_timerLabel = nullptr;
    GJGameLevel* m_downloadedLevel = nullptr;
    std::uint64_t m_renderedRevision = std::numeric_limits<std::uint64_t>::max();
    int m_downloadingLevelId = 0;
    std::string m_localMessage;

    ~CorumRankedPopup() override {
        detachDownloadDelegate();
        CC_SAFE_RELEASE(m_downloadedLevel);
    }

    bool init() override {
        if (!Popup::init(460.0f, 330.0f)) return false;
        setTitle("Corum Ranked", "goldFont.fnt", 0.72f, 23.0f);

        m_content = CCNode::create();
        m_content->setContentSize(m_size);
        m_content->setAnchorPoint({0.0f, 0.0f});
        m_content->setPosition(CCPointZero);
        m_mainLayer->addChild(m_content, 2);

        m_actionMenu = CCMenu::create();
        m_actionMenu->setPosition(CCPointZero);
        m_actionMenu->setID("corum-ranked-action-menu"_spr);
        m_mainLayer->addChild(m_actionMenu, 4);

        RankedRuntime::get().begin();
        render();
        schedule(schedule_selector(CorumRankedPopup::refresh), 0.20f);
        return true;
    }

    void refresh(float) {
        auto& runtime = RankedRuntime::get();
        runtime.tick();
        if (runtime.view().revision != m_renderedRevision) render();
        updateTimer();
    }

    void render() {
        auto const& view = RankedRuntime::get().view();
        m_renderedRevision = view.revision;
        m_content->removeAllChildrenWithCleanup(true);
        m_actionMenu->removeAllChildrenWithCleanup(true);
        m_timerLabel = nullptr;
        setTitle("Corum Ranked", "goldFont.fnt", 0.72f, 23.0f);

        addHeader(view);
        if (view.stage == RuntimeStage::Loading || view.stage == RuntimeStage::JoiningQueue) {
            auto spinner = LoadingSpinner::create(58.0f);
            spinner->setPosition({230.0f, 175.0f});
            m_content->addChild(spinner, 2);
        }

        if (view.stage == RuntimeStage::Ready) {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            addButton("Join Queue", {230.0f, 72.0f}, menu_selector(CorumRankedPopup::onJoin));
            addButton(
                "DEBUG BOT MATCH",
                {230.0f, 34.0f},
                menu_selector(CorumRankedPopup::onDebugBotMatch),
                true,
                0.52f
            );
#else
            addButton("Join Queue", {230.0f, 62.0f}, menu_selector(CorumRankedPopup::onJoin));
#endif
        } else if (view.stage == RuntimeStage::Queued) {
            addButton("Leave Queue", {230.0f, 62.0f}, menu_selector(CorumRankedPopup::onLeave), true);
        } else if (
            view.stage == RuntimeStage::Error ||
            view.stage == RuntimeStage::Blocked ||
            view.stage == RuntimeStage::NotConfigured
        ) {
            addButton("Retry", {230.0f, 62.0f}, menu_selector(CorumRankedPopup::onRetry));
        } else if (view.stage == RuntimeStage::Matched) {
            renderMatch(view.match);
        }
    }

    void addHeader(RuntimeView const& view) {
        auto stage = CCLabelBMFont::create(stageName(view.stage), "bigFont.fnt");
        stage->setColor(view.stage == RuntimeStage::Blocked || view.stage == RuntimeStage::Error
            ? ccc3(255, 115, 115)
            : ccc3(110, 240, 255));
        stage->setPosition({230.0f, 281.0f});
        stage->limitLabelWidth(390.0f, 0.48f, 0.28f);
        m_content->addChild(stage, 2);

        std::string detail = view.status;
        if (!m_localMessage.empty()) detail = m_localMessage;
        if (!view.error.empty()) detail += detail.empty() ? view.error : "\n" + view.error;
        if (view.stage == RuntimeStage::Ready) {
            detail += fmt::format(
                "\nTier: {}  Placements: {}/{}",
                view.profileTier,
                view.placementGames,
                view.placementGamesRequired
            );
        }
        auto text = SimpleTextArea::create(shorten(detail, 360), "bigFont.fnt", 0.31f, 400.0f);
        text->setAlignment(kCCTextAlignmentCenter);
        text->setMaxLines(4);
        text->setLinePadding(-2.0f);
        text->setPosition({230.0f, 238.0f});
        m_content->addChild(text, 2);
    }

    void renderMatch(MatchView const& match) {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if (match.debugBotMatch) {
            auto* debug = CCLabelBMFont::create("BOT MATCH - DEBUG", "goldFont.fnt");
            debug->setColor(ccc3(255, 165, 70));
            debug->setPosition({230.0f, 220.0f});
            debug->setScale(0.29f);
            m_content->addChild(debug, 3);
        }
#endif
        auto info = CCLabelBMFont::create(
            fmt::format(
                "{} vs {}  |  {}  |  {}",
                match.side.empty() ? "?" : match.side,
                shorten(match.opponentName, 14),
                match.effectiveTier,
                match.state
            ).c_str(),
            "bigFont.fnt"
        );
        info->setPosition({230.0f, 207.0f});
        info->limitLabelWidth(410.0f, 0.34f, 0.20f);
        m_content->addChild(info, 2);

        m_timerLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_timerLabel->setColor(ccc3(255, 220, 90));
        m_timerLabel->setPosition({410.0f, 282.0f});
        m_timerLabel->setScale(0.35f);
        m_content->addChild(m_timerLabel, 3);
        updateTimer();

        if (match.state == "BAN_PHASE") {
            renderBans(match);
            return;
        }

        if (match.currentMap) {
            auto const mapText = fmt::format(
                "{}  by {}\nPool {} | Qualifying {:.3g}%",
                match.currentMap->title,
                match.currentMap->creator,
                match.currentMap->pool,
                match.currentMap->qualifyingPercent
            );
            auto map = SimpleTextArea::create(shorten(mapText, 150), "bigFont.fnt", 0.32f, 390.0f);
            map->setAlignment(kCCTextAlignmentCenter);
            map->setMaxLines(2);
            map->setLinePadding(-2.0f);
            map->setPosition({230.0f, 166.0f});
            m_content->addChild(map, 2);
        }

        if (match.roundNumber > 0) {
            auto const bannerText = match.banner == "NONE" || match.banner.empty()
                ? fmt::format("ROUND {}", match.roundNumber)
                : fmt::format("{} - ROUND {}", match.banner, match.roundNumber);
            auto banner = CCLabelBMFont::create(bannerText.c_str(), "goldFont.fnt");
            banner->setPosition({230.0f, 128.0f});
            banner->limitLabelWidth(360.0f, 0.48f, 0.28f);
            m_content->addChild(banner, 2);

            auto score = CCLabelBMFont::create(
                fmt::format(
                    "A {} ({} clears)  -  B {} ({} clears)",
                    match.scoreA,
                    match.clearsA,
                    match.scoreB,
                    match.clearsB
                ).c_str(),
                "bigFont.fnt"
            );
            score->setPosition({230.0f, 103.0f});
            score->setScale(0.31f);
            m_content->addChild(score, 2);
        } else if (match.deathmatchSequence > 0) {
            auto deathmatch = CCLabelBMFont::create(
                fmt::format("DEATHMATCH {} - 3 ATTEMPTS EACH", match.deathmatchSequence).c_str(),
                "goldFont.fnt"
            );
            deathmatch->setPosition({230.0f, 124.0f});
            deathmatch->limitLabelWidth(390.0f, 0.44f, 0.26f);
            m_content->addChild(deathmatch, 2);
        }

        if (match.state == "MATCHED") {
            addButton("Ready", {230.0f, 57.0f}, menu_selector(CorumRankedPopup::onReady));
        } else if (match.state == "ROUND_PREPARE" || match.state == "DEATHMATCH_PREPARE") {
            auto const localMap = findLocalMap();
            if (localMap) {
                addButton("Ready", {285.0f, 52.0f}, menu_selector(CorumRankedPopup::onReady));
                addButton("Map Ready", {165.0f, 52.0f}, menu_selector(CorumRankedPopup::onDownload));
            } else {
                addButton("Download Map", {230.0f, 52.0f}, menu_selector(CorumRankedPopup::onDownload));
            }
        } else if (
            match.state == "ROUND_PLAYING" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "DEATHMATCH_PLAYING"
        ) {
            addButton("Play Current Map", {230.0f, 52.0f}, menu_selector(CorumRankedPopup::onPlay));
        } else if (match.state == "MATCH_RESULT") {
            auto const won = match.winnerSide == match.side;
            auto result = CCLabelBMFont::create(won ? "VICTORY" : "DEFEAT", "goldFont.fnt");
            result->setColor(won ? ccc3(120, 255, 145) : ccc3(255, 120, 120));
            result->setPosition({230.0f, 80.0f});
            result->setScale(0.62f);
            m_content->addChild(result, 2);
            if (match.ownMmrDelta && match.ownRatingAfter) {
                auto mmr = CCLabelBMFont::create(
                    fmt::format("MMR {:+d} -> {}", *match.ownMmrDelta, *match.ownRatingAfter).c_str(),
                    "bigFont.fnt"
                );
                mmr->setPosition({230.0f, 49.0f});
                mmr->setScale(0.36f);
                m_content->addChild(mmr, 2);
            }
        }
    }

    void renderBans(MatchView const& match) {
        auto label = CCLabelBMFont::create("PRIVATE BAN - choose one map", "goldFont.fnt");
        label->setPosition({230.0f, 194.0f});
        label->setScale(0.38f);
        m_content->addChild(label, 2);

        auto const count = std::min<std::size_t>(match.candidateMaps.size(), 6);
        for (std::size_t index = 0; index < count; ++index) {
            auto const column = static_cast<float>(index % 2);
            auto const row = static_cast<float>(index / 2);
            auto button = addButton(
                shorten(match.candidateMaps[index].title, 20).c_str(),
                {120.0f + column * 220.0f, 157.0f - row * 40.0f},
                menu_selector(CorumRankedPopup::onBan),
                true,
                0.58f
            );
            button->setTag(static_cast<int>(index));
        }
        addButton("No Ban", {230.0f, 38.0f}, menu_selector(CorumRankedPopup::onNoBan), false, 0.62f);
    }

    CCMenuItemSpriteExtra* addButton(
        char const* text,
        CCPoint position,
        SEL_MenuHandler selector,
        bool danger = false,
        float scale = 0.72f
    ) {
        auto sprite = ButtonSprite::create(text, "bigFont.fnt", buttonBackground(danger), 0.75f);
        sprite->setScale(scale);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, selector);
        button->setPosition(position);
        m_actionMenu->addChild(button);
        return button;
    }

    void updateTimer() {
        if (!m_timerLabel) return;
        auto const seconds = RankedRuntime::get().deadlineSeconds();
        m_timerLabel->setString(seconds ? fmt::format("{}s", *seconds).c_str() : "");
    }

    GJGameLevel* findLocalMap() const {
        auto const id = RankedRuntime::get().currentLevelId();
        if (id <= 0) return nullptr;
        if (m_downloadedLevel && static_cast<int>(m_downloadedLevel->m_levelID) == id) {
            return m_downloadedLevel;
        }
        auto* manager = GameLevelManager::sharedState();
        return manager ? manager->getSavedLevel(id) : nullptr;
    }

    void onJoin(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().joinQueue();
    }

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void onDebugBotMatch(CCObject*) {
        m_localMessage.clear();
        corum::ranked::debug::showDebugBotPasswordPopup();
    }
#endif

    void onLeave(CCObject*) {
        RankedRuntime::get().leaveQueue();
    }

    void onRetry(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().begin();
    }

    void onReady(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().submitReady();
    }

    void onBan(CCObject* sender) {
        auto const index = sender ? sender->getTag() : -1;
        auto const& maps = RankedRuntime::get().view().match.candidateMaps;
        if (index < 0 || static_cast<std::size_t>(index) >= maps.size()) return;
        RankedRuntime::get().submitBan(maps[static_cast<std::size_t>(index)].canonicalLevelId);
    }

    void onNoBan(CCObject*) {
        RankedRuntime::get().submitBan(std::nullopt);
    }

    void onDownload(CCObject*) {
        auto const id = RankedRuntime::get().currentLevelId();
        if (id <= 0) return;
        if (findLocalMap()) {
            m_localMessage = "The current map is downloaded. Confirm Ready when prepared.";
            ++m_renderedRevision;
            render();
            return;
        }
        auto* manager = GameLevelManager::sharedState();
        if (!manager || m_downloadingLevelId != 0) return;
        m_downloadingLevelId = id;
        manager->m_levelDownloadDelegate = this;
        m_localMessage = "Downloading the current map before Ready...";
        manager->downloadLevel(id, false, 0);
        render();
    }

    void onPlay(CCObject*) {
        auto* level = findLocalMap();
        if (!level) {
            m_localMessage = "The current map is not downloaded. Return to the prepare phase flow.";
            render();
            return;
        }
        auto scene = PlayLayer::scene(level, false, false);
        auto transition = CCTransitionFade::create(0.4f, scene);
        CCDirector::sharedDirector()->replaceScene(transition);
    }

    void levelDownloadFinished(GJGameLevel* level) override {
        detachDownloadDelegate();
        m_downloadingLevelId = 0;
        CC_SAFE_RETAIN(level);
        CC_SAFE_RELEASE(m_downloadedLevel);
        m_downloadedLevel = level;
        m_localMessage = "Map downloaded. Confirm Ready when prepared.";
        render();
    }

    void levelDownloadFailed(int response) override {
        detachDownloadDelegate();
        m_downloadingLevelId = 0;
        m_localMessage = fmt::format("Map download failed ({}). Retry before Ready.", response);
        render();
    }

    void detachDownloadDelegate() {
        auto* manager = GameLevelManager::sharedState();
        if (manager && manager->m_levelDownloadDelegate == this) {
            manager->m_levelDownloadDelegate = nullptr;
        }
    }

    void onClose(CCObject* sender) override {
        if (RankedRuntime::get().view().stage == RuntimeStage::Queued) {
            RankedRuntime::get().leaveQueue();
        }
        detachDownloadDelegate();
        Popup::onClose(sender);
    }

public:
    static CorumRankedPopup* create() {
        auto* popup = new CorumRankedPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace

namespace corum::ranked {

void showRankedPopup() {
    if (auto* popup = CorumRankedPopup::create()) popup->show();
}

} // namespace corum::ranked
