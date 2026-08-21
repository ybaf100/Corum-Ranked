#include "RankedRuntime.hpp"
#include "domain/HudPresentation.hpp"
#include "domain/AttemptScoring.hpp"
#include "domain/RenderFpsMeter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

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

std::string formatScore(double value) {
    if (std::abs(value - std::round(value)) < 0.0005) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    auto text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}


double rankedProgressPercent(PlayLayer* layer) {
    if (!layer) return 0.0;
    auto const precise = std::clamp(static_cast<double>(layer->getCurrentPercent()), 0.0, 100.0);
    auto const whole = std::clamp(static_cast<double>(layer->getCurrentPercentInt()), 0.0, 100.0);
    return std::max(precise, whole);
}

} // namespace

class $modify(CorumRankedPlayLayer, PlayLayer) {
    struct Fields {
        CCNode* hudRoot = nullptr;
        CCLabelBMFont* fpsLabel = nullptr;
        CCLabelBMFont* ownScoreLabel = nullptr;
        std::array<CCSprite*, 2> ownChecks {nullptr, nullptr};
        CCLabelBMFont* ownAttemptLabel = nullptr;
        CCLabelBMFont* timerLabel = nullptr;
        CCLabelBMFont* stateLabel = nullptr;
        CCLabelBMFont* opponentScoreLabel = nullptr;
        std::array<CCSprite*, 2> opponentChecks {nullptr, nullptr};
        CCLabelBMFont* opponentAttemptLabel = nullptr;
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
        double qualifyingPercent = -1.0;
        bool rankedLevel = false;
        bool autoExitRequested = false;
        bool attemptStartReported = false;
        bool attemptEndReported = false;
        bool deniedDeathmatchVisualAttempt = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        m_fields->levelId = level ? static_cast<int>(level->m_levelID) : 0;
        m_fields->rankedLevel =
            runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
            runtime.isGameplayLevel(m_fields->levelId);
        if (m_fields->rankedLevel) {
            if (auto const qualifying = runtime.gameplayQualifyingPercent(m_fields->levelId)) {
                m_fields->qualifyingPercent = *qualifying;
            } else {
                log::warn(
                    "Ranked PlayLayer started without a Qualifying snapshot: level={}",
                    m_fields->levelId
                );
            }
        }
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return true;

        addRankedHud();
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        if (runtime.view().match.state == "DEATHMATCH_PLAYING") {
            // Reserve the visual try before gameplay begins. This local budget is
            // independent of HTTP acknowledgement latency and therefore prevents
            // a fourth visual Death Match attempt from appearing on fast resets.
            if (!runtime.reserveDeathmatchVisualAttempt()) {
                m_fields->deniedDeathmatchVisualAttempt = true;
                return true;
            }
        }
        m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        if (m_fields->deniedDeathmatchVisualAttempt && !m_fields->autoExitRequested) {
            m_fields->autoExitRequested = true;
            PlayLayer::onQuit();
            return;
        }

        auto const& matchState = runtime.view().match.state;
        auto const stillPlaying =
            matchState == "ROUND_PLAYING" ||
            matchState == "FINAL_ATTEMPT_WINDOW" ||
            matchState == "LAST_ATTEMPT_WINDOW" ||
            matchState == "ROUND_SETTLING" ||
            matchState == "DEATHMATCH_PLAYING";
        if ((!stillPlaying || runtime.currentLevelId() != m_fields->levelId) && !m_fields->autoExitRequested) {
            // alpha.10: Ranked never leaves a finished map waiting for the user.
            // Use Geometry Dash's normal PlayLayer quit path so scene/audio cleanup
            // remains owned by the game.
            m_fields->autoExitRequested = true;
            PlayLayer::onQuit();
            return;
        }

        m_fields->fpsMeter.observeFrame(steadyNowMicros());

        if (!runtime.isSpectating()) {
            if (!m_fields->attemptStartReported && !m_fields->attemptEndReported) {
                m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
            }
            runtime.reportAttemptProgress(
                m_fields->levelId,
                rankedProgressPercent(this)
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
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode) {
            // destroyPlayer() normally closes an attempt before the vanilla reset.
            // A manual restart/reset can bypass destroyPlayer(), so close it here
            // before creating the next visual attempt. This guarantees exactly one
            // end event for every start even when the user restarts while alive.
            if (!m_fields->attemptEndReported) {
                auto const progress = rankedProgressPercent(this);
                m_fields->attemptEndReported = runtime.reportAttemptEnd(m_fields->levelId, progress, false, m_fields->qualifyingPercent);
            }

            auto const& match = runtime.view().match;
            if (match.state == "DEATHMATCH_PLAYING") {
                // A reset creates the next *visual* attempt immediately, before the
                // prior /attempt/end request may be acknowledged. Reserve the next
                // slot locally first; after attempt 3 this returns false and exits.
                if (!runtime.reserveDeathmatchVisualAttempt()) {
                    if (!m_fields->autoExitRequested) {
                        m_fields->autoExitRequested = true;
                        PlayLayer::onQuit();
                    }
                    return;
                }
            }
        }
        PlayLayer::resetLevel();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode && !m_fields->attemptEndReported) {
            auto const progress = rankedProgressPercent(this);
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                progress,
                false,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode && !m_fields->attemptEndReported) {
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                100.0,
                true,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::levelComplete();
    }

    void showCompleteText() {
        // levelComplete() is the primary clear hook. Keep showCompleteText() as a
        // second vanilla completion-path safety net; the per-attempt guard makes
        // the pair idempotent and prevents double Clear/score accounting.
        if (m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode && !m_fields->attemptEndReported) {
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                100.0,
                true,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::showCompleteText();
    }

    void onQuit() {
        // A manual quit is still an attempt end. Without this hook a player could
        // leave/re-enter LevelInfoLayer while the server kept the old attempt open,
        // producing stale scores, orphan attempts, and broken Death Match counts.
        if (
            m_fields->rankedLevel && !m_isPracticeMode && !m_isTestMode &&
            !m_fields->attemptEndReported && !m_fields->autoExitRequested
        ) {
            auto const progress = rankedProgressPercent(this);
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                progress,
                false,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::onQuit();
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
        m_fields->ownAttemptLabel = label("", "bigFont.fnt", 0.24f, {0.0f, 1.0f}, {layout.topLeftX, top - 38.0f});
        m_fields->ownAttemptLabel->setVisible(false);
        m_fields->hudRoot->addChild(m_fields->ownAttemptLabel, 3);

        // The authoritative clock belongs in the visual center. alpha.15 makes
        // it larger and center-aligned so FINAL/LAST ATTEMPT timing is readable
        // without looking away from gameplay.
        m_fields->timerLabel = label("", "bigFont.fnt", 0.66f, {0.5f, 1.0f}, {size.width / 2.0f, top - 4.0f});
        m_fields->timerLabel->setID("ranked-countdown"_spr);
        m_fields->hudRoot->addChild(m_fields->timerLabel, 3);
        m_fields->stateLabel = label("", "goldFont.fnt", 0.40f, {0.5f, 1.0f}, {size.width / 2.0f, top - 32.0f});
        m_fields->stateLabel->setID("ranked-round-state"_spr);
        m_fields->hudRoot->addChild(m_fields->stateLabel, 3);

        m_fields->opponentScoreLabel = label("Score : 0", "bigFont.fnt", 0.31f, {1.0f, 1.0f}, {right, top - 18.0f});
        m_fields->opponentScoreLabel->setID("ranked-opponent-score"_spr);
        m_fields->hudRoot->addChild(m_fields->opponentScoreLabel, 3);
        m_fields->opponentChecks[0] = checkIcon({right - 21.0f, top - 42.0f});
        m_fields->opponentChecks[1] = checkIcon({right - 5.0f, top - 42.0f});
        for (auto* icon : m_fields->opponentChecks) m_fields->hudRoot->addChild(icon, 3);
        m_fields->opponentAttemptLabel = label("", "bigFont.fnt", 0.24f, {1.0f, 1.0f}, {right, top - 38.0f});
        m_fields->opponentAttemptLabel->setVisible(false);
        m_fields->hudRoot->addChild(m_fields->opponentAttemptLabel, 3);

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

        auto attemptsUsedA = match.deathmatchAttemptsUsedA;
        auto attemptsUsedB = match.deathmatchAttemptsUsedB;
        if (match.state == "DEATHMATCH_PLAYING") {
            auto const localVisualUsed = runtime.localDeathmatchVisualAttemptsUsed();
            if (match.side == "A") attemptsUsedA = std::max(attemptsUsedA, localVisualUsed);
            if (match.side == "B") attemptsUsedB = std::max(attemptsUsedB, localVisualUsed);
        }
        auto const presentation = corum::ranked::presentHud({
            .side = match.side,
            .state = match.state,
            .banner = match.banner,
            .scoreA = match.scoreA,
            .scoreB = match.scoreB,
            .clearsA = match.clearsA,
            .clearsB = match.clearsB,
            .deathmatch = match.state == "DEATHMATCH_PLAYING",
            .deathmatchAttemptsUsedA = attemptsUsedA,
            .deathmatchAttemptsUsedB = attemptsUsedB,
            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,
            .remainingMillis = runtime.deadlineMillis(),
            .renderFps = m_fields->fpsMeter.fps(),
            .spectatorActive = match.spectatorActive,
            .spectatorOpponentName = match.spectatorOpponentName,
            .spectatorCurrentProgress = match.spectatorCurrentProgress,
        });

        m_fields->fpsLabel->setString(presentation.fpsText.c_str());
        auto const liveProgress = m_fields->attemptEndReported
            ? -1.0
            : rankedProgressPercent(this);
        auto const ownLiveScore = runtime.localDisplayScore(
            liveProgress,
            m_fields->qualifyingPercent
        );
        m_fields->ownScoreLabel->setString(("Score : " + formatScore(ownLiveScore)).c_str());
        m_fields->opponentScoreLabel->setString(presentation.opponentScoreText.c_str());
        auto ownChecks = presentation.ownChecks;
        if (!presentation.deathmatch) ownChecks = corum::ranked::clearChecks(runtime.localDisplayClears());
        for (std::size_t index = 0; index < 2; ++index) {
            applyCheck(m_fields->ownChecks[index], ownChecks[index]);
            applyCheck(m_fields->opponentChecks[index], presentation.opponentChecks[index]);
            m_fields->ownChecks[index]->setVisible(!presentation.deathmatch);
            m_fields->opponentChecks[index]->setVisible(!presentation.deathmatch);
        }
        m_fields->ownAttemptLabel->setVisible(presentation.deathmatch);
        m_fields->opponentAttemptLabel->setVisible(presentation.deathmatch);
        if (presentation.deathmatch) {
            m_fields->ownAttemptLabel->setString(presentation.ownAttemptText.c_str());
            m_fields->opponentAttemptLabel->setString(presentation.opponentAttemptText.c_str());
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
        m_fields->timerLabel->setPosition({size.width / 2.0f, top - 4.0f});
        m_fields->stateLabel->setPosition({size.width / 2.0f, top - 32.0f});

        m_fields->spectatorPanel->setVisible(presentation.spectatorVisible);
        if (presentation.spectatorVisible) {
            m_fields->spectatorNameLabel->setString(presentation.spectatorOpponentText.c_str());
            m_fields->spectatorProgressLabel->setString(presentation.spectatorProgressText.c_str());
            m_fields->spectatorTimerLabel->setString(presentation.timerText.c_str());
        }
    }
};
