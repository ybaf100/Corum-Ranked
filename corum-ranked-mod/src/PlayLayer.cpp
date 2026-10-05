#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "SpectatorLiveRuntime.hpp"
#include "spectator/SpectatorPlayerCapture.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#include "RankedLevelPreparation.hpp"
#endif
#include "domain/HudPresentation.hpp"
#include "domain/AttemptScoring.hpp"
#include "domain/RankedGameplayPolicy.hpp"
#include "domain/SpectatorLocalSafety.hpp"
#include "spectator/CorumRemotePlayer.hpp"
#include "spectator/CorumSpectatorCamera.hpp"
#include "spectator/SpectatorSceneBridge.hpp"
#include "domain/TransitionTrace.hpp"
#include "domain/RenderFpsMeter.hpp"
#include "ui/PlayerIcon.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"
#include "ui/PlacementGradeBadge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>

using namespace geode::prelude;

namespace {

constexpr ccColor3B kPendingCheck = {112, 128, 151};
constexpr ccColor3B kApprovedCheck = corum::ranked::ui::Green;

// Vanilla quit may clear PlayLayer::get() before saving. Keep the exact owning
// layer available ONLY during synchronous lifecycle calls (never by level ID).
thread_local PlayLayer* persistenceOwner = nullptr;
struct ScopedPersistenceOwner {
    PlayLayer* previous = persistenceOwner;
    explicit ScopedPersistenceOwner(PlayLayer* owner) { persistenceOwner = owner; }
    ~ScopedPersistenceOwner() { persistenceOwner = previous; }
};

struct ScopedRealGameplayEnd {
    bool& flag;
    bool previous;
    ScopedRealGameplayEnd(bool& value, bool realEnd) : flag(value), previous(value) {
        flag = previous || realEnd;
    }
    ~ScopedRealGameplayEnd() { flag = previous; }
};

std::int64_t steadyNowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

CCLabelBMFont* label(
    std::string const& text,
    char const* font,
    float scale,
    CCPoint anchor,
    CCPoint position
) {
    auto weight = corum::ranked::ui::FontWeight::SemiBold;
    auto const role = std::string_view(font);
    if (role == "black") weight = corum::ranked::ui::FontWeight::Black;
    else if (role == "goldFont.fnt") weight = corum::ranked::ui::FontWeight::ExtraBold;
    else if (role == "bigFont.fnt") weight = corum::ranked::ui::FontWeight::SemiBold;
    else if (role == "medium") weight = corum::ranked::ui::FontWeight::Medium;
    auto* result = corum::ranked::ui::label(
        text,
        scale,
        position,
        corum::ranked::ui::Text,
        weight
    );
    result->setAnchorPoint(anchor);
    return result;
}

CCSprite* checkIcon(CCPoint position) {
    auto* result = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    result->setScale(0.30f);
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

bool spectatorTransitionState(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING";
}

} // namespace

class $modify(CorumRankedPlayLayer, PlayLayer) {
public:
    struct Fields {
        CCNode* hudRoot = nullptr;
        CCLabelBMFont* fpsLabel = nullptr;
        CCLabelBMFont* ownNameLabel = nullptr;
        CCLabelBMFont* ownScoreCaption = nullptr;
        CCLabelBMFont* ownScoreLabel = nullptr;
        std::array<CCSprite*, 3> ownChecks {nullptr, nullptr, nullptr};
        CCLabelBMFont* ownAttemptLabel = nullptr;
        CCLabelBMFont* roundLabel = nullptr;
        CCLabelBMFont* timerLabel = nullptr;
        CCLabelBMFont* stateLabel = nullptr;
        CCLabelBMFont* opponentNameLabel = nullptr;
        CCLabelBMFont* opponentScoreCaption = nullptr;
        CCLabelBMFont* opponentScoreLabel = nullptr;
        std::array<CCSprite*, 3> opponentChecks {nullptr, nullptr, nullptr};
        CCLabelBMFont* opponentAttemptLabel = nullptr;
        CCLabelBMFont* qualifyingCaption = nullptr;
        CCLabelBMFont* qualifyingLabel = nullptr;
        corum::ranked::ui::PlacementGradeBadge* placementGrade = nullptr;
        CCNode* spectatorPanel = nullptr;
        CCLabelBMFont* spectatorNameLabel = nullptr;
        CCLabelBMFont* spectatorProgressLabel = nullptr;
        CCLabelBMFont* spectatorTimerLabel = nullptr;
        CCNode* opponentProgressGroup = nullptr;
        corum::ranked::ui::PlayerIcon* opponentProgressIcon = nullptr;
        CCLabelBMFont* opponentProgressLabel = nullptr;
        CCSprite* practiceIndicator = nullptr;
        std::array<CCLabelBMFont*, 2> placementTopLabels {};
        std::array<std::string, 2> placementTopRecordIds {};
        std::unordered_map<CCLabelBMFont*, CCLabelBMFont*> hudShadows;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        CCLabelBMFont* debugLabel = nullptr;
#endif
        corum::ranked::RenderFpsMeter fpsMeter;
        std::chrono::steady_clock::time_point nextHudRefreshAt {};
        std::chrono::steady_clock::time_point attemptJournalReadyAt {};
        std::uint64_t renderedRevision = 0;
        int levelId = 0;
        double qualifyingPercent = -1.0;
        bool rankedLevel = false;
        bool rankedInitialized = false;
        bool autoExitRequested = false;
        bool gameplayFrameObserved = false;
        bool attemptStartReported = false;
        bool attemptEndReported = false;
        bool deniedDeathmatchVisualAttempt = false;
        bool prematureResultLogged = false;
        bool rankedPractice = false;
        bool spectatorMode = false;
        bool spectatorPending = false;
        corum::ranked::SpectatorLocalSafety localSafety;
        bool suppressedDeathLogged = false;
        bool suppressedCompletionLogged = false;
        bool suppressedSaveLogged = false;
        bool safetyRetired = false;
        bool vanillaGameplayEndInProgress = false;
        corum::ranked::CorumRemotePlayer remotePlayer;
        corum::ranked::CorumSpectatorCamera spectatorCamera;
        corum::ranked::SpectatorSceneContext sceneContext;
        bool spectatorScenePrepared = false;
        bool spectatorPresentationRetired = false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool debugRecordScene = false, debugPlaybackScene = false, debugRecordHasFrame = false;
        CCLabelBMFont* debugSpectateHud = nullptr;
#endif
    };

    static void onModify(auto& self) {
        // Run the narrow guard before vanilla/other Corum lifecycle callbacks.
        for (auto name : {"PlayLayer::destroyPlayer", "PlayLayer::levelComplete",
                          "PlayLayer::checkForEnd", "PlayLayer::showCompleteText"}) {
            if (!self.setHookPriorityPre(name, Priority::First)) {
                log::warn("Failed to install spectator local safety priority: {}", name);
            }
        }
    }

    void syncSpectatorLocalSafety() {
        if (m_fields->safetyRetired) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) {
            if (m_fields->debugPlaybackScene && m_fields->localSafety.activate({
                .rankedScene = true, .testMode = m_isTestMode, .placement = false,
                .userPractice = false,
                .serverSpectator = corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId),
                .ownAttemptAlive = false,
            })) {
                m_fields->spectatorMode = true; m_fields->spectatorPending = false;
                if (m_player1) m_player1->setVisible(false);
                log::info("[DebugSpectate] server-authorized local dummy safety activated");
            }
            // RECORD is vanilla gameplay, TEST is the exact Phase-1 dummy.
            // Neither owns ANY Ranked Start/Progress/End/Practice authority.
            runtime.setSpectatorLocalTransportBlocked(this, true);
            return;
        }
#endif
        auto const ownAttemptAlive =
            m_fields->vanillaGameplayEndInProgress ||
            (m_fields->attemptStartReported && !m_fields->attemptEndReported) ||
            runtime.hasLocalGameplayAuthority();
        if (m_fields->localSafety.activate({
            .rankedScene = m_fields->rankedLevel,
            .testMode = m_isTestMode,
            .placement = runtime.isPlacement(),
            .userPractice = m_fields->rankedPractice || m_isPracticeMode,
            .serverSpectator = runtime.isSpectating(),
            .ownAttemptAlive = ownAttemptAlive,
        })) {
            m_fields->spectatorMode = true;
            m_fields->spectatorPending = false;
            if (m_player1) m_player1->setVisible(false);
            log::info("Spectator local safety activated: level={}", m_fields->levelId);
        }
        runtime.setSpectatorLocalTransportBlocked(
            this, m_fields->localSafety.blocksEvidence(m_fields->spectatorPending)
        );
    }

    bool blocksLocalEvidence() {
        syncSpectatorLocalSafety();
        return m_fields->localSafety.blocksEvidence(m_fields->spectatorPending);
    }

    bool suppressLocalCompletion() {
        if (!blocksLocalEvidence()) return false;
        if (!m_fields->suppressedCompletionLogged) {
            log::info("Local completion suppressed for spectator: level={}", m_fields->levelId);
            m_fields->suppressedCompletionLogged = true;
        }
        return true;
    }

    bool freezeLocalDummy(PlayerObject* player) {
        if (!player || player != m_player1) return false;
        syncSpectatorLocalSafety();
        auto const wasFrozen = m_fields->localSafety.frozen();
        auto const frozen = m_fields->localSafety.freezeAtCompletionBoundary(
            player && player == m_player1, rankedProgressPercent(this)
        );
        if (frozen && !wasFrozen) {
            log::info("Spectator local dummy frozen at 99% boundary: level={}", m_fields->levelId);
        }
        // Phase 3 freezes the exact dummy immediately once scene setup is done.
        // The independent Phase-1 99% boundary remains a backstop for setup and
        // large steps; neither path pauses the world, remote renderer or relay.
        return frozen || (m_fields->rankedInitialized && m_fields->localSafety.active());
    }

    bool blocksLevelPersistence(GJGameLevel* level) {
        syncSpectatorLocalSafety();
        // The original death/Clear/manual restart must finish saving its real
        // gameplay record even if its End ACK grants spectator permission. This
        // scoped exception can never be entered by an already-converted dummy.
        if (m_fields->vanillaGameplayEndInProgress && !m_fields->localSafety.active()) return false;
        auto const blocked = m_fields->localSafety.blockPersistence(
            level && level == m_level, m_fields->spectatorPending
        );
        if (blocked && !m_fields->suppressedSaveLogged) {
            log::info("Local progress persistence suppressed for spectator: level={}", m_fields->levelId);
            m_fields->suppressedSaveLogged = true;
        }
        return blocked;
    }

    bool scoringEnabled() {
        syncSpectatorLocalSafety();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) return false;
#endif
        return
            m_fields->rankedInitialized &&
            !m_fields->spectatorMode &&
            !m_fields->spectatorPending &&
            !corum::ranked::RankedRuntime::get().isSpectating() &&
            corum::ranked::rankedScoringEnabled(
                m_fields->rankedLevel,
                m_isTestMode,
                m_fields->rankedPractice,
                m_isPracticeMode
            );
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        m_fields->levelId = level ? static_cast<int>(level->m_levelID) : 0;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto& debug = corum::ranked::debug::DebugSpectateRuntime::get();
        if (debug.preparedLevel(m_fields->levelId) &&
            corum::ranked::RankedLevelPreparation::get().authorizes(level, debug.preparationContext())) {
            m_fields->debugRecordScene = debug.mode() == corum::ranked::debug::DebugSpectateMode::PreparingRecord;
            m_fields->debugPlaybackScene = debug.mode() == corum::ranked::debug::DebugSpectateMode::PreparingPlayback;
            debug.sceneEntered(m_fields->levelId);
        }
#endif
        m_fields->rankedLevel =
            runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
            runtime.isGameplayLevel(m_fields->levelId);
        m_fields->sceneContext = currentSpectatorSceneContext();
        // Establish authority BEFORE vanilla init's reset/attempt callbacks.
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) debug.exitScene();
#endif
            runtime.setSpectatorLocalTransportBlocked(this, false);
            return false;
        }
        m_fields->rankedInitialized = true;
        // Keep Match music alive throughout ROUND STARTING IN and LevelInfo.
        // Stop it only after vanilla has successfully created the real PlayLayer.
        if (m_fields->rankedLevel) {
            corum::ranked::RankedAudioManager::get().fadeOutForGameplay();
            log::info(
                "Ranked PlayLayer initialized through vanilla Play: match={} level={}",
                runtime.view().match.matchId,
                m_fields->levelId
            );
        }
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
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) {
            log::info("[DebugSpectate] PlayLayer entered level={}", m_fields->levelId);
            if (m_fields->debugRecordScene) log::info("[DebugSpectate] RECORDING active");
            auto const size = CCDirector::sharedDirector()->getWinSize();
            m_fields->debugSpectateHud = label(debug.status(), "medium", 0.28f, {0.5f, 0.5f}, {size.width / 2.0f, size.height - 26.0f});
            addChild(m_fields->debugSpectateHud, 100);
            // Same native initialization-reset settling window as Ranked Start.
            m_fields->attemptJournalReadyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            if (m_fields->debugPlaybackScene) schedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
            return true;
        }
#endif
        if (!m_fields->rankedLevel || m_isTestMode) return true;

        // Latch read-only authority during init, before any postUpdate/onQuit
        // callback can observe the new scene. A reconnecting spectator must not
        // emit even a transient Start/Progress/End/Clear lifecycle.
        syncSpectatorLocalSafety();
        m_fields->rankedPractice = !m_fields->spectatorMode && m_isPracticeMode;
        if (m_fields->spectatorMode && m_player1) m_player1->setVisible(false);
        if (m_fields->rankedPractice) runtime.reportPracticePresence(true);
        addRankedHud();
        m_fields->gameplayFrameObserved = false;
        m_fields->attemptJournalReadyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        // Scene-owned presentation heartbeat also runs after vanilla completion
        // has stopped gameplay postUpdate. It cannot create gameplay evidence.
        schedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
        return true;
    }

    corum::ranked::SpectatorSceneContext currentSpectatorSceneContext() const {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const& debug = corum::ranked::debug::DebugSpectateRuntime::get();
        if (debug.playbackLevel(corum::ranked::debug::kDebugSpectateLevelId)) {
            return {debug.contextId(), 1, 0, corum::ranked::debug::kDebugSpectateLevelId};
        }
#endif
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        return {match.matchId, match.roundNumber, match.deathmatchSequence, runtime.currentLevelId()};
    }

    bool watchableSpectatorScene() {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) {
            return corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId) &&
                m_fields->sceneContext == currentSpectatorSceneContext();
        }
#endif
        auto& runtime = corum::ranked::RankedRuntime::get();
        return corum::ranked::spectatorSceneWatchable(
            runtime.view().stage == corum::ranked::RuntimeStage::Matched,
            m_fields->sceneContext, currentSpectatorSceneContext(), runtime.view().match.state
        );
    }

    bool prepareSpectatorScene() {
        syncSpectatorLocalSafety();
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto spectatorAuthority = runtime.isSpectating();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) spectatorAuthority = corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId);
#endif
        if (!m_fields->rankedInitialized || m_fields->spectatorPresentationRetired || !m_fields->localSafety.active() ||
            !spectatorAuthority || !watchableSpectatorScene() ||
            m_fields->vanillaGameplayEndInProgress) return false;
        if (m_fields->spectatorScenePrepared) return true;
        ScopedPersistenceOwner persistence(this);
        // Capture the real viewport BEFORE vanilla reset clears completion/death
        // presentation. Waiting never follows the reset dummy or teleports to 0.
        m_fields->spectatorCamera.update(this, true, std::nullopt, std::nullopt);
        m_fields->spectatorScenePrepared = true;
        if (m_hasCompletedLevel || (m_player1 && m_player1->m_isDead)) {
            m_fields->localSafety.resetDummy();
            PlayLayer::resetLevel();
        }
        if (m_player1) {
            m_player1->stopAllActions();
            m_player1->setVisible(false);
        }
        log::info("Spectator mode activated in existing PlayLayer: match={} level={}",
            m_fields->sceneContext.matchId, m_fields->levelId);
        return true;
    }

    void driveSpectatorCamera() {
        if (!m_fields->spectatorScenePrepared) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto spectatorAuthority = runtime.isSpectating();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) spectatorAuthority = corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId);
#endif
        auto const active = !m_fields->spectatorPresentationRetired && !m_fields->autoExitRequested &&
            m_fields->localSafety.active() && spectatorAuthority && watchableSpectatorScene();
        auto const frame = corum::ranked::SpectatorLiveRuntime::get().presentationFrame();
        m_fields->spectatorCamera.update(this, active,
            frame ? std::optional(frame->context) : std::nullopt,
            m_fields->remotePlayer.cameraFrame());
    }

    void cleanupSpectatorPresentation() {
        // Release ownership before destroying its visual or native scene state.
        m_fields->spectatorCamera.cleanup(this);
        m_fields->remotePlayer.cleanup();
    }

    void updateSpectatorView(float dt) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto& live = corum::ranked::SpectatorLiveRuntime::get();
        live.tickPublisher({}, true);
        auto spectatorAuthority = runtime.isSpectating();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) spectatorAuthority = corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId);
#endif
        if (!spectatorAuthority || !watchableSpectatorScene()) {
            cleanupSpectatorPresentation();
            live.reset();
            if (!m_fields->autoExitRequested) {
                m_fields->autoExitRequested = true;
                log::info("Spectator mode exited: authority/context no longer watchable");
                if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
            }
            return;
        }
        if (!prepareSpectatorScene()) return;
        if (m_player1) m_player1->setVisible(false);
        (void)freezeLocalDummy(m_player1);
        live.tickViewer();
        m_fields->remotePlayer.update(
            this, spectatorAuthority && m_fields->localSafety.active(), live.presentationFrame()
        );
        driveSpectatorCamera();
        // Native postUpdate may be stopped by the genuine prior Clear. Continue
        // applying the native world camera without simulating a Ranked Attempt.
        GJBaseGameLayer::updateCamera(dt);
        auto const now = std::chrono::steady_clock::now();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) {
            if (m_fields->debugSpectateHud) {
                auto const waiting = live.status() == corum::ranked::SpectatorLiveStatus::Waiting;
                auto const fallback = live.status() == corum::ranked::SpectatorLiveStatus::Fallback;
                auto const complete = corum::ranked::debug::DebugSpectateRuntime::get().playbackComplete();
                m_fields->debugSpectateHud->setString(complete ? "SERVER PLAYBACK COMPLETE" :
                    waiting ? "LIVE SPECTATOR - WAITING FOR OPPONENT..." : fallback ? "LIVE SPECTATOR - POLLING..." : "LIVE SPECTATOR - VOICE");
            }
            return;
        }
#endif
        if (runtime.view().revision != m_fields->renderedRevision || now >= m_fields->nextHudRefreshAt) {
            updateRankedHud();
            m_fields->nextHudRefreshAt = now + std::chrono::milliseconds(100);
        }
    }

    void spectatorFrame(float dt) {
        if (!m_fields->rankedInitialized || m_fields->safetyRetired ||
            m_fields->spectatorPresentationRetired || m_fields->autoExitRequested ||
            m_isTestMode || m_fields->vanillaGameplayEndInProgress) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_fields->spectatorMode && !m_fields->spectatorPending &&
            !m_fields->attemptEndReported && !runtime.isSpectating()) return;
        runtime.tick();
        syncSpectatorLocalSafety();
        if (m_fields->spectatorMode) {
            updateSpectatorView(dt);
            return;
        }
        // An ended terminal Attempt waits for authority in THIS scene. Ordinary
        // deaths with another legal Attempt, Placement and user Practice do not.
        if (m_fields->attemptEndReported && !runtime.hasLocalGameplayAuthority() &&
            !runtime.canEnterCurrentLevel() && !runtime.isPlacement() &&
            !m_fields->rankedPractice && !m_isPracticeMode && watchableSpectatorScene()) {
            m_fields->spectatorPending = true;
            syncSpectatorLocalSafety();
        }
        // Completion overlays own their canonical result exit (onMenu); don't
        // tear out the PlayLayer from underneath an interactive overlay.
        if (m_fields->spectatorPending && !watchableSpectatorScene() && !m_hasCompletedLevel) {
            m_fields->autoExitRequested = true;
            onQuit();
        }
    }

    void postUpdate(float dt) {
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
        PlayLayer::postUpdate(dt);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene) {
            if (m_player1 && !m_player1->m_isDead && !m_hasCompletedLevel && !m_isPracticeMode &&
                std::chrono::steady_clock::now() >= m_fields->attemptJournalReadyAt) {
                corum::ranked::debug::DebugSpectateRuntime::get().recordPose(corum::ranked::captureSpectatorPose(m_player1, false, false, m_gameState.m_isDualMode));
                m_fields->debugRecordHasFrame = true;
            }
            if (m_fields->debugSpectateHud) m_fields->debugSpectateHud->setString(corum::ranked::debug::DebugSpectateRuntime::get().status().c_str());
            return;
        }
#endif
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        syncSpectatorLocalSafety();
        if (!m_fields->rankedLevel || m_isTestMode) return;
        if (!m_fields->gameplayFrameObserved) {
            corum::ranked::TransitionTrace::get().mark("first-gameplay-frame");
        }
        m_fields->gameplayFrameObserved = true;
        auto const now = std::chrono::steady_clock::now();
        m_fields->fpsMeter.observeFrame(steadyNowMicros());

        if (m_fields->spectatorMode) {
            // Presentation heartbeat owns viewer updates even when genuine
            // completion has stopped vanilla gameplay updates.
            return;
        }

        cleanupSpectatorPresentation();
        if (m_fields->spectatorPending) {
            auto const& matchState = runtime.view().match.state;
            if (!spectatorTransitionState(matchState) || runtime.currentLevelId() != m_fields->levelId) {
                if (!m_fields->autoExitRequested) {
                    m_fields->autoExitRequested = true;
                    PlayLayer::onQuit();
                }
                return;
            }
            if (m_player1) m_player1->setVisible(false);
            if (
                runtime.view().revision != m_fields->renderedRevision ||
                now >= m_fields->nextHudRefreshAt
            ) {
                updateRankedHud();
                m_fields->nextHudRefreshAt = now + std::chrono::milliseconds(100);
            }
            return;
        }

        if (m_fields->rankedPractice || m_isPracticeMode) {
            corum::ranked::SpectatorLiveRuntime::get().tickPublisher({}, true);
            // Practice is local-only and non-scoring. Polling deliberately keeps
            // running so opponent progress and the authoritative clock continue.
            if (!runtime.canEnterCurrentLevel() && !m_fields->autoExitRequested) {
                m_fields->autoExitRequested = true;
                PlayLayer::onQuit();
                return;
            }
            if (
                runtime.view().revision != m_fields->renderedRevision ||
                now >= m_fields->nextHudRefreshAt
            ) {
                updateRankedHud();
                m_fields->nextHudRefreshAt = now + std::chrono::milliseconds(100);
            }
            return;
        }

        // Journal the visual Start only after Geometry Dash's short scene setup
        // window. The game performs an initialization reset roughly 100-150 ms
        // after PlayLayer construction on the supplied devices; reporting from
        // init() turned it into Start -> End 0 -> Start. A real death/Clear before
        // this point still self-heals through reportAttemptEnd(), preserving it.
        if (
            !runtime.isSpectating() &&
            !m_fields->attemptStartReported && !m_fields->attemptEndReported &&
            now >= m_fields->attemptJournalReadyAt
        ) {
            m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
        }
        if (m_fields->deniedDeathmatchVisualAttempt && !m_fields->autoExitRequested) {
            m_fields->autoExitRequested = true;
            PlayLayer::onQuit();
            return;
        }

        if (m_player1) {
            corum::ranked::SpectatorLiveRuntime::get().tickPublisher(
                corum::ranked::captureSpectatorPose(m_player1, false, false, m_gameState.m_isDualMode), false);
        }

        auto const& matchState = runtime.view().match.state;
        auto const stillPlaying =
            matchState == "ROUND_PLAYING" ||
            matchState == "FINAL_ATTEMPT_WINDOW" ||
            matchState == "LAST_ATTEMPT_WINDOW" ||
            matchState == "ROUND_SETTLING" ||
            matchState == "DEATHMATCH_PLAYING" ||
            matchState == "MAP_PLAYING" ||
            matchState == "MAP_SETTLING";
        if ((!stillPlaying || runtime.currentLevelId() != m_fields->levelId) && !m_fields->autoExitRequested) {
            auto const localAttemptStillAlive =
                m_fields->attemptStartReported && !m_fields->attemptEndReported;
            if (localAttemptStillAlive && matchState != "CANCELLED") {
                // FINAL/LAST ATTEMPT is a start deadline, never an end deadline.
                // Even if a stale/early server result leaks through, do not call
                // onQuit() on a visual attempt that has not naturally died or
                // cleared yet. Server reconciliation should normally keep the
                // state in ROUND_SETTLING until this attempt ends.
                if (!m_fields->prematureResultLogged) {
                    log::error(
                        "Ignoring premature Ranked state transition while visual attempt is alive: state={} level={}",
                        matchState,
                        m_fields->levelId
                    );
                    m_fields->prematureResultLogged = true;
                }
            } else {
                if (!m_fields->attemptEndReported && runtime.hasLocalAttemptInFlight()) {
                    auto const progress = rankedProgressPercent(this);
                    m_fields->attemptEndReported = runtime.reportAttemptEnd(
                        m_fields->levelId,
                        progress,
                        false,
                        m_fields->qualifyingPercent
                    );
                }
                m_fields->autoExitRequested = true;
                PlayLayer::onQuit();
                return;
            }
        }

        runtime.reportAttemptProgress(
            m_fields->levelId,
            rankedProgressPercent(this)
        );
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
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && m_fields->debugRecordHasFrame) {
            corum::ranked::debug::DebugSpectateRuntime::get().cancelRecording();
        }
#endif
        if (blocksLocalEvidence()) {
            // Once converted, manual/automatic Retry must not restart even a
            // dummy simulation or destroy the acquired spectator viewport.
            if (m_fields->spectatorScenePrepared) return;
            cleanupSpectatorPresentation();
            m_fields->localSafety.resetDummy();
            PlayLayer::resetLevel();
            if (m_player1) m_player1->setVisible(false);
            return;
        }
        if (
            scoringEnabled() &&
            (
                !m_fields->gameplayFrameObserved ||
                (
                    !m_fields->attemptStartReported && !m_fields->attemptEndReported &&
                    std::chrono::steady_clock::now() < m_fields->attemptJournalReadyAt
                )
            )
        ) {
            // Vanilla's scene-initialization reset is not a player attempt. Let
            // Geometry Dash finish setup, then the stable postUpdate path creates
            // exactly one Ranked Start. Real early deaths/Clears have already set
            // attemptEndReported through their dedicated hooks and do not enter here.
            PlayLayer::resetLevel();
            return;
        }
        auto const resetActions = corum::ranked::rankedResetActions(
            scoringEnabled(),
            m_fields->attemptEndReported,
            runtime.canEnterCurrentLevel()
        );
        ScopedRealGameplayEnd finishing(m_fields->vanillaGameplayEndInProgress, resetActions.endAttempt);
        if (resetActions.endAttempt) {
            // destroyPlayer() normally closes an attempt before the vanilla reset.
            // A manual restart/reset can bypass destroyPlayer(), so close it here
            // before creating the next visual attempt. This guarantees exactly one
            // end event for every start even when the user restarts while alive.
            auto const progress = rankedProgressPercent(this);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId,
                progress,
                false,
                m_fields->qualifyingPercent
            );
        }
        if (resetActions.exitLocalGameplay) {
            // Hold the canonical PlayLayer while the just-ended terminal Attempt
            // is being reflected by the server. If this side becomes an eligible
            // spectator the same scene is reused; otherwise the next authoritative
            // result poll exits normally. The hidden local avatar cannot create a
            // new Attempt during this handoff.
            if (
                !runtime.isPlacement() &&
                spectatorTransitionState(runtime.view().match.state)
            ) {
                m_fields->spectatorPending = true;
                syncSpectatorLocalSafety();
                PlayLayer::resetLevel();
                if (m_player1) m_player1->setVisible(false);
                return;
            }
            if (!m_fields->autoExitRequested) {
                m_fields->autoExitRequested = true;
                PlayLayer::onQuit();
            }
            return;
        }
        PlayLayer::resetLevel();
        if (!resetActions.startFreshAttempt) return;
        corum::ranked::SpectatorLiveRuntime::get().localAttemptStarted();
        m_fields->spectatorPending = false;
        if (m_player1) m_player1->setVisible(true);
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
        if (m_fields->localSafety.suppressDeath(player && player == m_player1)) {
            if (!m_fields->suppressedDeathLogged) {
                log::info("Local death suppressed for spectator: level={}", m_fields->levelId);
                m_fields->suppressedDeathLogged = true;
            }
            return;
        }
        ScopedRealGameplayEnd finishing(
            m_fields->vanillaGameplayEndInProgress,
            scoringEnabled() && !m_fields->attemptEndReported
        );
        // Geometry Dash deliberately calls destroyPlayer() for its internal
        // anti-cheat verification spike and then returns without killing the
        // player. Treating that callback as a real death closes the Ranked
        // attempt near 0%, after which every real progress/death/clear event is
        // ignored. This was the root cause of the human side staying at Score 0
        // while the server-driven Debug Bot continued scoring normally.
        if (
            object && m_anticheatSpike &&
            object->m_uniqueID == m_anticheatSpike->m_uniqueID
        ) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        if (scoringEnabled() && !m_fields->attemptEndReported) {
            if (m_player1) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(
                corum::ranked::captureSpectatorPose(m_player1, true, false, m_gameState.m_isDualMode), false);
            auto const progress = rankedProgressPercent(this);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId,
                progress,
                false,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::destroyPlayer(player, object);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && player && player == m_player1 && player->m_isDead) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(corum::ranked::captureSpectatorPose(player, true, false, m_gameState.m_isDualMode), true);
        }
#endif
    }

    void levelComplete() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (suppressLocalCompletion()) return;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && m_player1) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(corum::ranked::captureSpectatorPose(m_player1, false, true, m_gameState.m_isDualMode), true);
        }
#endif
        ScopedRealGameplayEnd finishing(
            m_fields->vanillaGameplayEndInProgress,
            scoringEnabled() && !m_fields->attemptEndReported
        );
        if (scoringEnabled() && !m_fields->attemptEndReported) {
            if (m_player1) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(
                corum::ranked::captureSpectatorPose(m_player1, false, true, m_gameState.m_isDualMode), false);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId,
                100.0,
                true,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::levelComplete();
    }

    void showCompleteText() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (suppressLocalCompletion()) return;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && m_player1) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(corum::ranked::captureSpectatorPose(m_player1, false, true, m_gameState.m_isDualMode), true);
        }
#endif
        ScopedRealGameplayEnd finishing(
            m_fields->vanillaGameplayEndInProgress,
            scoringEnabled() && !m_fields->attemptEndReported
        );
        // levelComplete() is the primary clear hook. Keep showCompleteText() as a
        // second vanilla completion-path safety net; the per-attempt guard makes
        // the pair idempotent and prevents double Clear/score accounting.
        if (scoringEnabled() && !m_fields->attemptEndReported) {
            if (m_player1) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(
                corum::ranked::captureSpectatorPose(m_player1, false, true, m_gameState.m_isDualMode), false);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId,
                100.0,
                true,
                m_fields->qualifyingPercent
            );
        }
        PlayLayer::showCompleteText();
    }

    void onQuit() {
        m_fields->spectatorPresentationRetired = true;
        unschedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
        cleanupSpectatorPresentation();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) corum::ranked::debug::DebugSpectateRuntime::get().exitScene();
#endif
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
        ScopedRealGameplayEnd finishing(
            m_fields->vanillaGameplayEndInProgress,
            scoringEnabled() && !m_fields->attemptEndReported
        );
        // A manual quit is still an attempt end. Without this hook a player could
        // leave/re-enter LevelInfoLayer while the server kept the old attempt open,
        // producing stale scores, orphan attempts, and broken Death Match counts.
        if (m_fields->spectatorMode || m_fields->spectatorPending) {
            corum::ranked::SpectatorLiveRuntime::get().reset();
        }
        if (!blocksLocalEvidence() && (m_fields->rankedPractice || m_isPracticeMode)) {
            corum::ranked::RankedRuntime::get().reportPracticePresence(false);
        }
        if (
            scoringEnabled() &&
            !m_fields->attemptEndReported && !m_fields->autoExitRequested &&
            !m_fields->spectatorMode && !m_fields->spectatorPending
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

    void onExit() {
        m_fields->spectatorPresentationRetired = true;
        unschedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
        ScopedPersistenceOwner persistence(this);
        cleanupSpectatorPresentation();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene) corum::ranked::debug::DebugSpectateRuntime::get().exitScene();
#endif
        if (m_fields->spectatorMode || m_fields->spectatorPending) {
            corum::ranked::SpectatorLiveRuntime::get().reset();
        }
        PlayLayer::onExit();
        corum::ranked::RankedRuntime::get().setSpectatorLocalTransportBlocked(this, false);
        m_fields->safetyRetired = true;
        if (m_fields->localSafety.active()) {
            log::info("Spectator local safety deactivated: level={}", m_fields->levelId);
        }
        // Keep the scene latch until destruction, including delayed quit saves.
    }

    void checkForEnd() {
        if (suppressLocalCompletion()) return;
        PlayLayer::checkForEnd();
    }

    void playEndAnimationToPos(CCPoint position) {
        if (suppressLocalCompletion()) return;
        PlayLayer::playEndAnimationToPos(position);
    }

    void playPlatformerEndAnimationToPos(CCPoint position, bool instant) {
        if (suppressLocalCompletion()) return;
        PlayLayer::playPlatformerEndAnimationToPos(position, instant);
    }

    void showEndLayer() {
        if (suppressLocalCompletion()) return;
        PlayLayer::showEndLayer();
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (blocksLocalEvidence()) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void togglePracticeMode(bool practiceMode) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (blocksLocalEvidence()) {
            // Spectator scenes are read-only. Practice cannot alter the remote
            // player or create a local scoring lifecycle.
            return;
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene) corum::ranked::debug::DebugSpectateRuntime::get().cancelRecording();
#endif
        auto const transition = corum::ranked::practiceTransition({
            .rankedLevel = m_fields->rankedLevel,
            .testMode = m_isTestMode,
            .rankedPractice = m_fields->rankedPractice,
            .requestedPractice = practiceMode,
            .canStartFreshAttempt = runtime.canEnterCurrentLevel(),
        });
        if (transition == corum::ranked::RankedPracticeTransition::None) {
            PlayLayer::togglePracticeMode(practiceMode);
            return;
        }

        if (transition == corum::ranked::RankedPracticeTransition::Enter) {
            // Practice starts by closing the current normal Attempt once with
            // ordinary death semantics. Vanilla practice setup runs only after
            // the local transport suppression flag is active.
            if (scoringEnabled() && !m_fields->attemptEndReported) {
                auto const endQueued = runtime.reportAttemptEnd(
                    m_fields->levelId,
                    rankedProgressPercent(this),
                    false,
                    m_fields->qualifyingPercent
                );
                (void)endQueued;
                m_fields->attemptEndReported = true;
            }
            m_fields->rankedPractice = true;
            runtime.reportPracticePresence(true);
            corum::ranked::SpectatorLiveRuntime::get().tickPublisher({}, true);
            PlayLayer::togglePracticeMode(true);
            updateRankedHud();
            return;
        }

        // Vanilla removes checkpoints and resets while Ranked transport remains
        // suppressed. The normal postUpdate journal then starts a fresh Attempt
        // from the reset position, never from Practice progress.
        PlayLayer::togglePracticeMode(false);
        runtime.reportPracticePresence(false);
        if (transition == corum::ranked::RankedPracticeTransition::LeaveAndExit) {
            m_fields->autoExitRequested = true;
            PlayLayer::onQuit();
            return;
        }
        m_fields->rankedPractice = false;
        m_fields->gameplayFrameObserved = false;
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        m_fields->deniedDeathmatchVisualAttempt = false;
        m_fields->prematureResultLogged = false;
        m_fields->attemptJournalReadyAt =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        updateRankedHud();
    }

    CCLabelBMFont* addShadowedHudLabel(
        CCNode* parent,
        std::string const& text,
        char const* fontRole,
        float scale,
        CCPoint anchor,
        CCPoint position,
        ccColor3B color,
        int zOrder = 3
    ) {
        auto const kShadowOffset = CCPoint{0.75f, -0.75f};
        auto* shadow = label(
            text,
            fontRole,
            scale,
            anchor,
            {position.x + kShadowOffset.x, position.y + kShadowOffset.y}
        );
        shadow->setColor({0, 0, 0});
        shadow->setOpacity(205);
        parent->addChild(shadow, zOrder - 1);

        auto* foreground = label(text, fontRole, scale, anchor, position);
        foreground->setColor(color);
        parent->addChild(foreground, zOrder);
        m_fields->hudShadows.emplace(foreground, shadow);
        return foreground;
    }

    CCLabelBMFont* hudShadow(CCLabelBMFont* foreground) {
        if (!foreground) return nullptr;
        auto const found = m_fields->hudShadows.find(foreground);
        return found == m_fields->hudShadows.end() ? nullptr : found->second;
    }

    void setHudText(CCLabelBMFont* foreground, std::string const& text) {
        if (!foreground) return;
        foreground->setString(text.c_str());
        if (auto* shadow = hudShadow(foreground)) shadow->setString(text.c_str());
    }

    void setHudVisible(CCLabelBMFont* foreground, bool visible) {
        if (!foreground) return;
        foreground->setVisible(visible);
        if (auto* shadow = hudShadow(foreground)) shadow->setVisible(visible);
    }

    void setHudPosition(CCLabelBMFont* foreground, CCPoint position) {
        if (!foreground) return;
        foreground->setPosition(position);
        if (auto* shadow = hudShadow(foreground)) {
            shadow->setPosition({position.x + 0.75f, position.y - 0.75f});
        }
    }

    void limitHudWidth(CCLabelBMFont* foreground, float width, float maximumScale, float minimumScale) {
        if (!foreground) return;
        foreground->limitLabelWidth(width, maximumScale, minimumScale);
        if (auto* shadow = hudShadow(foreground)) {
            shadow->limitLabelWidth(width, maximumScale, minimumScale);
        }
    }

    void addRankedHud() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        auto const top = layout.topY;
        auto const left = layout.topLeftX + 8.0f;
        auto const right = layout.topRightX - 8.0f;

        m_fields->hudRoot = CCNode::create();
        m_fields->hudRoot->setID("ranked-play-hud"_spr);
        m_fields->hudRoot->setAnchorPoint({0.0f, 0.0f});
        m_fields->hudRoot->setPosition({0.0f, 0.0f});
        m_fields->hudRoot->setContentSize(size);

        // Gameplay stays visually open: no cards or rectangular washes. A tight
        // one-pixel text shadow gives Paperlogy enough contrast over bright maps
        // without covering hazards or level art.
        m_fields->fpsLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "FPS : -",
            "bigFont.fnt",
            0.51f,
            {0.0f, 1.0f},
            {7.0f, top - 0.5f},
            corum::ranked::ui::Text
        );
        m_fields->fpsLabel->setID("ranked-fps"_spr);

        m_fields->ownNameLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            corum::ranked::i18n::text("you", "YOU"),
            "bigFont.fnt",
            0.51f,
            {0.0f, 1.0f},
            {left, top - 4.0f},
            corum::ranked::ui::Cyan
        );
        limitHudWidth(m_fields->ownNameLabel, 96.0f, 0.51f, 0.21f);

        m_fields->ownScoreCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            corum::ranked::i18n::text("score", "SCORE"),
            "bigFont.fnt",
            0.51f,
            {0.0f, 1.0f},
            {left, top - 22.0f},
            corum::ranked::ui::Text
        );
        m_fields->ownScoreLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "0",
            "black",
            0.714f,
            {0.0f, 1.0f},
            {left + 82.0f, top - 20.0f},
            corum::ranked::ui::Text
        );
        m_fields->ownScoreLabel->setID("ranked-own-score"_spr);
        limitHudWidth(m_fields->ownScoreLabel, 76.0f, 0.714f, 0.26f);
        m_fields->ownChecks[0] = checkIcon({left + 8.0f, top - 36.0f});
        m_fields->ownChecks[1] = checkIcon({left + 25.0f, top - 36.0f});
        m_fields->ownChecks[2] = checkIcon({left + 42.0f, top - 36.0f});
        for (auto* icon : m_fields->ownChecks) m_fields->hudRoot->addChild(icon, 3);
        m_fields->ownAttemptLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.476f,
            {0.0f, 1.0f},
            {left, top - 40.0f},
            corum::ranked::ui::Gold
        );
        setHudVisible(m_fields->ownAttemptLabel, false);

        m_fields->roundLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.527f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 1.0f},
            corum::ranked::ui::Gold
        );
        m_fields->roundLabel->setID("ranked-current-round"_spr);

        // Keep the authoritative clock directly below the current round.
        m_fields->timerLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "black",
            0.60f * 1.70f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 18.0f},
            corum::ranked::ui::Text
        );
        m_fields->timerLabel->setID("ranked-countdown"_spr);
        m_fields->stateLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.442f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 47.0f},
            corum::ranked::ui::Gold
        );
        m_fields->stateLabel->setID("ranked-round-state"_spr);

        m_fields->opponentNameLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            corum::ranked::i18n::text("opponent", "OPPONENT"),
            "bigFont.fnt",
            0.51f,
            {1.0f, 1.0f},
            {right, top - 4.0f},
            corum::ranked::ui::Red
        );
        limitHudWidth(m_fields->opponentNameLabel, 96.0f, 0.51f, 0.21f);

        m_fields->opponentScoreCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            corum::ranked::i18n::text("score", "SCORE"),
            "bigFont.fnt",
            0.51f,
            {1.0f, 1.0f},
            {right - 88.0f, top - 22.0f},
            corum::ranked::ui::Text
        );
        m_fields->opponentScoreLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "0",
            "black",
            0.714f,
            {1.0f, 1.0f},
            {right, top - 20.0f},
            corum::ranked::ui::Text
        );
        m_fields->opponentScoreLabel->setID("ranked-opponent-score"_spr);
        limitHudWidth(m_fields->opponentScoreLabel, 76.0f, 0.714f, 0.26f);
        m_fields->opponentChecks[0] = checkIcon({right - 42.0f, top - 36.0f});
        m_fields->opponentChecks[1] = checkIcon({right - 25.0f, top - 36.0f});
        m_fields->opponentChecks[2] = checkIcon({right - 8.0f, top - 36.0f});
        for (auto* icon : m_fields->opponentChecks) m_fields->hudRoot->addChild(icon, 3);
        m_fields->opponentAttemptLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.476f,
            {1.0f, 1.0f},
            {right, top - 40.0f},
            corum::ranked::ui::Gold
        );
        setHudVisible(m_fields->opponentAttemptLabel, false);

        m_fields->qualifyingCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            "Qualifying",
            "bigFont.fnt",
            0.51f,
            {0.0f, 0.0f},
            {layout.bottomLeftX + 4.0f, layout.bottomY + 4.0f},
            corum::ranked::ui::Text
        );
        m_fields->qualifyingLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "-",
            "black",
            0.714f,
            {0.0f, 0.0f},
            {layout.bottomLeftX + 112.0f, layout.bottomY + 1.0f},
            corum::ranked::ui::Text
        );
        m_fields->qualifyingLabel->setID("ranked-qualifying"_spr);
        limitHudWidth(m_fields->qualifyingLabel, 96.0f, 0.714f, 0.28f);

        if (corum::ranked::RankedRuntime::get().isPlacement()) {
            setHudVisible(m_fields->qualifyingCaption, false);
            setHudVisible(m_fields->qualifyingLabel, false);
            for (std::size_t rank = 0; rank < m_fields->placementTopLabels.size(); ++rank) {
                auto const text = rank == 0 ? "1st - -" : "2nd - -";
                m_fields->placementTopLabels[rank] = addShadowedHudLabel(
                    m_fields->hudRoot,
                    text,
                    "black",
                    0.36f,
                    {1.0f, 0.5f},
                    {size.width - 10.0f, top - 4.0f - static_cast<float>(rank) * 15.0f},
                    corum::ranked::ui::Text,
                    5
                );
            }
            m_fields->placementGrade = corum::ranked::ui::PlacementGradeBadge::create(38.0f);
            if (m_fields->placementGrade) {
                m_fields->placementGrade->setID("ranked-placement-grade"_spr);
                m_fields->placementGrade->setPosition({
                    layout.bottomLeftX + 22.0f,
                    layout.bottomY + 21.0f,
                });
                m_fields->placementGrade->setGrade(
                    corum::ranked::RankedRuntime::get().view().placement.currentMapGrade,
                    false
                );
                m_fields->hudRoot->addChild(m_fields->placementGrade, 5);
            }
        }

        if (!corum::ranked::RankedRuntime::get().isPlacement()) {
            auto const& match = corum::ranked::RankedRuntime::get().view().match;
            auto const ownIsA = match.side == "A";
            auto const opponentAccountId = ownIsA
                ? match.playerBAccountId
                : match.playerAAccountId;
            m_fields->opponentProgressGroup = CCNode::create();
            m_fields->opponentProgressGroup->setID("ranked-opponent-progress-hud"_spr);
            m_fields->opponentProgressGroup->setPosition({
                layout.bottomRightX - 42.0f,
                layout.bottomY + 14.0f,
            });
            m_fields->opponentProgressIcon =
                corum::ranked::ui::PlayerIcon::createRemote(opponentAccountId);
            if (m_fields->opponentProgressIcon) {
                m_fields->opponentProgressIcon->setPosition({-31.0f, 0.0f});
                m_fields->opponentProgressIcon->setScale(0.50f);
                m_fields->opponentProgressGroup->addChild(m_fields->opponentProgressIcon, 3);
            }
            m_fields->opponentProgressLabel = addShadowedHudLabel(
                m_fields->opponentProgressGroup,
                "0%",
                "black",
                0.44f,
                {0.5f, 0.5f},
                {0.0f, 0.0f},
                corum::ranked::ui::Text,
                3
            );
            m_fields->opponentProgressLabel->setID("ranked-opponent-progress"_spr);
            m_fields->practiceIndicator =
                CCSprite::createWithSpriteFrameName("GJ_practiceBtn_001.png");
            if (m_fields->practiceIndicator) {
                m_fields->practiceIndicator->setID("ranked-practice-indicator"_spr);
                m_fields->practiceIndicator->setPosition({31.0f, 0.0f});
                m_fields->practiceIndicator->setScale(0.28f);
                m_fields->practiceIndicator->setOpacity(210);
                m_fields->practiceIndicator->setVisible(false);
                m_fields->opponentProgressGroup->addChild(m_fields->practiceIndicator, 3);
            }
            m_fields->hudRoot->addChild(m_fields->opponentProgressGroup, 5);
        }

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_fields->debugLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "BOT MATCH · DEBUG",
            "bigFont.fnt",
            0.20f,
            {0.5f, 0.0f},
            {size.width / 2.0f, 6.0f},
            ccc3(255, 145, 105)
        );
        setHudVisible(m_fields->debugLabel, corum::ranked::RankedRuntime::get().view().match.debug);
#endif

        addSpectatorPanel(size);
        addChild(m_fields->hudRoot, 1000);
        updateRankedHud();
    }

    void addSpectatorPanel(CCSize const& size) {
        auto const width = std::min(150.0f, size.width - 40.0f);
        m_fields->spectatorPanel = CCNode::create();
        m_fields->spectatorPanel->setID("ranked-spectator-overlay"_spr);
        m_fields->spectatorPanel->setContentSize({width, 44.0f});
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        m_fields->spectatorPanel->setPosition({layout.spectatorCenterX, layout.spectatorCenterY});

        addShadowedHudLabel(
            m_fields->spectatorPanel,
            corum::ranked::i18n::text("live-spectator", "LIVE SPECTATOR"),
            "bigFont.fnt",
            0.23f,
            {0.5f, 0.5f},
            {0.0f, 15.0f},
            corum::ranked::ui::Gold,
            2
        );
        m_fields->spectatorNameLabel = addShadowedHudLabel(
            m_fields->spectatorPanel,
            corum::ranked::i18n::text("opponent", "OPPONENT"),
            "bigFont.fnt",
            0.23f,
            {0.5f, 0.5f},
            {-36.0f, 2.0f},
            corum::ranked::ui::Text,
            2
        );
        limitHudWidth(m_fields->spectatorNameLabel, 72.0f, 0.23f, 0.16f);
        m_fields->spectatorProgressLabel = addShadowedHudLabel(
            m_fields->spectatorPanel,
            corum::ranked::i18n::text("current", "CURRENT") + " : -",
            "black",
            0.34f,
            {0.5f, 0.5f},
            {37.0f, 1.0f},
            corum::ranked::ui::Cyan,
            2
        );
        addShadowedHudLabel(
            m_fields->spectatorPanel,
            "ROUND CLOCK",
            "bigFont.fnt",
            0.20f,
            {0.5f, 0.5f},
            {-24.0f, -15.0f},
            corum::ranked::ui::TextMuted,
            2
        );
        m_fields->spectatorTimerLabel = addShadowedHudLabel(
            m_fields->spectatorPanel,
            "-",
            "bigFont.fnt",
            0.26f,
            {0.0f, 0.5f},
            {19.0f, -15.0f},
            corum::ranked::ui::Text,
            2
        );
        m_fields->spectatorPanel->setVisible(false);
        m_fields->hudRoot->addChild(m_fields->spectatorPanel, 10);
    }

    void animateHudPosition(CCLabelBMFont* foreground, CCPoint position) {
        if (!foreground) return;
        foreground->stopAllActions();
        foreground->runAction(CCEaseInOut::create(CCMoveTo::create(0.22f, position), 2.0f));
        if (auto* shadow = hudShadow(foreground)) {
            shadow->stopAllActions();
            shadow->runAction(CCEaseInOut::create(
                CCMoveTo::create(0.22f, {position.x + 0.75f, position.y - 0.75f}),
                2.0f
            ));
        }
    }

    void updatePlacementTopHud(double liveProgress) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!runtime.isPlacement()) return;
        struct Record {
            std::string id;
            double progress = 0.0;
            double score = 0.0;
        };
        std::vector<Record> records;
        for (auto const& attempt : runtime.view().placement.currentMapTopAttempts) {
            records.push_back({attempt.attemptId, attempt.progressPercent, attempt.awardedScore});
        }
        auto const& activeId = runtime.view().placement.activeAttemptId;
        if (
            !activeId.empty() && liveProgress >= 0.0 &&
            scoringEnabled() && !m_fields->attemptEndReported &&
            m_fields->qualifyingPercent >= 0.0
        ) {
            records.erase(
                std::remove_if(records.begin(), records.end(), [&](Record const& record) {
                    return record.id == activeId;
                }),
                records.end()
            );
            records.push_back({
                activeId,
                liveProgress,
                corum::ranked::calculateAttemptScore(
                    liveProgress, false, m_fields->qualifyingPercent
                ),
            });
        }
        std::sort(records.begin(), records.end(), [](Record const& left, Record const& right) {
            if (std::abs(left.score - right.score) > 0.0005) return left.score > right.score;
            return left.id < right.id;
        });
        if (records.size() > 2) records.resize(2);

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const top = corum::ranked::layoutHud(size.width, size.height).topY;
        std::array<bool, 2> usedNodes {false, false};
        for (std::size_t rank = 0; rank < records.size(); ++rank) {
            int node = -1;
            for (std::size_t index = 0; index < m_fields->placementTopRecordIds.size(); ++index) {
                if (m_fields->placementTopRecordIds[index] == records[rank].id) {
                    node = static_cast<int>(index);
                    break;
                }
            }
            auto const existingIdentity = node >= 0;
            if (node < 0) {
                for (std::size_t index = 0; index < usedNodes.size(); ++index) {
                    auto const retained = std::any_of(records.begin(), records.end(), [&](Record const& record) {
                        return m_fields->placementTopRecordIds[index] == record.id;
                    });
                    if (!usedNodes[index] && !retained) {
                        node = static_cast<int>(index);
                        break;
                    }
                }
            }
            if (node < 0) node = static_cast<int>(rank);
            usedNodes[static_cast<std::size_t>(node)] = true;
            auto* row = m_fields->placementTopLabels[static_cast<std::size_t>(node)];
            m_fields->placementTopRecordIds[static_cast<std::size_t>(node)] = records[rank].id;
            auto const ordinal = rank == 0 ? "1st" : "2nd";
            setHudText(
                row,
                fmt::format(
                    "{} - {}% ({}점)",
                    ordinal,
                    formatScore(records[rank].progress),
                    formatScore(records[rank].score)
                )
            );
            setHudVisible(row, true);
            auto const target = CCPoint{
                size.width - 10.0f,
                top - 4.0f - static_cast<float>(rank) * 15.0f,
            };
            if (existingIdentity && std::abs(row->getPositionY() - target.y) > 0.5f) {
                animateHudPosition(row, target);
            } else {
                setHudPosition(row, target);
            }
        }
        for (std::size_t rank = records.size(); rank < 2; ++rank) {
            int node = -1;
            for (std::size_t index = 0; index < usedNodes.size(); ++index) {
                if (!usedNodes[index]) { node = static_cast<int>(index); break; }
            }
            if (node < 0) continue;
            usedNodes[static_cast<std::size_t>(node)] = true;
            m_fields->placementTopRecordIds[static_cast<std::size_t>(node)].clear();
            auto* row = m_fields->placementTopLabels[static_cast<std::size_t>(node)];
            setHudText(row, rank == 0 ? "1st - -" : "2nd - -");
            setHudVisible(row, true);
            setHudPosition(row, {
                size.width - 10.0f,
                top - 4.0f - static_cast<float>(rank) * 15.0f,
            });
        }
    }

    void updateRankedHud() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        m_fields->renderedRevision = runtime.view().revision;
        if (!m_fields->hudRoot || !m_fields->fpsLabel || !m_fields->ownScoreLabel) return;

        auto const placement = runtime.isPlacement();

        auto attemptsUsedA = match.deathmatchAttemptsUsedA;
        auto attemptsUsedB = match.deathmatchAttemptsUsedB;
        if (match.state == "DEATHMATCH_PLAYING") {
            auto const localVisualUsed = runtime.localDeathmatchVisualAttemptsUsed();
            if (match.side == "A") attemptsUsedA = std::max(attemptsUsedA, localVisualUsed);
            if (match.side == "B") attemptsUsedB = std::max(attemptsUsedB, localVisualUsed);
        }
        auto const presentation = corum::ranked::presentHud({
            .side = match.side,
            .state = placement
                ? (match.state == "MAP_PLAYING" ? "ROUND_PLAYING"
                    : match.state == "MAP_SETTLING" ? "ROUND_SETTLING"
                    : match.state)
                : match.state,
            .banner = match.banner,
            .roundNumber = match.roundNumber,
            .scoreA = match.scoreA,
            .scoreB = match.scoreB,
            .clearsA = match.clearsA,
            .clearsB = match.clearsB,
            .deathmatch = match.state == "DEATHMATCH_PLAYING",
            .deathmatchSequence = match.deathmatchSequence,
            .deathmatchAttemptsUsedA = attemptsUsedA,
            .deathmatchAttemptsUsedB = attemptsUsedB,
            .deathmatchAttemptsCompletedA = match.deathmatchAttemptsCompletedA,
            .deathmatchAttemptsCompletedB = match.deathmatchAttemptsCompletedB,
            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,
            .remainingMillis = runtime.deadlineMillis(),
            .signedRemainingMillis = runtime.signedDeadlineMillis(),
            .renderFps = m_fields->fpsMeter.fps(),
            .spectatorActive = match.spectatorActive,
            .spectatorOpponentName = match.spectatorOpponentName,
            .spectatorCurrentProgress = match.spectatorCurrentProgress,
            .opponentProgressEnabled = !placement,
            .opponentCurrentProgress = match.opponentProgress,
            .opponentFinished = match.opponentFinished,
            .opponentPractice = match.opponentPractice,
        });

        setHudText(m_fields->fpsLabel, presentation.fpsText);
        auto const ownIsA = match.side == "A";
        if (m_fields->ownNameLabel) {
            setHudText(
                m_fields->ownNameLabel,
                placement ? "PLACEMENT" : (ownIsA ? match.playerAName : match.playerBName)
            );
            limitHudWidth(m_fields->ownNameLabel, 96.0f, 0.51f, 0.21f);
        }
        if (m_fields->opponentNameLabel) {
            setHudText(m_fields->opponentNameLabel, ownIsA ? match.playerBName : match.playerAName);
            limitHudWidth(m_fields->opponentNameLabel, 96.0f, 0.51f, 0.21f);
        }
        auto const liveProgress = !scoringEnabled() || m_fields->attemptEndReported
            ? -1.0
            : rankedProgressPercent(this);
        auto const ownLiveScore = runtime.localDisplayScore(
            liveProgress,
            m_fields->qualifyingPercent
        );
        if (placement) updatePlacementTopHud(liveProgress);
        setHudText(m_fields->ownScoreLabel, formatScore(ownLiveScore));
        limitHudWidth(m_fields->ownScoreLabel, 76.0f, 0.714f, 0.26f);
        auto const opponentScore = ownIsA ? match.scoreB : match.scoreA;
        setHudText(m_fields->opponentScoreLabel, formatScore(opponentScore));
        limitHudWidth(m_fields->opponentScoreLabel, 76.0f, 0.714f, 0.26f);
        auto ownChecks = presentation.ownChecks;
        if (!presentation.deathmatch) ownChecks = corum::ranked::clearChecks(runtime.localDisplayClears());
        for (std::size_t index = 0; index < m_fields->ownChecks.size(); ++index) {
            applyCheck(m_fields->ownChecks[index], ownChecks[index]);
            applyCheck(m_fields->opponentChecks[index], presentation.opponentChecks[index]);
            m_fields->ownChecks[index]->setVisible(!presentation.deathmatch);
            m_fields->opponentChecks[index]->setVisible(!presentation.deathmatch);
            if (placement && index == 2) {
                m_fields->ownChecks[index]->setVisible(false);
                m_fields->opponentChecks[index]->setVisible(false);
            }
        }
        setHudVisible(m_fields->ownAttemptLabel, false);
        setHudVisible(m_fields->opponentAttemptLabel, false);
        auto const qualifying = match.currentMap ? match.currentMap->qualifyingPercent : 100.0;
        if (placement || !presentation.qualifyingVisible) {
            setHudVisible(m_fields->qualifyingCaption, false);
            setHudVisible(m_fields->qualifyingLabel, false);
            if (placement && m_fields->placementGrade) {
                m_fields->placementGrade->setGrade(runtime.view().placement.currentMapGrade, true);
            }
        } else {
            setHudVisible(m_fields->qualifyingCaption, true);
            setHudVisible(m_fields->qualifyingLabel, true);
            setHudText(m_fields->qualifyingLabel, formatScore(qualifying) + "%");
            limitHudWidth(m_fields->qualifyingLabel, 96.0f, 0.714f, 0.28f);
        }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if (m_fields->debugLabel) setHudVisible(m_fields->debugLabel, match.debug);
#endif

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const top = corum::ranked::layoutHud(size.width, size.height).topY;
        auto const roundText = placement
            ? fmt::format(
                "STAGE {} / {}  ·  MAP {} / {}",
                runtime.view().placement.currentStageIndex,
                runtime.view().placement.totalStages,
                runtime.view().placement.currentMapIndex,
                runtime.view().placement.mapsPerStage
            )
            : presentation.roundText;
        auto const& timerText = presentation.timerText;
        auto const stateText = presentation.stateText;
        setHudText(m_fields->roundLabel, roundText);
        setHudText(m_fields->timerLabel, timerText);
        setHudText(m_fields->stateLabel, stateText);
        setHudVisible(m_fields->roundLabel, !roundText.empty());
        setHudVisible(m_fields->timerLabel, !timerText.empty());
        setHudVisible(m_fields->stateLabel, !stateText.empty());
        setHudPosition(m_fields->roundLabel, {size.width / 2.0f, top - 1.0f});
        setHudPosition(m_fields->timerLabel, {size.width / 2.0f, top - 18.0f});
        setHudPosition(m_fields->stateLabel, {size.width / 2.0f, top - 47.0f});
        auto const urgent =
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "ROUND_SETTLING" ||
            match.state == "MAP_SETTLING" ||
            (match.state == "MAP_PLAYING" && runtime.signedDeadlineMillis().value_or(1) <= 0);
        m_fields->timerLabel->setColor(urgent ? corum::ranked::ui::Red : corum::ranked::ui::Text);
        m_fields->stateLabel->setColor(urgent ? corum::ranked::ui::Red : corum::ranked::ui::Gold);

        auto const spectatorVisible = m_fields->spectatorMode && m_fields->localSafety.active() &&
            runtime.isSpectating() && presentation.spectatorVisible;
        m_fields->spectatorPanel->setVisible(spectatorVisible);
        if (m_fields->opponentProgressGroup) {
            m_fields->opponentProgressGroup->setVisible(presentation.opponentProgressVisible);
        }
        setHudText(m_fields->opponentProgressLabel, presentation.opponentProgressText);
        if (m_fields->practiceIndicator) {
            m_fields->practiceIndicator->setVisible(presentation.practiceIndicatorVisible);
        }

        if (placement) {
            setHudVisible(m_fields->opponentNameLabel, false);
            setHudVisible(m_fields->opponentScoreCaption, false);
            setHudVisible(m_fields->opponentScoreLabel, false);
            setHudVisible(m_fields->opponentAttemptLabel, false);
            for (auto* icon : m_fields->opponentChecks) {
                if (icon) icon->setVisible(false);
            }
            m_fields->spectatorPanel->setVisible(false);
        }
        if (spectatorVisible) {
            setHudText(m_fields->spectatorNameLabel, presentation.spectatorOpponentText);
            auto const status = corum::ranked::SpectatorLiveRuntime::get().status();
            auto const rendered = m_fields->remotePlayer.cameraFrame();
            setHudText(m_fields->spectatorProgressLabel,
                rendered && rendered->pose && rendered->pose->finished ? "CLEARED" :
                rendered && rendered->pose && rendered->pose->dead ? "OPPONENT DOWN" :
                status == corum::ranked::SpectatorLiveStatus::Fallback ? "POLLING..." :
                m_fields->spectatorCamera.state() == corum::ranked::SpectatorCameraState::Waiting ?
                    "WAITING..." : presentation.spectatorProgressText);
            limitHudWidth(m_fields->spectatorProgressLabel, 72.0f, 0.34f, 0.16f);
            setHudText(m_fields->spectatorTimerLabel, presentation.timerText);
        }
    }
};

bool corum::ranked::prepareSpectatorScene(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return false;
    return static_cast<CorumRankedPlayLayer*>(layer)->prepareSpectatorScene();
}

namespace {
CorumRankedPlayLayer* localSafetyLayer() {
    auto* layer = persistenceOwner ? persistenceOwner : PlayLayer::get();
    return layer ? static_cast<CorumRankedPlayLayer*>(layer) : nullptr;
}
} // namespace

// Native forced-camera ownership, not player pointer/pose swapping. The exact
// current scene's controller is inert for every ordinary gameplay mode.
class $modify(CorumRankedSpectatorCameraHook, GJBaseGameLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::updateCamera", Priority::First)) {
            log::warn("Failed to install spectator camera priority");
        }
    }
    void updateCamera(float dt) {
        auto* layer = PlayLayer::get();
        if (layer && static_cast<GJBaseGameLayer*>(layer) == this) {
            static_cast<CorumRankedPlayLayer*>(layer)->driveSpectatorCamera();
        }
        GJBaseGameLayer::updateCamera(dt);
    }
};

// Stop only the dummy's own physics at the completion boundary. Do not pause
// PlayLayer/world/relay updates: a distinct Phase-2 remote player must be free
// to display 100%. checkForEnd + completion callbacks cover a large single step.
class $modify(CorumRankedSpectatorDummyPlayer, PlayerObject) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PlayerObject::update", Priority::First)) {
            log::warn("Failed to install spectator dummy update priority");
        }
    }

    void update(float dt) {
        auto* layer = localSafetyLayer();
        if (layer && layer->freezeLocalDummy(this)) return;
        PlayerObject::update(dt);
    }
};

// Explicit persistence firewall, independent of m_isPracticeMode/test mode.
// Only the exact level object of this dummy scene is guarded. Unrelated levels,
// normal Ranked, Placement, real Death Match and user Practice retain vanilla.
class $modify(CorumRankedSpectatorProgressSave, GJGameLevel) {
    static void onModify(auto& self) {
        for (auto name : {"GJGameLevel::savePercentage", "GJGameLevel::saveNewScore"}) {
            if (!self.setHookPriorityPre(name, Priority::First)) {
                log::warn("Failed to install spectator persistence priority: {}", name);
            }
        }
    }

    void savePercentage(int percent, bool practice, int clicks, int attempts, bool valid) {
        auto* layer = localSafetyLayer();
        if (layer && layer->blocksLevelPersistence(this)) return;
        GJGameLevel::savePercentage(percent, practice, clicks, attempts, valid);
    }

    void saveNewScore(int value, int type, int ticks, int clicks, int coins, gd::string inputs, bool save) {
        auto* layer = localSafetyLayer();
        if (layer && layer->blocksLevelPersistence(this)) return;
        GJGameLevel::saveNewScore(value, type, ticks, clicks, coins, inputs, save);
    }
};
