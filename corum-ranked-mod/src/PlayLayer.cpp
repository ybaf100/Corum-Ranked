#include "RankedRuntime.hpp"
#include "RankedRoundPreparation.hpp"
#include "SurvivalRuntime.hpp"
#include "domain/SurvivalNativeOptions.hpp"
#include "SurvivalOptionsPreferences.hpp"
#include "SurvivalSceneBridge.hpp"
#include "RankedLevelPreparation.hpp"
#include "ui/SurvivalHud.hpp"
#include "ui/RankedHudLabel.hpp"
#include "domain/ScoreDisplay.hpp"
#include "domain/CompetitiveHudLayout.hpp"
#include "RankedAudioManager.hpp"
#include "SpectatorLiveRuntime.hpp"
#include "domain/SpectatorCapturePolicy.hpp"
#include "domain/SpectatorProgressPolicy.hpp"
#include "spectator/ScopedCameraTargetOverride.hpp"
#include "spectator/SpectatorPlayerCapture.hpp"
#include "spectator/SpectatorWorldDriver.hpp"
#include "spectator/ScopedSpectatorPersistenceMode.hpp"
#include "domain/SpectatorNativeProbe.hpp"
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
#include "ui/RankedEntryOverlay.hpp"
#include "ui/RankedInput.hpp"
#include "ui/PlacementGradeBadge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/AppDelegate.hpp>
#include <Geode/binding/UILayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iterator>
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

std::string formatScore(double value,bool deathmatch=false) {
    return corum::ranked::rankedScoreText(value,deathmatch);
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
        bool nativeCompletionStarted = false;
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
        struct DeathRowVisual {
            std::string id;
            CCNode* node = nullptr;
            CCLabelBMFont* label = nullptr;
            float targetY = 0;
            bool fading = false;
        };
        corum::ranked::DeathFeed deathFeed;
        std::vector<DeathRowVisual> deathRows;
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
        bool survivalLevel = false;
        bool survivalFreePlayHud = false;
        bool survivalNativeClearFinished = false;
        corum::ranked::LevelPreparationContext survivalContext;
        corum::ranked::ui::SurvivalHud* survivalHud = nullptr;
        bool survivalWaiting = false;
        bool rankedInitialized = false;
        bool nativeEntryHeld = false;
        // Process-owned fallback is independent of the paused native PlayLayer scheduler.
        std::chrono::steady_clock::time_point nextIndependentEntryPulseAt {};
        int independentEntryLogCount = 0;
        bool entryNativeCompletionInProgress = false;
        bool entryAttemptCountDeferred = false;
        bool entryWasPaused = false, entryTouchEnabled = false, entryKeyboardEnabled = false;
        bool entryUIKeyboardEnabled = false, entryUIKeypadEnabled = false;
        bool entryUIPauseEnabled = false;
        corum::ranked::ui::RankedEntryOverlay* entryOverlay = nullptr;
        bool autoExitRequested = false;
        bool pauseQuitBlockedLogged = false;
        bool gameplayFrameObserved = false;
        corum::ranked::SpectatorCapturePolicy spectatorCapture;
        bool attemptStartReported = false;
        bool attemptEndReported = false;
        bool deniedDeathmatchVisualAttempt = false;
        bool prematureResultLogged = false;
        bool rankedPractice = false;
        bool spectatorMode = false;
        bool spectatorPending = false;
        std::chrono::steady_clock::time_point spectatorHandoffReadyAt {};
        corum::ranked::SpectatorLocalSafety localSafety;
        bool suppressedDeathLogged = false;
        bool suppressedCompletionLogged = false;
        bool suppressedSaveLogged = false;
        bool safetyRetired = false;
        bool vanillaGameplayEndInProgress = false;
        corum::ranked::CorumRemotePlayer remotePlayer;
        corum::ranked::CorumSpectatorCamera spectatorCamera;
        corum::ranked::SpectatorWorldDriver spectatorWorldDriver;
        corum::ranked::SpectatorSceneContext sceneContext;
        int placementMapIndex = 0;
        bool spectatorScenePrepared = false;
        bool spectatorPresentationRetired = false;
        std::optional<corum::ranked::SpectatorRemoteFrame> spectatorRenderedFrame;
        std::optional<unsigned int> spectatorCameraFrameNumber;
        std::optional<unsigned int> spectatorPresentationFrameNumber, spectatorWorldFrameNumber;
        bool spectatorWorldUpdateInProgress = false;
        bool spectatorWorldResetInProgress = false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        bool debugRecordScene = false, debugPlaybackScene = false, debugRecordHasFrame = false;
        corum::ranked::SpectatorNativeProbe spectatorNativeProbe;
        std::chrono::steady_clock::time_point nextNativeProbeLog {};
        CCLabelBMFont* debugSpectateHud = nullptr;
        int cameraForensicFrames = 0;
        std::int64_t cameraForensicSequence = -1;
        bool cameraForensicFrame = false;
        int worldForensicFrames = 0;
        std::optional<corum::ranked::SpectatorGhostPose> worldForensicPose;
        std::int64_t worldForensicSequence = -1;
#endif
    };

    static void onModify(auto& self) {
        // Run the narrow guard before vanilla/other Corum lifecycle callbacks.
        for (auto name : {"PlayLayer::destroyPlayer", "PlayLayer::levelComplete",
                          "PlayLayer::checkForEnd", "PlayLayer::showCompleteText", "PlayLayer::updateAttempts"}) {
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
        // Freeze physics, not its network-driven world coordinates.
        // The independent Phase-1 99% boundary remains a backstop for setup and
        // large steps; neither path pauses the world, remote renderer or relay.
        return frozen || (m_fields->rankedInitialized && m_fields->localSafety.active());
    }

    bool blocksSpectatorNativePlayerWrite(PlayerObject* player) {
        return player && player == m_player1 && m_fields->localSafety.active() &&
            m_fields->spectatorWorldDriver.owns(player) &&
            !corum::ranked::ScopedSpectatorWorldDriverWrite::allows(player);
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
            m_fields->rankedInitialized && !m_fields->nativeEntryHeld &&
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
        m_fields->survivalLevel=corum::ranked::SurvivalRuntime::get().consumeNativeLaunch(level);
        m_fields->survivalFreePlayHud=!m_fields->survivalLevel && corum::ranked::SurvivalRuntime::get().freePlaying();
        if(m_fields->survivalLevel){
            m_fields->survivalContext=corum::ranked::SurvivalRuntime::get().preparationContext();
            corum::ranked::SurvivalOptionsPreferences::get().apply(level,corum::ranked::SurvivalRuntime::get().optionsContext());
        }
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
        m_fields->nativeEntryHeld = m_fields->survivalLevel || (m_fields->rankedLevel && runtime.canEnterNativeRoundScene());
        m_fields->sceneContext = currentSpectatorSceneContext();
        m_fields->placementMapIndex = runtime.isPlacement() ? runtime.view().placement.currentMapIndex : 0;
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
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene)
            log::info("[DebugSpectate] PlayLayer entered level={}", m_fields->levelId);
#endif
        acquireGameplayAudio();
        // Keep Match music alive throughout ROUND STARTING IN and LevelInfo.
        // Stop it only after vanilla has successfully created the real PlayLayer.
        if (m_fields->rankedLevel) {
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
            if (m_fields->debugRecordScene) log::info("[DebugSpectate] RECORDING active");
            auto const size = CCDirector::sharedDirector()->getWinSize();
            m_fields->debugSpectateHud = label(debug.status(), "medium", 0.28f, {0.5f, 0.5f}, {size.width / 2.0f, size.height - 26.0f});
            addChild(m_fields->debugSpectateHud, 100);
            if (m_fields->debugPlaybackScene) schedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
            return true;
        }
#endif
        if(m_fields->survivalFreePlayHud){
            addSurvivalHud();schedule(schedule_selector(CorumRankedPlayLayer::survivalFrame));return true;
        }
        if ((!m_fields->rankedLevel && !m_fields->survivalLevel) || m_isTestMode) return true;

        // Latch read-only authority during init, before any postUpdate/onQuit
        // callback can observe the new scene. A reconnecting spectator must not
        // emit even a transient Start/Progress/End/Clear lifecycle.
        syncSpectatorLocalSafety();
        m_fields->rankedPractice = !m_fields->spectatorMode && m_isPracticeMode;
        if (m_fields->spectatorMode && m_player1) m_player1->setVisible(false);
        if (m_fields->rankedPractice) runtime.reportPracticePresence(true);
        if(m_fields->survivalLevel){
            // Native camera shake suppression, scoped to this official game.
            // Preserve any author/native disable flag when the preference is OFF.
            m_skipCameraShake=m_skipCameraShake || level->m_disableShakeToggled;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            log::info("[SurvivalOptions] after native init level={} LDM={} skipCameraShake={} disableShake={}",
                m_fields->levelId,m_lowDetailMode,m_skipCameraShake,level->m_disableShakeToggled);
#endif
            addSurvivalHud();
        }else addRankedHud();
        m_fields->gameplayFrameObserved = false;
        m_fields->attemptJournalReadyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        // Scene-owned presentation heartbeat also runs after vanilla completion
        // has stopped gameplay postUpdate. It cannot create gameplay evidence.
        schedule(schedule_selector(CorumRankedPlayLayer::spectatorFrame));
        if(m_fields->survivalLevel)schedule(schedule_selector(CorumRankedPlayLayer::survivalFrame));
        if (m_fields->nativeEntryHeld) {
            m_fields->entryWasPaused = m_isPaused;
            m_fields->entryTouchEnabled = isTouchEnabled();
            m_fields->entryKeyboardEnabled = isKeyboardEnabled();
            // Freeze through the existing world/evidence gate below. Setting
            // GD's native pause flag during init can prevent normal scene entry
            // and its scheduler from starting before ENTERED is acknowledged.
            setTouchEnabled(false);
            setKeyboardEnabled(false);
            if (m_uiLayer) {
                m_fields->entryUIKeyboardEnabled = m_uiLayer->isKeyboardEnabled();
                m_fields->entryUIKeypadEnabled = m_uiLayer->isKeypadEnabled();
                // The countdown owns the swallowing touch delegate. Leave the
                // native UILayer registered in its original scene-entry order;
                // re-registering it at GO can put it ahead of its Pause menu.
                m_uiLayer->setKeyboardEnabled(false);
                m_uiLayer->setKeypadEnabled(false);
                m_uiLayer->resetAllButtons();
                if (m_uiLayer->m_pauseBtn) {
                    m_fields->entryUIPauseEnabled = m_uiLayer->m_pauseBtn->isEnabled();
                    m_uiLayer->m_pauseBtn->setEnabled(false);
                }
            }
            if (m_player1) m_player1->releaseAllButtons();
            if (m_player2) m_player2->releaseAllButtons();
            m_queuedButtons.clear();
            m_fields->entryOverlay = corum::ranked::ui::RankedEntryOverlay::create();
            if (m_fields->entryOverlay) {
                addChild(m_fields->entryOverlay, 10000);
                m_fields->entryOverlay->present(false, std::nullopt);
            }
            FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
            log::info("[RankedEntry] native hold level={} pausedBeforeHold={} pauseBeforeHold={} appPaused={}",
                m_fields->levelId, m_fields->entryWasPaused, m_fields->entryUIPauseEnabled,
                AppDelegate::get() && AppDelegate::get()->m_gamePaused);
        }
        return true;
    }

    // Uses the existing native world-frame owner: no additional PlayerObject
    // detours or remote physics. Scene/UI heartbeat continues while GD gameplay,
    // button commands, evidence and its audio clock are held at the initial pose.
    void queueHeldEntryExit() {
        // Survival owns one retryable deferred quit path, including Pause. The
        // old entry-only path latched autoExitRequested before onQuit could be
        // refused by Pause; an accepted surrender then could never queue again.
        if(m_fields->survivalLevel){queueSurvivalGameplayReturn();return;}
        if (m_fields->autoExitRequested) return;
        // A round boundary can race the genuine native clear animation.
        // Let its completion setup finish before retiring this official scene.
        if(m_fields->survivalLevel && m_hasCompletedLevel && !m_fields->survivalNativeClearFinished)return;
        if(m_fields->survivalLevel){auto* director=CCDirector::sharedDirector();
            if(director->getNextScene() || getParent()!=director->getRunningScene())return;}
        m_fields->autoExitRequested = true;
        // Leave the native update/lifecycle stack before retiring this scene.
        auto scene = geode::WeakRef<PlayLayer>(this);
        auto const survival=m_fields->survivalLevel;
        queueInMainThread([scene,survival] {
            if (auto layer = scene.lock(); layer && layer.data() == PlayLayer::get() &&
                layer->getParent() && layer->getParent()->isRunning() &&
                layer->getParent() == CCDirector::sharedDirector()->getRunningScene()) {
                if(survival && corum::ranked::quitThroughSurvivalCompletionOverlay(layer.data()))return;
                layer->onQuit();
            }
        });
    }

    bool blockEntryGameplayUpdate() {
        if(m_fields->survivalLevel)return blockSurvivalEntry();
        if (!m_fields->nativeEntryHeld) return false;
        if (m_fields->entryNativeCompletionInProgress || m_fields->autoExitRequested) return true;
        // Scene-entry actions can enable the native menu after init. Keep our
        // lock while waiting, then release it through vanilla at successful GO.
        if (m_uiLayer && m_uiLayer->m_pauseBtn) m_uiLayer->disableMenu();
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        auto const& match = runtime.view().match;
        if (match.matchId != m_fields->sceneContext.matchId ||
            match.roundNumber != m_fields->sceneContext.roundNumber ||
            match.deathmatchSequence != m_fields->sceneContext.deathmatchSequence ||
            runtime.currentLevelId() != m_fields->levelId ||
            (!corum::ranked::roundEnteringState(match.state) && !runtime.canEnterCurrentLevel())) {
            queueHeldEntryExit();
            return true;
        }
        // ACK attachment to the active scene, not a native scheduler flag that
        // can be false while entry is held. GO still requires a running owner.
        auto* director = CCDirector::sharedDirector();
        bool const ownsScene = this == PlayLayer::get() && getParent() &&
            getParent() == director->getRunningScene() && !director->getNextScene();
        if (ownsScene) runtime.noteNativeRoundSceneEntered(m_fields->levelId);
        auto const remaining = runtime.nativeEntryCountdownMillis();
        auto const bothEntered = match.entry.enteredA && match.entry.enteredB;
        if (m_fields->entryOverlay) m_fields->entryOverlay->present(bothEntered, remaining);
        m_queuedButtons.clear();
        FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        if (!bothEntered || !remaining || *remaining > 0 ||
            (corum::ranked::roundEnteringState(match.state) && !runtime.nativeEntryStartGranted()) ||
            !ownsScene || !isRunning() || !getParent()->isRunning() ||
            !runtime.armCurrentLevelForGameplay()) return true;
        // Quit returning is not scene retirement: the deferred pop/onExit can
        // still change GD's process pause state. A local bit alone cannot certify
        // GO. This is an already-running entry, so do not Resume/onEnter again
        // and register its child input delegates a second time.
        Ref<PlayLayer> const keepAlive(this);
        m_isPaused = m_fields->entryWasPaused;
        auto* app = AppDelegate::get();
        if (!m_fields->entryWasPaused && app && app->m_gamePaused) {
            m_fields->entryNativeCompletionInProgress = true;
            static_cast<PlayLayer*>(this)->onEnterTransitionDidFinish();
            m_fields->entryNativeCompletionInProgress = false;
            log::info("[RankedEntry] native completion level={} paused={} appPaused={}",
                m_fields->levelId, m_isPaused, AppDelegate::get() && AppDelegate::get()->m_gamePaused);
        }
        auto const& currentMatch = runtime.view().match;
        auto const currentRemaining = runtime.nativeEntryCountdownMillis();
        bool const sameEntry = this == PlayLayer::get() && isRunning() &&
            getParent() && getParent()->isRunning() &&
            getParent() == CCDirector::sharedDirector()->getRunningScene() &&
            !CCDirector::sharedDirector()->getNextScene() &&
            currentMatch.matchId == m_fields->sceneContext.matchId &&
            currentMatch.roundNumber == m_fields->sceneContext.roundNumber &&
            currentMatch.deathmatchSequence == m_fields->sceneContext.deathmatchSequence &&
            runtime.currentLevelId() == m_fields->levelId &&
            (corum::ranked::roundEnteringState(currentMatch.state) || runtime.canEnterCurrentLevel()) &&
            currentMatch.entry.enteredA && currentMatch.entry.enteredB &&
            currentRemaining && *currentRemaining <= 0 &&
            (!corum::ranked::roundEnteringState(currentMatch.state) || runtime.nativeEntryStartGranted());
        app = AppDelegate::get();
        if (!sameEntry || (!m_fields->entryWasPaused && (!app || app->m_gamePaused || m_isPaused))) {
            // Never publish an attempt, open input or resume gameplay audio on
            // refusal or on a scene/context changed by another lifecycle hook.
            log::warn("[RankedEntry] GO blocked after native completion level={} sameEntry={} paused={} appPaused={}",
                m_fields->levelId, sameEntry, m_isPaused, app && app->m_gamePaused);
            m_isPaused = true;
            if (this == PlayLayer::get() && getParent() && getParent()->isRunning() &&
                getParent() == CCDirector::sharedDirector()->getRunningScene())
                FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
            queueHeldEntryExit();
            return true;
        }
        releaseEntryControls();
        log::info("[RankedEntry] shared countdown complete; gameplay released level={} paused={} appPaused={} pauseBeforeHold={} pauseEnabled={}",
            m_fields->levelId, m_isPaused, app && app->m_gamePaused, m_fields->entryUIPauseEnabled,
            m_uiLayer && m_uiLayer->m_pauseBtn && m_uiLayer->m_pauseBtn->isEnabled());
        if (m_uiLayer && m_uiLayer->m_pauseBtn) {
            auto* menu = typeinfo_cast<CCMenu*>(m_uiLayer->m_pauseBtn->getParent());
            auto* dispatcher = CCDirector::sharedDirector()->getTouchDispatcher();
            auto* inputHandler = dispatcher->findHandler(m_uiLayer);
            auto* menuHandler = menu ? dispatcher->findHandler(menu) : nullptr;
            log::info("[RankedInput] GO level={} uiTouch={} menuTouch={} menuEnabled={} uiPriority={} menuPriority={}",
                m_fields->levelId, m_uiLayer->isTouchEnabled(), menu && menu->isTouchEnabled(),
                menu && menu->isEnabled(), inputHandler ? inputHandler->m_nPriority : 0,
                menuHandler ? menuHandler->m_nPriority : 0);
        }
        return false;
    }

    bool blockSurvivalEntry() {
        if(!m_fields->rankedInitialized || m_fields->entryNativeCompletionInProgress)return m_fields->nativeEntryHeld;
        if(m_fields->autoExitRequested)return true;
        auto& runtime=corum::ranked::SurvivalRuntime::get();runtime.tick();
        if(runtime.preparationContext()!=m_fields->survivalContext || runtime.view().state=="ROUND_RESULT" ||
            runtime.view().state=="FINAL_RESULT" || runtime.view().state=="CANCELLED") {queueHeldEntryExit();return true;}
        if(runtime.surrenderPending()){m_queuedButtons.clear();return true;}
        if(!m_fields->nativeEntryHeld)return m_fields->survivalWaiting;
        auto* director=CCDirector::sharedDirector();
        bool const ownsScene=this==PlayLayer::get() && getParent() &&
            getParent()==director->getRunningScene() && !director->getNextScene();
        if(ownsScene)runtime.sceneEntered(m_fields->levelId);
        auto remaining=runtime.countdownMs();
        if(m_fields->entryOverlay)m_fields->entryOverlay->present(runtime.view().ownEntered,remaining);
        m_queuedButtons.clear();FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
        if(!runtime.canPlay() || !runtime.view().ownEntered || !remaining || *remaining>0 ||
            !ownsScene || !isRunning() || !getParent()->isRunning())return true;
        m_isPaused=m_fields->entryWasPaused;
        auto* app=AppDelegate::get();
        if(!m_fields->entryWasPaused && app && app->m_gamePaused) {
            m_fields->entryNativeCompletionInProgress=true;
            static_cast<PlayLayer*>(this)->onEnterTransitionDidFinish();
            m_fields->entryNativeCompletionInProgress=false;
        }
        if(this!=PlayLayer::get() || !isRunning() || !getParent() || !getParent()->isRunning() ||
            getParent()!=director->getRunningScene() || director->getNextScene() ||
            !runtime.canPlay() || runtime.preparationContext()!=m_fields->survivalContext ||
            (!m_fields->entryWasPaused && (!AppDelegate::get() || AppDelegate::get()->m_gamePaused || m_isPaused))) {
            m_isPaused=true;queueHeldEntryExit();return true;
        }
        releaseEntryControls();
        log::info("[RankedEntry] Survival shared countdown complete; gameplay released level={} paused={} appPaused={}",
            m_fields->levelId,m_isPaused,AppDelegate::get() && AppDelegate::get()->m_gamePaused);
        return false;
    }

    void survivalFrame(float dt) {
        if((!m_fields->survivalLevel && !m_fields->survivalFreePlayHud) || !m_fields->rankedInitialized || m_fields->safetyRetired)return;
        auto& runtime=corum::ranked::SurvivalRuntime::get();runtime.tick();
        (void)dt; // SurvivalHud owns its clock/render update independently of native gameplay pause.
        if(m_fields->fpsLabel)setHudText(m_fields->fpsLabel,corum::ranked::presentHud({.renderFps=m_fields->fpsMeter.fps()}).fpsText);
        if(m_fields->survivalFreePlayHud){
            if(runtime.freePlayReturnPending())queueSurvivalGameplayReturn();return;
        }
        if(runtime.normalRoundCleared() && (!m_hasCompletedLevel || m_fields->survivalNativeClearFinished)){
            queueSurvivalGameplayReturn();return;
        }
        if(blockSurvivalEntry())return;
        if(!runtime.canPlay() && runtime.view().state=="PLAYING" && !m_hasCompletedLevel) {m_fields->survivalWaiting=true;m_isPaused=true;}
    }

    void queueSurvivalGameplayReturn() {
        if(m_fields->autoExitRequested || m_fields->safetyRetired || !m_fields->rankedInitialized)return;
        if(m_fields->survivalLevel && m_hasCompletedLevel && !m_fields->survivalNativeClearFinished)return;
        auto* director=CCDirector::sharedDirector();
        if(director->getNextScene() || getParent()!=director->getRunningScene() ||
            typeinfo_cast<CCTransitionScene*>(director->getRunningScene()))return;
        m_fields->autoExitRequested=true;
        setTouchEnabled(false);setKeyboardEnabled(false);m_queuedButtons.clear();
        if(m_uiLayer)m_uiLayer->disableMenu();
        queueInMainThread([weak=WeakRef<PlayLayer>(this)]{
            auto owner=weak.lock();auto* director=CCDirector::sharedDirector();
            if(!owner || owner.data()!=PlayLayer::get() || owner->getParent()!=director->getRunningScene())return;
            auto* managed=static_cast<CorumRankedPlayLayer*>(owner.data());
            if(director->getNextScene()) {managed->m_fields->autoExitRequested=false;return;}
            if(corum::ranked::quitThroughSurvivalCompletionOverlay(owner.data()))return;
            if(managed->m_fields->survivalLevel && corum::ranked::quitSurvivalPauseForReturn(owner.data()))return;
            if(managed->m_fields->survivalFreePlayHud && !corum::ranked::resumeSurvivalFreePlayPauseBeforeQuit(owner.data())){
                managed->m_fields->autoExitRequested=false;return;
            }
            owner->onQuit();
            // If a native Resume hook refused teardown, retry on the next
            // heartbeat instead of treating a requested Quit as completed.
            if(owner.data()==PlayLayer::get() && !corum::ranked::RankedRoundPreparation::get().nativeReturnPending(owner.data()) &&
                (owner->isRunning() || owner->getParent()==director->getRunningScene()))managed->m_fields->autoExitRequested=false;
        });
    }

    void addSurvivalHud() {
        auto const size=CCDirector::sharedDirector()->getWinSize();
        m_fields->hudRoot=CCNode::create();m_fields->hudRoot->setContentSize(size);
        addFpsHud(m_fields->hudRoot,corum::ranked::layoutHud(size.width,size.height).topY);
        m_fields->survivalHud=corum::ranked::ui::SurvivalHud::create();
        if(m_fields->survivalHud){
            m_fields->hudRoot->addChild(m_fields->survivalHud);
            m_fields->survivalHud->attachProgressBar(m_progressBar,m_progressFill,m_progressWidth,m_progressHeight);
        }
        addChild(m_fields->hudRoot,1000);
    }

    void resume() {
        PlayLayer::resume();
        if((m_fields->survivalLevel || m_fields->survivalFreePlayHud) && m_fields->rankedInitialized && !m_fields->safetyRetired && !m_fields->autoExitRequested) {
            resumeSchedulerAndActions();schedule(schedule_selector(CorumRankedPlayLayer::survivalFrame));survivalFrame(0);
        }
    }

    void releaseEntryControls() {
        m_fields->nativeEntryHeld = false;
        setTouchEnabled(m_fields->entryTouchEnabled);
        setKeyboardEnabled(m_fields->entryKeyboardEnabled);
        if (m_uiLayer) {
            m_uiLayer->resetAllButtons();
            m_uiLayer->setKeyboardEnabled(m_fields->entryUIKeyboardEnabled);
            m_uiLayer->setKeypadEnabled(m_fields->entryUIKeypadEnabled);
            if (m_uiLayer->m_pauseBtn) {
                if (!m_isPaused) {
                    m_uiLayer->enableMenu();
                    auto* menu = typeinfo_cast<CCMenu*>(m_uiLayer->m_pauseBtn->getParent());
                    corum::ranked::ui::restorePauseMenuInput(menu, m_uiLayer);
                } else m_uiLayer->m_pauseBtn->setEnabled(m_fields->entryUIPauseEnabled);
            }
        }
        if (m_player1) m_player1->releaseAllButtons();
        if (m_player2) m_player2->releaseAllButtons();
        m_queuedButtons.clear();
        if (m_fields->entryOverlay) {
            m_fields->entryOverlay->releaseToGameplay(this);
        }
        m_fields->attemptJournalReadyAt = std::chrono::steady_clock::now();
        if (m_fields->entryAttemptCountDeferred) {
            m_fields->entryAttemptCountDeferred = false;
            PlayLayer::updateAttempts();
        }
        if (!m_isPaused) {
            FMODAudioEngine::sharedEngine()->resumeAllMusic();
            PlayLayer::startMusic();
        }
    }

    void acquireGameplayAudio() {
        auto const corumGameplayScene = m_fields->rankedLevel || m_fields->survivalLevel || m_fields->survivalFreePlayHud
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            || m_fields->debugRecordScene || m_fields->debugPlaybackScene
#endif
            ;
        if (!m_fields->rankedInitialized || !corumGameplayScene) return;
        corum::ranked::RankedAudioManager::get().fadeOutForGameplay();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene || m_fields->debugPlaybackScene)
            log::info("[DebugSpectate] Ranked gameplay audio ownership acquired");
#endif
    }

    bool observesSpectatorPlayer(PlayerObject* player) {
        if (!m_fields->rankedInitialized || player != m_player1 || !player ||
            m_isTestMode || m_isPracticeMode || m_fields->safetyRetired ||
            m_fields->spectatorMode || m_fields->spectatorPending) return false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene) return true;
        if (m_fields->debugPlaybackScene) return false;
#endif
        return m_fields->rankedLevel && !corum::ranked::RankedRuntime::get().isSpectating();
    }

    corum::ranked::SpectatorGhostPose observedSpectatorPose(bool dead = false, bool finished = false) {
        auto pose = corum::ranked::captureSpectatorPose(m_player1, dead, finished, m_gameState.m_isDualMode);
        auto const camera = m_gameState.m_cameraPosition;
        corum::ranked::SpectatorCameraOrigin origin{camera.x, camera.y};
        if (corum::ranked::validSpectatorCamera(origin)) pose.camera = origin;
        m_fields->spectatorCapture.observe(pose);
        return m_fields->spectatorCapture.snapshot().value_or(pose);
    }

    void observeSpectatorNativeUpdate(PlayerObject* player) {
        if (observesSpectatorPlayer(player)) (void)observedSpectatorPose();
    }

    void observeSpectatorNativeSnap(PlayerObject* player, corum::ranked::SpectatorDiscontinuity type,
                                    corum::ranked::SpectatorGhostPose const& before) {
        if (!observesSpectatorPlayer(player)) return;
        auto const after = corum::ranked::captureSpectatorPose(player, false, false, m_gameState.m_isDualMode);
        m_fields->spectatorCapture.nativeSnap(type, before, after);
        (void)observedSpectatorPose();
    }

    void sampleSpectatorPresentation() {
        if (!observesSpectatorPlayer(m_player1) || m_player1->m_isDead || m_hasCompletedLevel) return;
        // End of the native world update, after physics, rotation and camera.
        auto const pose = observedSpectatorPose();
        auto const ready = m_fields->spectatorCapture.stableNativeFrame(m_attemptTime,
            m_started && !m_inResetDelay && !m_isPaused && m_player1->m_gameLayer == this &&
            std::isfinite(m_player1->m_vehicleSize) && m_player1->m_vehicleSize > 0);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene) {
            if (ready && corum::ranked::debug::DebugSpectateRuntime::get().recordPose(pose)) {
                if (!m_fields->debugRecordHasFrame)
                    log::info("[DebugSpectate] initial world seed nativeElapsedMs={} x={} y={} cameraX={} cameraY={}",
                        m_attemptTime * 1000, pose.x, pose.y, pose.camera ? pose.camera->x : 0, pose.camera ? pose.camera->y : 0);
                m_fields->debugRecordHasFrame = true;
                m_fields->spectatorCapture.sampled();
            }
            return;
        }
#endif
        if (ready && corum::ranked::SpectatorLiveRuntime::get().tickPublisher(pose, false))
            m_fields->spectatorCapture.sampled();
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
        if (m_fields->rankedLevel && runtime.hasLocalAttemptInFlight()) return false;
        auto spectatorAuthority = runtime.isSpectating() && runtime.view().match.spectatorLiveEnabled;
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
        m_fields->spectatorCamera.attach(this);
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

    bool spectatorCameraActive() {
        if (!m_fields->spectatorScenePrepared || m_isTestMode || m_isPracticeMode) return false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto spectatorAuthority = runtime.isSpectating() && runtime.view().match.spectatorLiveEnabled;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) spectatorAuthority = corum::ranked::debug::DebugSpectateRuntime::get().playbackLevel(m_fields->levelId);
#endif
        return !m_fields->spectatorPresentationRetired && !m_fields->autoExitRequested &&
            m_fields->localSafety.active() && spectatorAuthority && watchableSpectatorScene();
    }

    bool beginSpectatorPresentationFrame() {
        auto const frameNumber = CCDirector::sharedDirector()->getTotalFrames();
        if (m_fields->spectatorPresentationFrameNumber == frameNumber) return false;
        m_fields->spectatorPresentationFrameNumber = frameNumber;
        m_fields->spectatorRenderedFrame = corum::ranked::SpectatorLiveRuntime::get().presentationFrame();
        auto const& rendered = m_fields->spectatorRenderedFrame;
        if (rendered && rendered->pose && m_fields->spectatorWorldDriver.contextChanged(rendered->context)) {
            // Only a server stream/epoch change can reset trigger state. Manual
            // Retry remains blocked; all native reset evidence is still guarded.
            m_fields->spectatorWorldDriver.reset();
            m_fields->localSafety.resetDummy();
            ScopedPersistenceOwner persistence(this);
            ScopedRealGameplayEnd resetting(m_fields->spectatorWorldResetInProgress, true);
            PlayLayer::resetLevel();
        }
        m_fields->spectatorWorldDriver.apply(this, spectatorCameraActive(), rendered);
        m_fields->remotePlayer.update(this, true, m_fields->spectatorRenderedFrame);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const frame = m_fields->remotePlayer.cameraFrame();
        m_fields->cameraForensicFrame = m_fields->debugPlaybackScene && frame && frame->pose &&
            m_fields->cameraForensicFrames < 8 && frame->sequence != m_fields->cameraForensicSequence &&
            (frame->sequence <= 1 || (frame->sequence >= 18 && frame->sequence <= 22));
        if (m_fields->cameraForensicFrame) {
            ++m_fields->cameraForensicFrames; m_fields->cameraForensicSequence = frame->sequence;
        }
#endif
        return true;
    }

    bool beginSpectatorWorldFrame() {
        auto const frameNumber = CCDirector::sharedDirector()->getTotalFrames();
        if (m_fields->spectatorWorldFrameNumber == frameNumber) return false;
        m_fields->spectatorWorldFrameNumber = frameNumber;
        (void)beginSpectatorPresentationFrame();
        return true;
    }

    void reconcileSpectatorWorldDriver() {
        if (spectatorCameraActive()) {
            // Own visibility at the spectator frame boundary, including the
            // wait for a remote pose. A global PlayerObject setter detour also
            // intercepts ordinary animation/Practice and is not needed here.
            if (m_player1) m_player1->setVisible(false);
            m_fields->spectatorWorldDriver.reconcile(this);
        }
    }

    void logSpectatorWorldDriver() {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const& frame = m_fields->spectatorWorldDriver.frame();
        if (!m_fields->debugPlaybackScene || !frame || !frame->pose || !m_player1) return;
        auto const& pose = *frame->pose;
        auto const& previous = m_fields->worldForensicPose;
        auto const transition = !previous || previous->mode != pose.mode || previous->upsideDown != pose.upsideDown;
        auto const snap = pose.discontinuity && frame->sequence != m_fields->worldForensicSequence;
        if (m_fields->worldForensicFrames < 24 && (transition || snap ||
            (frame->sequence != m_fields->worldForensicSequence && frame->sequence <= 2))) {
            ++m_fields->worldForensicFrames;
            log::info("[DebugSpectate] world-driver sequence={} elapsedMs={} remoteX={} remoteY={} driverX={} driverY={} mode={} driverSpider={} upsideDown={} driverUpsideDown={} cameraX={} cameraY={} nativeCameraX={} nativeCameraY={} nativeZoom={} snap={} followIndex={}",
                frame->sequence, frame->publisherElapsedMs, pose.x, pose.y, m_player1->getPositionX(), m_player1->getPositionY(),
                corum::ranked::spectatorModeName(pose.mode), m_player1->m_isSpider, pose.upsideDown, m_player1->m_isUpsideDown,
                pose.camera ? pose.camera->x : 0, pose.camera ? pose.camera->y : 0,
                m_gameState.m_cameraPosition.x, m_gameState.m_cameraPosition.y, m_gameState.m_cameraZoom,
                snap ? corum::ranked::spectatorDiscontinuityName(pose.discontinuity->type) : "NONE", m_player1->m_followRelated);
        }
        m_fields->worldForensicPose = pose; m_fields->worldForensicSequence = frame->sequence;
#endif
    }

    void logSpectatorNativeProbe() {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto& probe = m_fields->spectatorNativeProbe;
        auto const now = std::chrono::steady_clock::now();
        auto const& frame = m_fields->spectatorRenderedFrame;
        if (!m_fields->debugPlaybackScene || !frame || !frame->pose || !m_player1 ||
            probe.logs >= 120 || now < m_fields->nextNativeProbeLog) return;
        m_fields->nextNativeProbeLog = now + std::chrono::seconds(1);
        ++probe.logs;
        using P = corum::ranked::SpectatorNativePath;
        log::info("[DebugSpectate] native-path window={} frames={} sequence={} elapsedMs={} segment={} blockedUpdate={} checkCollisions={} collisionObjects={} touchedObject={} activatedObjectIDs={} touchedTrigger={} canActivate={} allowed={} teleport={} spawn={} commands={} follow={} dynamicMove={} triggerObject={} repeatedTriggerCalls={} lastX={} lastY={} x={} y={}",
            probe.logs, probe.frames, frame->sequence, frame->publisherElapsedMs, frame->presentationSegment,
            probe.count(P::BlockedPlayerUpdate), probe.count(P::CheckCollisions), probe.count(P::CollisionObjects),
            probe.count(P::TouchedObject), m_gameState.m_activatedObjectIDs.size(), probe.count(P::TouchedTrigger),
            probe.count(P::ActivationQuery), probe.count(P::ActivationAllowed), probe.count(P::Teleport),
            probe.count(P::Spawn), probe.count(P::Commands), probe.count(P::Follow), probe.count(P::DynamicMove),
            probe.count(P::TriggerObject), probe.repeatedTriggerCalls,
            m_player1->m_lastPosition.x, m_player1->m_lastPosition.y, m_player1->getPositionX(), m_player1->getPositionY());
        probe.clearWindow();
#endif
    }

    std::optional<CCPoint> spectatorNativeCameraTarget() {
        auto const frame = m_fields->remotePlayer.cameraFrame();
        if (!frame || !frame->pose || frame->pose->cameraInvalid || frame->pose->camera) return std::nullopt;
        m_fields->spectatorCamera.prepareNativeFallback(this);
        return CCPoint{frame->pose->x, frame->pose->y};
    }

    void logSpectatorNativeCamera(char const* stage) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (!m_fields->cameraForensicFrame) return;
        auto const frame = m_fields->remotePlayer.cameraFrame();
        auto const& pose = *frame->pose;
        auto const native = m_gameState.m_cameraPosition;
        auto const transform = m_objectLayer->nodeToWorldTransform();
        log::info("[SpectatorCamera] {} frame={} renderSequence={} renderElapsedMs={} remoteX={} remoteY={} hasPacketCamera={} packetCameraX={} packetCameraY={} nativeCameraX={} nativeCameraY={} objectLayerX={} objectLayerY={} viewportTX={} viewportTY={}",
            stage, CCDirector::sharedDirector()->getTotalFrames(), frame->sequence, frame->publisherElapsedMs,
            pose.x, pose.y, pose.camera.has_value(), pose.camera ? pose.camera->x : 0, pose.camera ? pose.camera->y : 0,
            native.x, native.y, m_objectLayer->getPositionX(), m_objectLayer->getPositionY(), transform.tx, transform.ty);
#else
        (void)stage;
#endif
    }

    void applySpectatorProgressPresentation() {
        if (!spectatorCameraActive() || !m_level) return;
        auto const frame = m_fields->remotePlayer.cameraFrame();
        if (!frame) return;
        auto const& runtime = corum::ranked::RankedRuntime::get();
        auto authoritative = runtime.view().match.opponentProgress;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) authoritative.reset();
#endif
        auto const percent = corum::ranked::spectatorProgressPercent(*frame, m_levelLength, m_level->m_timestamp, authoritative);
        if (!percent) return;
        if (m_progressFill) m_progressFill->setTextureRect({0, 0, m_progressWidth * *percent / 100.0f, m_progressHeight});
        if (m_percentageLabel) m_percentageLabel->setString((m_decimalPercentage
            ? fmt::format("{:.2f}%", *percent) : fmt::format("{:.0f}%", std::floor(*percent))).c_str());
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        applySpectatorProgressPresentation();
    }

    void driveSpectatorCamera() {
        auto const& frame = m_fields->spectatorRenderedFrame;
        m_fields->spectatorCamera.update(this, spectatorCameraActive(),
            frame ? std::optional(frame->context) : std::nullopt,
            m_fields->remotePlayer.cameraFrame());
        applySpectatorProgressPresentation();
    }

    void ensureSpectatorCameraFrame(float dt) {
        // Heartbeat enters the SAME world owner if vanilla has stopped; never
        // calculate camera against an unprepared frame or advance twice.
        if (spectatorCameraActive() && m_fields->spectatorWorldFrameNumber != CCDirector::sharedDirector()->getTotalFrames())
            GJBaseGameLayer::update(dt);
    }

    void cleanupSpectatorPresentation() {
        // Release ownership before destroying its visual or native scene state.
        m_fields->spectatorCamera.cleanup(this);
        m_fields->remotePlayer.cleanup();
        m_fields->spectatorWorldDriver.reset();
        m_fields->spectatorRenderedFrame.reset(); m_fields->spectatorCameraFrameNumber.reset();
        m_fields->spectatorPresentationFrameNumber.reset(); m_fields->spectatorWorldFrameNumber.reset();
    }

    void updateSpectatorView(float dt) {
        (void)dt;
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto& live = corum::ranked::SpectatorLiveRuntime::get();
        live.tickPublisher({}, true);
        auto spectatorAuthority = runtime.isSpectating() && runtime.view().match.spectatorLiveEnabled;
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
        auto const now = std::chrono::steady_clock::now();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugPlaybackScene) {
            if (m_fields->debugSpectateHud) {
                auto const waiting = live.status() == corum::ranked::SpectatorLiveStatus::Waiting;
                auto const fallback = live.status() == corum::ranked::SpectatorLiveStatus::Fallback;
                auto const buffering = live.status() == corum::ranked::SpectatorLiveStatus::Buffering;
                auto const complete = corum::ranked::debug::DebugSpectateRuntime::get().playbackComplete();
                m_fields->debugSpectateHud->setString(complete ? "SERVER PLAYBACK COMPLETE" :
                    waiting ? "LIVE SPECTATOR - WAITING FOR OPPONENT..." : buffering ? "LIVE SPECTATOR - BUFFERING..." : fallback ? "LIVE SPECTATOR - POLLING..." : "LIVE SPECTATOR - VOICE");
            }
            return;
        }
#endif
        if (runtime.view().revision != m_fields->renderedRevision || now >= m_fields->nextHudRefreshAt) {
            updateRankedHud();
            m_fields->nextHudRefreshAt = now + std::chrono::milliseconds(100);
        }
    }

    bool finishTerminalGameplayHandoff() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_fields->rankedLevel || !m_fields->attemptEndReported ||
            m_fields->vanillaGameplayEndInProgress || m_isTestMode ||
            runtime.isPlacement() || m_fields->rankedPractice || m_isPracticeMode) return false;
        // The clock is a START deadline. Never retire a genuinely open final
        // Attempt, and never leave before its already-journaled End is ACKed.
        if (runtime.hasLocalGameplayAuthority()) return false;
        if (!m_fields->spectatorMode && !m_fields->spectatorPending &&
            runtime.canEnterCurrentLevel()) return false;
        if (!m_fields->spectatorMode) m_fields->spectatorPending = true;
        syncSpectatorLocalSafety();
        if (runtime.hasLocalAttemptInFlight()) return true;

        auto const now = std::chrono::steady_clock::now();
        if (m_fields->spectatorHandoffReadyAt == std::chrono::steady_clock::time_point{})
            m_fields->spectatorHandoffReadyAt = now;
        bool const liveEnabled = runtime.view().match.spectatorLiveEnabled;
        bool const sameScene = watchableSpectatorScene();
        if (liveEnabled && sameScene && runtime.isSpectating()) return false;
        // An already-attached native viewer follows the existing authority-loss
        // cleanup path; the five-second allowance is only for initial handoff.
        if (liveEnabled && m_fields->spectatorScenePrepared) return false;
        // Permit a bounded authoritative eligibility refresh after End ACK.
        // OFF has no native spectator work to await: return to the retained
        // Ranked percent-only screen immediately, including genuine Clear UI.
        if (liveEnabled && sameScene &&
            now - m_fields->spectatorHandoffReadyAt < std::chrono::seconds(5)) return true;
        if (!m_fields->autoExitRequested) {
            m_fields->autoExitRequested = true;
            log::info("Ranked terminal gameplay returned to percentage view: live={} watchable={}",
                liveEnabled, sameScene);
            if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
        }
        return true;
    }

    bool finishConcededGameplayHandoff() {
        auto& controls = corum::ranked::RankedRuntime::get();
        if (m_fields->rankedLevel && m_fields->sceneContext.matchId == controls.view().match.matchId &&
            (m_fields->placementMapIndex > 0
                ? controls.placementMapSkipped(m_fields->sceneContext.matchId,
                    m_fields->sceneContext.roundNumber, m_fields->placementMapIndex)
                : controls.gameplayConceded(m_fields->sceneContext.roundNumber, m_fields->sceneContext.deathmatchSequence))) {
            if (m_fields->autoExitRequested) return true;
            m_fields->autoExitRequested = true; m_fields->attemptEndReported = true;
            log::info("[RankedSurrender] scene return queued level={} round={} deathmatch={}",
                m_fields->levelId, m_fields->sceneContext.roundNumber, m_fields->sceneContext.deathmatchSequence);
            // Never destroy a PlayLayer/PauseLayer while its native update or
            // menu callback is still running. The exact departed scene owns it.
            auto scene = geode::WeakRef<PlayLayer>(this);
            queueInMainThread([scene] {
                if (auto layer = scene.lock(); layer && layer.data() == PlayLayer::get() &&
                    layer->getParent() == CCDirector::sharedDirector()->getRunningScene()) {
                    // This callback is outside the native onQuit detour. Enter
                    // the binding so the installed quit chain runs normally.
                    if (corum::ranked::quitRankedPauseForConcession(layer.data())) return;
                    // Native Resume/Quit callbacks may already have departed
                    // this scene. Recheck before the independent fallback.
                    if (layer.data() == PlayLayer::get() &&
                        layer->getParent() == CCDirector::sharedDirector()->getRunningScene() &&
                        !corum::ranked::quitThroughRankedCompletionOverlay(layer.data())) layer->onQuit();
                }
            });
            return true;
        }
        return false;
    }

    void spectatorFrame(float dt) {
        if (blockEntryGameplayUpdate() || finishConcededGameplayHandoff()) return;
        if (!m_fields->rankedInitialized || m_fields->safetyRetired ||
            m_fields->spectatorPresentationRetired || m_fields->autoExitRequested ||
            m_isTestMode || m_fields->vanillaGameplayEndInProgress) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_fields->spectatorMode && !m_fields->spectatorPending &&
            !m_fields->attemptEndReported && !runtime.isSpectating()) return;
        runtime.tick();
        syncSpectatorLocalSafety();
        if (finishTerminalGameplayHandoff()) return;
        if (m_fields->spectatorMode) {
            updateSpectatorView(dt);
            ensureSpectatorCameraFrame(dt);
            return;
        }
    }

    void postUpdate(float dt) {
        if (blockEntryGameplayUpdate() || finishConcededGameplayHandoff()) return;
        if (m_fields->rankedLevel) corum::ranked::RankedRuntime::get().syncMatchGameplayPause(m_isPaused);
        syncSpectatorLocalSafety();
        ScopedPersistenceOwner persistence(this);
        PlayLayer::postUpdate(dt);
        if(m_fields->survivalFreePlayHud)m_fields->fpsMeter.observeFrame(steadyNowMicros());
        if(m_fields->survivalLevel && m_fields->rankedInitialized && !m_isTestMode) {
            auto& survival=corum::ranked::SurvivalRuntime::get();m_fields->fpsMeter.observeFrame(steadyNowMicros());
            if(!m_isPracticeMode && !m_isPaused && m_player1 && !m_player1->m_isDead && !m_hasCompletedLevel && survival.canPlay()) {
                if(!m_fields->attemptStartReported)m_fields->attemptStartReported=survival.beginAttempt();
                if(m_fields->attemptStartReported && !m_fields->attemptEndReported)survival.progress(rankedProgressPercent(this));
            }
            return;
        }
        reconcileSpectatorWorldDriver();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene) {
            if (m_fields->debugSpectateHud) m_fields->debugSpectateHud->setString(corum::ranked::debug::DebugSpectateRuntime::get().status().c_str());
            return;
        }
#endif
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        syncSpectatorLocalSafety();
        if (!m_fields->rankedLevel || m_isTestMode) return;
        if (finishConcededGameplayHandoff()) return;
        if (finishTerminalGameplayHandoff()) return;
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
                    if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
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
                if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
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
            if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
            return;
        }


        auto const& matchState = runtime.view().match.state;
        auto const stillPlaying =
            runtime.nativeEntryStartGranted() ||
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
            if (localAttemptStillAlive && matchState != "CANCELLED" && !runtime.view().match.decisiveRoundEnded) {
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
                if (runtime.view().match.decisiveRoundEnded) m_fields->attemptEndReported = true;
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
                if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
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
        m_fields->spectatorCapture.reset();
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
            corum::ranked::ScopedSpectatorPersistenceMode noSave(m_isTestMode, blocksLevelPersistence(m_level));
            PlayLayer::resetLevel();
            if (m_player1) m_player1->setVisible(false);
            return;
        }
        if(m_fields->survivalLevel) {
            if(m_fields->rankedInitialized && !m_fields->nativeEntryHeld) {
                auto& survival=corum::ranked::SurvivalRuntime::get();
                if(!m_fields->attemptEndReported)survival.endAttempt(m_hasCompletedLevel?100.0:rankedProgressPercent(this));
                if(survival.attemptLimitReached() || !survival.canPlay()) {m_fields->survivalWaiting=true;m_isPaused=true;return;}
                m_fields->attemptStartReported=m_fields->attemptEndReported=false;
            }
            PlayLayer::resetLevel();return;
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
            m_fields->nativeCompletionStarted = false;
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
            auto const cleared = m_hasCompletedLevel;
            auto const progress = cleared ? 100.0 : rankedProgressPercent(this);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId,
                progress,
                cleared,
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
                // End transmission may still be in flight. Do not create a
                // fresh visual Attempt; the frame heartbeat owns ACK/eligibility
                // and the OFF/timeout return through the scoped native quit.
                if (finishTerminalGameplayHandoff()) return;
                corum::ranked::ScopedSpectatorPersistenceMode noSave(m_isTestMode, blocksLevelPersistence(m_level));
                PlayLayer::resetLevel();
                if (m_player1) m_player1->setVisible(false);
                return;
            }
            if (!m_fields->autoExitRequested) {
                m_fields->autoExitRequested = true;
                if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
            }
            return;
        }
        PlayLayer::resetLevel();
        m_fields->nativeCompletionStarted = false;
        if (!resetActions.startFreshAttempt) return;
        corum::ranked::SpectatorLiveRuntime::get().localAttemptStarted();
        m_fields->spectatorPending = false;
        m_fields->spectatorHandoffReadyAt = {};
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
        if(m_fields->survivalLevel) {
            auto const anticheat=object&&m_anticheatSpike&&object->m_uniqueID==m_anticheatSpike->m_uniqueID;
            auto const eligible=m_fields->rankedInitialized && !m_fields->nativeEntryHeld && !m_isPracticeMode && !anticheat &&
                player && (player==m_player1 || (m_gameState.m_isDualMode&&player==m_player2)) && !player->m_isDead && !m_hasCompletedLevel;
            auto const progress=eligible?rankedProgressPercent(this):0.0;
            PlayLayer::destroyPlayer(player,object);
            if(eligible && player->m_isDead && !m_hasCompletedLevel && !m_fields->attemptEndReported) {
                corum::ranked::SurvivalRuntime::get().endAttempt(progress);m_fields->attemptEndReported=true;
            }
            return;
        }
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

        // A native callback is not proof that a player died. GD can refuse a
        // collision or run cleanup after completion. The supplied BLOZ log
        // closed three ~99.1% finishes as non-clears before completion. Do not
        // let an unconfirmed callback make the per-attempt guard discard a clear.
        auto const exactPlayer = player && (player == m_player1 ||
            (m_gameState.m_isDualMode && player == m_player2));
        auto const deathEligible = exactPlayer && !player->m_isDead && !m_hasCompletedLevel &&
            !m_fields->nativeCompletionStarted &&
            scoringEnabled() && !m_fields->attemptEndReported;
        auto const deathProgress = deathEligible ? rankedProgressPercent(this) : 0.0;
        auto const deathPose = deathEligible && m_player1
            ? std::optional(observedSpectatorPose(true, false)) : std::nullopt;
        ScopedRealGameplayEnd finishing(m_fields->vanillaGameplayEndInProgress, deathEligible);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        // Death effects may mutate native presentation. Record the real contact
        // pose captured before vanilla, but commit it only after confirmed death.
        auto const debugTerminal = m_fields->debugRecordScene && player == m_player1
            ? std::optional(observedSpectatorPose(true, false)) : std::nullopt;
#endif
        PlayLayer::destroyPlayer(player, object);
        // Keep the pre-effect progress/pose, but publish only a confirmed native
        // death. Nested reset/completion paths may already have ended the Attempt.
        if (deathEligible && player->m_isDead && !m_hasCompletedLevel && !m_fields->nativeCompletionStarted &&
            !m_fields->attemptEndReported && scoringEnabled()) {
            if (deathPose) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(*deathPose, false);
            m_fields->attemptEndReported = runtime.reportAttemptEnd(
                m_fields->levelId, deathProgress, false, m_fields->qualifyingPercent, true
            );
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (debugTerminal && player && player->m_isDead) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(*debugTerminal, true);
        }
#endif
    }

    void levelComplete() {

        auto& runtime = corum::ranked::RankedRuntime::get();
        if (suppressLocalCompletion()) return;
        if(m_fields->survivalLevel) {
            if(m_fields->rankedInitialized && !m_isPracticeMode && !m_fields->attemptEndReported) {
                corum::ranked::SurvivalRuntime::get().endAttempt(100);m_fields->attemptEndReported=true;
            }
            PlayLayer::levelComplete();return;
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && m_player1) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(observedSpectatorPose(false, true), true);
        }
#endif
        ScopedRealGameplayEnd finishing(
            m_fields->vanillaGameplayEndInProgress,
            scoringEnabled() && !m_fields->attemptEndReported
        );
        if (scoringEnabled() && !m_fields->attemptEndReported) {
            if (m_player1) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(
                observedSpectatorPose(false, true), false);
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
        if(m_fields->survivalLevel) {
            if(m_fields->rankedInitialized && !m_isPracticeMode && !m_fields->attemptEndReported) {
                corum::ranked::SurvivalRuntime::get().endAttempt(100);m_fields->attemptEndReported=true;
            }
            PlayLayer::showCompleteText();return;
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (m_fields->debugRecordScene && m_player1) {
            corum::ranked::debug::DebugSpectateRuntime::get().recordPose(observedSpectatorPose(false, true), true);
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
                observedSpectatorPose(false, true), false);
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
        auto& survival=corum::ranked::SurvivalRuntime::get();
        auto const freePlayReturn=m_fields->survivalFreePlayHud && survival.freePlayReturnPending();
        if(freePlayReturn && !corum::ranked::resumeSurvivalFreePlayPauseBeforeQuit(this))return;
        if(freePlayReturn)survival.beginFreePlayNativeQuit(m_level);
        if(m_fields->survivalLevel) {
            if(!corum::ranked::resumeRankedPauseBeforeQuit(this))return;
            unschedule(schedule_selector(CorumRankedPlayLayer::survivalFrame));
            if(!m_fields->autoExitRequested && !m_isPracticeMode)corum::ranked::SurvivalRuntime::get().endAttempt(m_hasCompletedLevel?100.0:rankedProgressPercent(this));
        }

        if (m_fields->rankedLevel && !corum::ranked::resumeRankedPauseBeforeQuit(this)) {
            if (!m_fields->pauseQuitBlockedLogged) {
                m_fields->pauseQuitBlockedLogged = true;
                log::error("[RankedPause] native quit blocked: scene still paused level={}", m_fields->levelId);
            }
            return;
        }
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
            auto const cleared = m_hasCompletedLevel;
            auto const progress = cleared ? 100.0 : rankedProgressPercent(this);
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId,
                progress,
                cleared,
                m_fields->qualifyingPercent
            );
        }
        corum::ranked::ScopedRankedRoundQuit roundQuit(this);
        corum::ranked::ScopedSpectatorPersistenceMode noSave(m_isTestMode, blocksLevelPersistence(m_level));
        PlayLayer::onQuit();
        if(freePlayReturn)survival.finishFreePlayNativeQuit();
    }

    void onExit() {
        unschedule(schedule_selector(CorumRankedPlayLayer::survivalFrame));
        if (m_fields->entryOverlay) {
            m_fields->entryOverlay->dismiss();
            m_fields->entryOverlay = nullptr;
        }
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
        corum::ranked::ScopedSpectatorPersistenceMode noSave(m_isTestMode, blocksLevelPersistence(m_level));
        bool const appPausedBefore = AppDelegate::get() && AppDelegate::get()->m_gamePaused;
        PlayLayer::onExit();
        if (m_fields->rankedLevel) log::info("[RankedEntry] native exit level={} appPausedBefore={} appPausedAfter={}",
            m_fields->levelId, appPausedBefore, AppDelegate::get() && AppDelegate::get()->m_gamePaused);
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
        // End animation may retire a dual avatar before levelComplete sets its
        // native flag. Preserve the current Attempt until the real clear, even
        // if the round clock expires while the completion animation is running.
        if (m_player1 && !m_player1->m_isDead && scoringEnabled() && !m_fields->attemptEndReported)
            m_fields->nativeCompletionStarted = true;
        PlayLayer::playEndAnimationToPos(position);
    }

    void playPlatformerEndAnimationToPos(CCPoint position, bool instant) {
        if (suppressLocalCompletion()) return;
        if (m_player1 && !m_player1->m_isDead && scoringEnabled() && !m_fields->attemptEndReported)
            m_fields->nativeCompletionStarted = true;
        PlayLayer::playPlatformerEndAnimationToPos(position, instant);
    }

    void showEndLayer() {
        if (suppressLocalCompletion()) return;
        // Back up the two completion callbacks using native state, never a
        // rounded progress percentage. Custom-end maps may finish below 100%.
        ScopedRealGameplayEnd finishing(m_fields->vanillaGameplayEndInProgress,
            m_hasCompletedLevel && scoringEnabled() && !m_fields->attemptEndReported);
        if (m_hasCompletedLevel && scoringEnabled() && !m_fields->attemptEndReported) {
            if (m_player1) corum::ranked::SpectatorLiveRuntime::get().publishTerminal(
                observedSpectatorPose(false, true), false);
            m_fields->attemptEndReported = corum::ranked::RankedRuntime::get().reportAttemptEnd(
                m_fields->levelId, 100.0, true, m_fields->qualifyingPercent
            );
        }
        PlayLayer::showEndLayer();
        if(m_fields->survivalLevel && !m_isPracticeMode && m_hasCompletedLevel)m_fields->survivalNativeClearFinished=true;
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (blocksLocalEvidence()) return;
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void updateAttempts() {
        if (m_fields->nativeEntryHeld) {
            m_fields->entryAttemptCountDeferred = true;
            return;
        }
        if (blocksLocalEvidence()) return;
        PlayLayer::updateAttempts();
    }

    void togglePracticeMode(bool practiceMode) {
        if(m_fields->survivalLevel) {
            if(practiceMode && !m_isPracticeMode)corum::ranked::SurvivalRuntime::get().endAttempt(rankedProgressPercent(this));
            m_fields->attemptStartReported=m_fields->attemptEndReported=false;PlayLayer::togglePracticeMode(practiceMode);return;
        }
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
            if (!corum::ranked::quitThroughRankedCompletionOverlay(this)) onQuit();
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
        auto const weight=std::string_view(fontRole)=="black"?corum::ranked::ui::FontWeight::Black:
            std::string_view(fontRole)=="goldFont.fnt"?corum::ranked::ui::FontWeight::ExtraBold:
            std::string_view(fontRole)=="medium"?corum::ranked::ui::FontWeight::Medium:corum::ranked::ui::FontWeight::SemiBold;
        auto const pair=corum::ranked::ui::rankedHudLabel(parent,text,weight,scale,anchor,position,color,zOrder);
        m_fields->hudShadows.emplace(pair.foreground,pair.shadow);return pair.foreground;
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
        corum::ranked::ui::fitLabelWidth(foreground, width, maximumScale, minimumScale);
        if (auto* shadow = hudShadow(foreground)) {
            corum::ranked::ui::fitLabelWidth(shadow, width, maximumScale, minimumScale);
        }
    }

    void addFpsHud(CCNode* root,float top) {
        m_fields->fpsLabel = addShadowedHudLabel(
            root,
            "FPS : -",
            "bigFont.fnt",
            0.51f,
            {0.0f, 1.0f},
            {7.0f, top - 0.5f},
            corum::ranked::ui::Text
        );
        m_fields->fpsLabel->setID("ranked-fps"_spr);
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
        addFpsHud(m_fields->hudRoot,top);

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
            corum::ranked::CompetitiveClockScale,
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
            {m_fields->qualifyingCaption->getPositionX() + m_fields->qualifyingCaption->getScaledContentSize().width + 10.0f, layout.bottomY + 1.0f},
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

    void updateDeathFeed() {
        auto const& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        auto const now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        auto const serverNow = runtime.serverNowMillis();
        auto const context = match.matchId + ":" + std::to_string(match.roundNumber) + ":" +
            std::to_string(match.deathmatchSequence) + ":" + std::to_string(m_fields->levelId);
        auto& feed = m_fields->deathFeed;
        feed.reset(context); feed.observe(match.deathLog, now, serverNow);
        auto& visuals = m_fields->deathRows;
        std::erase_if(visuals, [&](auto const& visual) {
            if (std::any_of(feed.rows().begin(), feed.rows().end(), [&](auto const& row) { return row.event.id == visual.id; })) return false;
            m_fields->hudShadows.erase(visual.label); visual.node->removeFromParentAndCleanup(true); return true;
        });
        for (std::size_t index = 0; index < feed.rows().size(); ++index) {
            auto const& row = feed.rows()[index];
            auto found = std::find_if(visuals.begin(), visuals.end(), [&](auto const& v) { return v.id == row.event.id; });
            auto const y = layout.bottomY + 31.0f + static_cast<float>(index) * 16.0f;
            if (found == visuals.end()) {
                auto* node = CCNode::create(); node->setAnchorPoint(CCPointZero);
                node->setPosition({layout.bottomLeftX + 4.0f, y}); m_fields->hudRoot->addChild(node, 5);
                auto const name = row.event.side == "A" ? match.playerAName : match.playerBName;
                auto* text = addShadowedHudLabel(node, fmt::format("{} {}%", name, static_cast<int>(std::floor(std::clamp(static_cast<double>(row.event.progressPercent),0.0,100.0)))),
                    "black", 0.65f, CCPointZero, CCPointZero,
                    row.event.side == match.side ? corum::ranked::ui::Red : corum::ranked::ui::Green);
                limitHudWidth(text, std::min(220.0f, size.width * .46f), 0.65f, 0.40f);
                text->setOpacity(0);text->runAction(CCFadeIn::create(.18f));
                if(auto* shadow=hudShadow(text)){auto const opacity=shadow->getOpacity();shadow->setOpacity(0);shadow->runAction(CCFadeTo::create(.18f,opacity));}
                visuals.push_back({row.event.id, node, text, y, false}); found = std::prev(visuals.end());
            }
            auto const age = now - row.createdAtMs;
            if (age >= 5000 && !found->fading) {
                found->fading = true; found->node->stopAllActions();
                auto const duration = static_cast<float>(std::max(.01, (5250 - age) / 1000));
                found->node->runAction(CCEaseSineIn::create(CCMoveBy::create(duration, {0, -8})));
                found->label->runAction(CCFadeTo::create(duration, 0));
                if (auto* shadow = hudShadow(found->label)) shadow->runAction(CCFadeTo::create(duration, 0));
            } else if (!found->fading && found->targetY != y) {
                found->targetY = y; found->node->stopAllActions();
                found->node->runAction(CCEaseSineOut::create(CCMoveTo::create(.22f, {layout.bottomLeftX + 4.0f, y})));
            }
        }
    }

    void updateRankedHud() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        m_fields->renderedRevision = runtime.view().revision;
        if (!m_fields->hudRoot || !m_fields->fpsLabel || !m_fields->ownScoreLabel) return;

        updateDeathFeed();
        auto const placement = runtime.isPlacement();

        auto attemptsUsedA = match.deathmatchAttemptsUsedA;
        auto attemptsUsedB = match.deathmatchAttemptsUsedB;
        if (match.deathmatchSequence > 0) {
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
                : runtime.nativeEntryStartGranted()
                    ? (match.deathmatchSequence > 0 ? "DEATHMATCH_PLAYING" : "ROUND_PLAYING")
                    : match.state,
            .banner = match.banner,
            .roundNumber = match.roundNumber,
            .scoreA = match.scoreA,
            .scoreB = match.scoreB,
            .clearsA = match.clearsA,
            .clearsB = match.clearsB,
            .deathmatch = match.deathmatchSequence > 0,
            .deathmatchSequence = match.deathmatchSequence,
            .deathmatchAttemptsUsedA = attemptsUsedA,
            .deathmatchAttemptsUsedB = attemptsUsedB,
            .deathmatchAttemptsCompletedA = match.deathmatchAttemptsCompletedA,
            .deathmatchAttemptsCompletedB = match.deathmatchAttemptsCompletedB,
            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,
            .remainingMillis = runtime.nativeEntryStartGranted() && match.deathmatchSequence == 0
                ? std::optional<std::int64_t>(300'000 + runtime.signedDeadlineMillis().value_or(0))
                : runtime.deadlineMillis(),
            .signedRemainingMillis = runtime.nativeEntryStartGranted() && match.deathmatchSequence == 0
                ? std::optional<std::int64_t>(300'000 + runtime.signedDeadlineMillis().value_or(0))
                : runtime.signedDeadlineMillis(),
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
        setHudText(m_fields->ownScoreLabel, formatScore(ownLiveScore,match.deathmatchSequence>0));
        limitHudWidth(m_fields->ownScoreLabel, 76.0f, 0.714f, 0.26f);
        auto const opponentScore = ownIsA ? match.scoreB : match.scoreA;
        setHudText(m_fields->opponentScoreLabel, formatScore(opponentScore,match.deathmatchSequence>0));
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
                status == corum::ranked::SpectatorLiveStatus::Buffering ? "BUFFERING..." :
                status == corum::ranked::SpectatorLiveStatus::Fallback ? "POLLING..." :
                m_fields->spectatorCamera.state() == corum::ranked::SpectatorCameraState::Waiting ?
                    "WAITING..." : presentation.spectatorProgressText);
            limitHudWidth(m_fields->spectatorProgressLabel, 72.0f, 0.34f, 0.16f);
            setHudText(m_fields->spectatorTimerLabel, presentation.timerText);
        }
    }
};

// The native PlayLayer may stop receiving postUpdate/scheduled callbacks while
// its exact server-authorized entry remains held. The process-owned preparation
// heartbeat drives this same gate without unpausing physics or inventing entry
// acknowledgement, start certificates, or client-side countdown deadlines.
void corum::ranked::pumpHeldRankedEntry(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return;
    auto* managed = static_cast<CorumRankedPlayLayer*>(layer);
    auto* director = CCDirector::sharedDirector();
    if (!managed->m_fields->rankedInitialized ||
        (!managed->m_fields->rankedLevel && !managed->m_fields->survivalLevel) ||
        !managed->m_fields->nativeEntryHeld ||
        managed->m_fields->entryNativeCompletionInProgress ||
        managed->m_fields->autoExitRequested || managed->m_fields->safetyRetired ||
        !layer->getParent() ||
        layer->getParent() != director->getRunningScene() ||
        director->getNextScene()) return;
    auto const now = std::chrono::steady_clock::now();
    if (now < managed->m_fields->nextIndependentEntryPulseAt) return;
    managed->m_fields->nextIndependentEntryPulseAt = now + std::chrono::milliseconds(100);
    if (managed->m_fields->independentEntryLogCount++ < 3)
        log::info("[RankedEntry] process pulse level={} survival={} running={} paused={}",
            managed->m_fields->levelId,managed->m_fields->survivalLevel,layer->isRunning(),layer->m_isPaused);
    managed->blockEntryGameplayUpdate();
}

bool corum::ranked::prepareSpectatorScene(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return false;
    return static_cast<CorumRankedPlayLayer*>(layer)->prepareSpectatorScene();
}
bool corum::ranked::isOfficialSurvivalPlayLayer(PlayLayer* layer) {
    return layer && layer==PlayLayer::get() && static_cast<CorumRankedPlayLayer*>(layer)->m_fields->survivalLevel;
}
bool corum::ranked::requestSurvivalGameplayReturn(PlayLayer* layer) {
    if(!layer || layer!=PlayLayer::get())return false;
    auto* managed=static_cast<CorumRankedPlayLayer*>(layer);
    if(!managed->m_fields->survivalLevel && !managed->m_fields->survivalFreePlayHud)return false;
    auto const alreadyQueued=managed->m_fields->autoExitRequested;
    managed->queueSurvivalGameplayReturn();
    if(!alreadyQueued)geode::log::info("[SurvivalExit] queued={} running={} retired={} initialized={} sceneOwned={} nextScene={}",
        managed->m_fields->autoExitRequested,layer->isRunning(),managed->m_fields->safetyRetired,managed->m_fields->rankedInitialized,
        layer->getParent()==CCDirector::sharedDirector()->getRunningScene(),CCDirector::sharedDirector()->getNextScene()!=nullptr);
    return managed->m_fields->autoExitRequested;
}
void corum::ranked::pumpSurvivalGameplayExit(PlayLayer* layer){
    if(!layer || layer!=PlayLayer::get())return;
    auto* managed=static_cast<CorumRankedPlayLayer*>(layer);auto& fields=*managed->m_fields.self();
    if(fields.safetyRetired || !fields.rankedInitialized || (!fields.survivalLevel && !fields.survivalFreePlayHud))return;
    auto& runtime=SurvivalRuntime::get();
    bool const departing=fields.survivalFreePlayHud?runtime.freePlayReturnPending():
        !runtime.surrenderPending() && (runtime.view().ownEliminated || runtime.normalRoundCleared() ||
        runtime.preparationContext()!=fields.survivalContext || runtime.view().state=="ROUND_RESULT" ||
        runtime.view().state=="FINAL_RESULT" || runtime.view().state=="CANCELLED");
    if(departing)managed->queueSurvivalGameplayReturn();
}

bool corum::ranked::requestConcededGameplayReturn(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return false;
    return static_cast<CorumRankedPlayLayer*>(layer)->finishConcededGameplayHandoff();
}

namespace {
CorumRankedPlayLayer* localSafetyLayer() {
    auto* layer = persistenceOwner ? persistenceOwner : PlayLayer::get();
    return layer ? static_cast<CorumRankedPlayLayer*>(layer) : nullptr;
}
} // namespace

// One final viewport owner. Ordinary gameplay delegates unchanged; only a
// scoped missing-camera fallback borrows the dummy as a native camera target.
class $modify(CorumRankedSpectatorCameraHook, GJBaseGameLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::updateCamera", Priority::First)) {
            log::warn("Failed to install spectator camera priority");
        }
        if (!self.setHookPriorityPre("GJBaseGameLayer::update", Priority::First)) {
            log::warn("Failed to install spectator world-frame ownership priority");
        }
    }
    void updateCamera(float dt) {
        auto* layer = PlayLayer::get();
        auto* owner = layer && static_cast<GJBaseGameLayer*>(layer) == this
            ? static_cast<CorumRankedPlayLayer*>(layer) : nullptr;
        if (!owner || !owner->spectatorCameraActive()) {
            GJBaseGameLayer::updateCamera(dt); return;
        }
        auto const frameNumber = CCDirector::sharedDirector()->getTotalFrames();
        // World ownership prepares the frame first. Repeated native sub-step /
        // heartbeat calls cannot overwrite the final viewport in this frame.
        if (owner->m_fields->spectatorWorldResetInProgress ||
            owner->m_fields->spectatorWorldFrameNumber != frameNumber ||
            owner->m_fields->spectatorCameraFrameNumber == frameNumber) return;
        owner->m_fields->spectatorCameraFrameNumber = frameNumber;
        owner->reconcileSpectatorWorldDriver();
        owner->logSpectatorNativeCamera("before vanilla");
        {
            corum::ranked::ScopedCameraTargetOverride target(m_player1, owner->spectatorNativeCameraTarget());
            GJBaseGameLayer::updateCamera(dt);
        }
        owner->logSpectatorNativeCamera("after vanilla");
        if (!owner->m_fields->spectatorWorldUpdateInProgress) {
            owner->driveSpectatorCamera();
            owner->logSpectatorNativeCamera("after spectator correction");
        }
    }

    void update(float dt) {
        auto* layer = PlayLayer::get();
        auto* owner = layer && static_cast<GJBaseGameLayer*>(layer) == this
            ? static_cast<CorumRankedPlayLayer*>(layer) : nullptr;
        if (owner && owner->blockEntryGameplayUpdate()) return;
        if (owner && owner->prepareSpectatorScene() && owner->spectatorCameraActive()) {
            if (!owner->beginSpectatorWorldFrame()) return;
            {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
                corum::ranked::ScopedSpectatorNativeProbe nativeProbe(owner->m_fields->spectatorNativeProbe,
                    owner, m_player1, owner->m_fields->debugPlaybackScene);
#endif
                ScopedRealGameplayEnd worldUpdate(owner->m_fields->spectatorWorldUpdateInProgress, true);
                GJBaseGameLayer::update(dt);
                owner->logSpectatorNativeProbe();
                owner->reconcileSpectatorWorldDriver();
                // Some native paths omit updateCamera. Its hook still owns the
                // one calculation, with correction deferred until world exits.
                if (owner->m_fields->spectatorCameraFrameNumber != CCDirector::sharedDirector()->getTotalFrames())
                    updateCamera(dt);
            }
            owner->driveSpectatorCamera();
            owner->logSpectatorNativeCamera("after spectator correction");
            owner->logSpectatorWorldDriver();
            return;
        }
        GJBaseGameLayer::update(dt);
        if (owner) owner->sampleSpectatorPresentation();
    }

    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        auto* layer = PlayLayer::get();
        auto* owner = layer && static_cast<GJBaseGameLayer*>(layer) == this
            ? static_cast<CorumRankedPlayLayer*>(layer) : nullptr;
        auto const observed = owner && object && owner->observesSpectatorPlayer(player);
        auto const before = observed ? owner->observedSpectatorPose() : corum::ranked::SpectatorGhostPose{};
        GJBaseGameLayer::teleportPlayer(object, player);
        if (observed) owner->observeSpectatorNativeSnap(player, corum::ranked::SpectatorDiscontinuity::Teleport, before);
    }
};

// Stop only the dummy's own physics at the completion boundary. Do not pause
// PlayLayer/world/relay updates: a distinct Phase-2 remote player must be free
// to display 100%. checkForEnd + completion callbacks cover a large single step.
class $modify(CorumRankedSpectatorDummyPlayer, PlayerObject) {
    static void onModify(auto& self) {
        for (auto name : {"PlayerObject::update", "PlayerObject::updateRotation", "PlayerObject::spiderTestJump",
                          "PlayerObject::setPosition", "PlayerObject::setRotation"}) {
            if (!self.setHookPriorityPre(name, Priority::First))
                log::warn("Failed to install spectator world-driver safety priority: {}", name);
        }
#if !defined(GEODE_IS_WINDOWS)
        if (!self.setHookPriorityPre("PlayerObject::updateSpecial", Priority::First))
            log::warn("Failed to install spectator follow-history safety priority");
#endif
    }

    void update(float dt) {
        auto* layer = localSafetyLayer();
        if (layer && layer->freezeLocalDummy(this)) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            corum::ranked::noteSpectatorNativePath(layer, this, corum::ranked::SpectatorNativePath::BlockedPlayerUpdate);
#endif
            return;
        }
        auto const observed = layer && layer->observesSpectatorPlayer(this);
        auto const teleportedBefore = m_wasTeleported;
        auto const before = observed ? layer->observedSpectatorPose() : corum::ranked::SpectatorGhostPose{};
        PlayerObject::update(dt);
        // Native teleport flag also covers object-driven relocation outside the
        // portal entry callback. A position delta alone never creates evidence.
        if (observed && !teleportedBefore && m_wasTeleported)
            layer->observeSpectatorNativeSnap(this, corum::ranked::SpectatorDiscontinuity::Teleport, before);
        if (layer) layer->observeSpectatorNativeUpdate(this);
    }

    void updateRotation(float dt) {
        if (auto* layer = localSafetyLayer(); layer && layer->freezeLocalDummy(this)) return;
        PlayerObject::updateRotation(dt);
        if (auto* layer = localSafetyLayer()) layer->observeSpectatorNativeUpdate(this);
    }

    void spiderTestJump(bool dynamic) {
        auto* layer = localSafetyLayer();
        if (layer && layer->freezeLocalDummy(this)) return;
        auto const observed = layer && m_isSpider && layer->observesSpectatorPlayer(this);
        auto const before = observed ? layer->observedSpectatorPose() : corum::ranked::SpectatorGhostPose{};
        PlayerObject::spiderTestJump(dynamic);
        if (observed && before.upsideDown != m_isUpsideDown)
            layer->observeSpectatorNativeSnap(this, corum::ranked::SpectatorDiscontinuity::SpiderSnap, before);
    }

    void setPosition(CCPoint const& position) {
        auto* layer = localSafetyLayer();
        if (layer && layer->blocksSpectatorNativePlayerWrite(this)) return;
        PlayerObject::setPosition(position);
    }

    void setRotation(float rotation) {
        auto* layer = localSafetyLayer();
        if (layer && layer->blocksSpectatorNativePlayerWrite(this)) return;
        PlayerObject::setRotation(rotation);
    }

#if !defined(GEODE_IS_WINDOWS)
    // Windows has no hookable native address: the SDK provides an inline body.
    // Its physics caller is blocked by update(); the driver calls that narrow
    // inline sampler directly. iOS/Android can guard the native helper as well.
    void updateSpecial(float dt) {
        auto* layer = localSafetyLayer();
        if (layer && layer->freezeLocalDummy(this) &&
            !corum::ranked::ScopedSpectatorWorldDriverWrite::allows(this)) return;
        PlayerObject::updateSpecial(dt);
    }
#endif

};

// Explicit persistence firewall, independent of m_isPracticeMode/test mode.
// Only the exact level object of this dummy scene is guarded. Unrelated levels,
// normal Ranked, Placement, real Death Match and user Practice retain vanilla.
class $modify(CorumRankedSpectatorProgressSave, GJGameLevel) {
    static void onModify(auto& self) {
        for (auto name : {"GJGameLevel::savePercentage"}) {
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

    // Do not register saveNewScore or storeNewLocalScore. Genuine completion
    // must reach GD's score/string implementation without a Corum detour.
    // Spectator score writes are prevented at the scene's completion/death
    // boundaries, with scoped native no-save mode for reset/quit/exit cleanup.
};
