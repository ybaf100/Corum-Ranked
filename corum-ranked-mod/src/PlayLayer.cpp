#include "RankedRuntime.hpp"
#include "domain/HudPresentation.hpp"
#include "domain/RenderFpsMeter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>

using namespace geode::prelude;

namespace {

constexpr ccColor3B kPendingCheck = {128, 128, 128};
constexpr ccColor3B kApprovedCheck = {72, 224, 105};

std::int64_t steadyNowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

CCLabelBMFont* label(
    char const* text,
    char const* font,
    float scale,
    CCPoint anchor,
    CCPoint position
) {
    auto* result = CCLabelBMFont::create(text, font);
    result->setScale(scale);
    result->setAnchorPoint(anchor);
    result->setPosition(position);
    return result;
}

CCSprite* checkIcon(CCPoint position) {
    auto* result = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    result->setScale(0.34f);
    result->setPosition(position);
    result->setColor(kPendingCheck);
    result->setOpacity(220);
    return result;
}

void applyCheck(CCSprite* icon, corum::ranked::ClearCheckColor color) {
    if (!icon) return;
    icon->setColor(color == corum::ranked::ClearCheckColor::Green
        ? kApprovedCheck
        : kPendingCheck);
}

} // namespace

class $modify(CorumRankedPlayLayer, PlayLayer) {
    struct Fields {
        CCNode* hudRoot = nullptr;
        CCLabelBMFont* fpsLabel = nullptr;
        CCLabelBMFont* ownScoreLabel = nullptr;
        std::array<CCSprite*, 2> ownChecks {nullptr, nullptr};
        CCLabelBMFont* timerLabel = nullptr;
        CCLabelBMFont* stateLabel = nullptr;
        CCLabelBMFont* opponentScoreLabel = nullptr;
        std::array<CCSprite*, 2> opponentChecks {nullptr, nullptr};
        CCLabelBMFont* qualifyingLabel = nullptr;
        CCNode* spectatorPanel = nullptr;
        CCLabelBMFont* spectatorNameLabel = nullptr;
        CCLabelBMFont* spectatorProgressLabel = nullptr;
        CCLabelBMFont* spectatorTimerLabel = nullptr;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        CCLabelBMFont* debugLabel = nullptr;
#endif
        corum::ranked::RenderFpsMeter fpsMeter;
        std::chrono::steady_clock::time_point nextHudRefreshAt {};
        std::uint64_t renderedRevision = 0;
        int levelId = 0;
        bool rankedLevel = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        m_fields->levelId = level ? static_cast<int>(level->m_levelID) : 0;
        m_fields->rankedLevel =
            runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
            runtime.currentLevelId() == m_fields->levelId;
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return true;

        addRankedHud();
        runtime.reportAttemptStart(m_fields->levelId);
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        m_fields->fpsMeter.observeFrame(steadyNowMicros());

        if (!runtime.isSpectating()) {
            runtime.reportAttemptProgress(
                m_fields->levelId,
                std::clamp(static_cast<double>(getCurrentPercent()), 0.0, 100.0)
            );
        }
        auto const now = std::chrono::steady_clock::now();
        if (
            runtime.view().revision != m_fields->renderedRevision ||
            now >= m_fields->nextHudRefreshAt
        ) {
            updateRankedHud();
            m_fields->nextHudRefreshAt = now + std::chrono::milliseconds(100);
        }
    }

    void resetLevel() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode &&
            runtime.isSpectating()
        ) {
            return;
        }
        PlayLayer::resetLevel();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        runtime.reportAttemptStart(m_fields->levelId);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode) {
            auto const progress = std::clamp(static_cast<double>(getCurrentPercent()), 0.0, 100.0);
            corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                progress,
                false
            );
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode) {
            corum::ranked::RankedRuntime::get().reportAttemptEnd(m_fields->levelId, 100.0, true);
        }
        PlayLayer::levelComplete();
    }

    void togglePracticeMode(bool practiceMode) {
        if (m_fields->rankedLevel && practiceMode) {
            FLAlertLayer::create(
                "Corum Ranked",
                "Practice Mode is disabled during an active Ranked round.",
                "OK"
            )->show();
            return;
        }
        PlayLayer::togglePracticeMode(practiceMode);
    }

    void addRankedHud() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        auto const top = layout.topY;
        auto const right = layout.topRightX;

        m_fields->hudRoot = CCNode::create();
        m_fields->hudRoot->setID("ranked-play-hud"_spr);
        m_fields->hudRoot->setAnchorPoint({0.0f, 0.0f});
        m_fields->hudRoot->setPosition({0.0f, 0.0f});
        m_fields->hudRoot->setContentSize(size);

        m_fields->fpsLabel = label("FPS : -", "bigFont.fnt", 0.28f, {0.0f, 1.0f}, {layout.topLeftX, top});
        m_fields->fpsLabel->setID("ranked-fps"_spr);
        m_fields->hudRoot->addChild(m_fields->fpsLabel, 3);

        m_fields->ownScoreLabel = label("Score : 0", "bigFont.fnt", 0.31f, {0.0f, 1.0f}, {layout.topLeftX, top - 18.0f});
        m_fields->ownScoreLabel->setID("ranked-own-score"_spr);
        m_fields->hudRoot->addChild(m_fields->ownScoreLabel, 3);
        m_fields->ownChecks[0] = checkIcon({layout.topLeftX + 5.0f, top - 42.0f});
        m_fields->ownChecks[1] = checkIcon({layout.topLeftX + 21.0f, top - 42.0f});
        for (auto* icon : m_fields->ownChecks) m_fields->hudRoot->addChild(icon, 3);

        m_fields->timerLabel = label("", "bigFont.fnt", 0.34f, {0.0f, 1.0f}, {layout.topLeftX, top - 54.0f});
        m_fields->timerLabel->setID("ranked-countdown"_spr);
        m_fields->hudRoot->addChild(m_fields->timerLabel, 3);
        m_fields->stateLabel = label("", "goldFont.fnt", 0.34f, {0.0f, 1.0f}, {layout.topLeftX, top - 72.0f});
        m_fields->stateLabel->setID("ranked-round-state"_spr);
        m_fields->hudRoot->addChild(m_fields->stateLabel, 3);

        m_fields->opponentScoreLabel = label("Score : 0", "bigFont.fnt", 0.31f, {1.0f, 1.0f}, {right, top - 18.0f});
        m_fields->opponentScoreLabel->setID("ranked-opponent-score"_spr);
        m_fields->hudRoot->addChild(m_fields->opponentScoreLabel, 3);
        m_fields->opponentChecks[0] = checkIcon({right - 21.0f, top - 42.0f});
        m_fields->opponentChecks[1] = checkIcon({right - 5.0f, top - 42.0f});
        for (auto* icon : m_fields->opponentChecks) m_fields->hudRoot->addChild(icon, 3);

        m_fields->qualifyingLabel = label("Qualifying : -", "bigFont.fnt", 0.28f, {0.0f, 0.0f}, {layout.bottomLeftX, layout.bottomY});
        m_fields->qualifyingLabel->setID("ranked-qualifying"_spr);
        m_fields->hudRoot->addChild(m_fields->qualifyingLabel, 3);

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_fields->debugLabel = label("BOT MATCH · DEBUG", "goldFont.fnt", 0.22f, {0.5f, 1.0f}, {size.width / 2.0f, top});
        m_fields->debugLabel->setColor(ccc3(255, 145, 105));
        m_fields->debugLabel->setVisible(corum::ranked::RankedRuntime::get().view().match.debug);
        m_fields->hudRoot->addChild(m_fields->debugLabel, 3);
#endif

        addSpectatorPanel(size);
        addChild(m_fields->hudRoot, 1000);
        updateRankedHud();
    }

    void addSpectatorPanel(CCSize const& size) {
        auto const width = std::min(190.0f, size.width - 40.0f);
        m_fields->spectatorPanel = CCNode::create();
        m_fields->spectatorPanel->setID("ranked-spectator-overlay"_spr);
        m_fields->spectatorPanel->setContentSize({width, 92.0f});
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        m_fields->spectatorPanel->setPosition({layout.spectatorCenterX, layout.spectatorCenterY});

        auto* background = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        background->setContentSize({width, 92.0f});
        background->setColor(ccc3(20, 27, 38));
        background->setOpacity(220);
        m_fields->spectatorPanel->addChild(background, 0);

        auto* waiting = label("WAITING FOR OPPONENT", "goldFont.fnt", 0.28f, {0.5f, 0.5f}, {0.0f, 32.0f});
        m_fields->spectatorPanel->addChild(waiting, 2);
        m_fields->spectatorNameLabel = label("OPPONENT", "bigFont.fnt", 0.27f, {0.5f, 0.5f}, {0.0f, 15.0f});
        m_fields->spectatorPanel->addChild(m_fields->spectatorNameLabel, 2);
        m_fields->spectatorProgressLabel = label("CURRENT : -", "bigFont.fnt", 0.42f, {0.5f, 0.5f}, {0.0f, -5.0f});
        m_fields->spectatorPanel->addChild(m_fields->spectatorProgressLabel, 2);
        auto* lastAttempt = label("LAST ATTEMPT", "goldFont.fnt", 0.25f, {0.5f, 0.5f}, {-22.0f, -29.0f});
        m_fields->spectatorPanel->addChild(lastAttempt, 2);
        m_fields->spectatorTimerLabel = label("-", "bigFont.fnt", 0.3f, {0.0f, 0.5f}, {34.0f, -29.0f});
        m_fields->spectatorPanel->addChild(m_fields->spectatorTimerLabel, 2);
        m_fields->spectatorPanel->setVisible(false);
        m_fields->hudRoot->addChild(m_fields->spectatorPanel, 10);
    }

    void updateRankedHud() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        m_fields->renderedRevision = runtime.view().revision;
        if (!m_fields->hudRoot || !m_fields->fpsLabel || !m_fields->ownScoreLabel) return;

        auto const presentation = corum::ranked::presentHud({
            .side = match.side,
            .state = match.state,
            .banner = match.banner,
            .scoreA = match.scoreA,
            .scoreB = match.scoreB,
            .clearsA = match.clearsA,
            .clearsB = match.clearsB,
            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,
            .remainingMillis = runtime.deadlineMillis(),
            .renderFps = m_fields->fpsMeter.fps(),
            .spectatorActive = match.spectatorActive,
            .spectatorOpponentName = match.spectatorOpponentName,
            .spectatorCurrentProgress = match.spectatorCurrentProgress,
        });

        m_fields->fpsLabel->setString(presentation.fpsText.c_str());
        m_fields->ownScoreLabel->setString(presentation.ownScoreText.c_str());
        m_fields->opponentScoreLabel->setString(presentation.opponentScoreText.c_str());
        for (std::size_t index = 0; index < 2; ++index) {
            applyCheck(m_fields->ownChecks[index], presentation.ownChecks[index]);
            applyCheck(m_fields->opponentChecks[index], presentation.opponentChecks[index]);
        }
        m_fields->qualifyingLabel->setString(presentation.qualifyingText.c_str());
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if (m_fields->debugLabel) m_fields->debugLabel->setVisible(match.debug);
#endif

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const top = corum::ranked::layoutHud(size.width, size.height).topY;
        m_fields->timerLabel->setString(presentation.timerText.c_str());
        m_fields->stateLabel->setString(presentation.stateText.c_str());
        m_fields->timerLabel->setVisible(!presentation.timerText.empty());
        m_fields->stateLabel->setVisible(!presentation.stateText.empty());
        m_fields->stateLabel->setPositionY(top - (presentation.windowStateFirst ? 54.0f : 72.0f));
        m_fields->timerLabel->setPositionY(top - (presentation.windowStateFirst ? 72.0f : 54.0f));

        m_fields->spectatorPanel->setVisible(presentation.spectatorVisible);
        if (presentation.spectatorVisible) {
            m_fields->spectatorNameLabel->setString(presentation.spectatorOpponentText.c_str());
            m_fields->spectatorProgressLabel->setString(presentation.spectatorProgressText.c_str());
            m_fields->spectatorTimerLabel->setString(presentation.timerText.c_str());
        }
    }
};
