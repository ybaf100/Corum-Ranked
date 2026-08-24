#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "domain/HudPresentation.hpp"
#include "domain/AttemptScoring.hpp"
#include "domain/RenderFpsMeter.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

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

} // namespace

class $modify(CorumRankedPlayLayer, PlayLayer) {
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
        CCNode* spectatorPanel = nullptr;
        CCLabelBMFont* spectatorNameLabel = nullptr;
        CCLabelBMFont* spectatorProgressLabel = nullptr;
        CCLabelBMFont* spectatorTimerLabel = nullptr;
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
        bool autoExitRequested = false;
        bool gameplayFrameObserved = false;
        bool attemptStartReported = false;
        bool attemptEndReported = false;
        bool deniedDeathmatchVisualAttempt = false;
        bool prematureResultLogged = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        m_fields->levelId = level ? static_cast<int>(level->m_levelID) : 0;
        m_fields->rankedLevel =
            runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
            runtime.isGameplayLevel(m_fields->levelId);
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
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return true;

        addRankedHud();
        m_fields->gameplayFrameObserved = false;
        m_fields->attemptJournalReadyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        m_fields->gameplayFrameObserved = true;
        auto const now = std::chrono::steady_clock::now();

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

        // If our accepted final attempt has already ended and the server has
        // switched this side into spectator mode, leave PlayLayer immediately.
        // The opponent keeps playing their accepted attempt; the Ranked scene
        // renders their live progress instead of leaving us on a completed/dead
        // level screen until they finish.
        if (
            runtime.isSpectating() && m_fields->attemptEndReported &&
            !m_hasCompletedLevel &&
            !m_fields->autoExitRequested
        ) {
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

        m_fields->fpsMeter.observeFrame(steadyNowMicros());

        if (!runtime.isSpectating()) {
            runtime.reportAttemptProgress(
                m_fields->levelId,
                rankedProgressPercent(this)
            );
        }
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
            auto const startWindowExpired =
                ((match.state == "FINAL_ATTEMPT_WINDOW" || match.state == "LAST_ATTEMPT_WINDOW") &&
                 runtime.deadlineMillis().value_or(1) <= 0) ||
                match.state == "ROUND_SETTLING";
            auto const newAttemptForbidden = startWindowExpired || !runtime.canEnterCurrentLevel();
            if (newAttemptForbidden) {
                // The current attempt may finish after the start deadline, but
                // once a new start is forbidden (closed window, settling, result,
                // spectator, or exhausted Death Match budget), vanilla reset must
                // never create a fake visual attempt. Exit immediately so Ranked can show the result
                // or the opponent spectator state instead.
                if (!m_fields->autoExitRequested) {
                    m_fields->autoExitRequested = true;
                    PlayLayer::onQuit();
                }
                return;
            }
        }
        PlayLayer::resetLevel();
        if (!m_fields->rankedLevel || m_isPracticeMode || m_isTestMode) return;
        m_fields->attemptStartReported = false;
        m_fields->attemptEndReported = false;
        m_fields->attemptStartReported = runtime.reportAttemptStart(m_fields->levelId);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
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

    CCLabelBMFont* addShadowedHudLabel(
        CCNode* parent,
        char const* text,
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
            0.30f,
            {0.0f, 1.0f},
            {7.0f, top - 0.5f},
            corum::ranked::ui::Text
        );
        m_fields->fpsLabel->setID("ranked-fps"_spr);

        m_fields->ownNameLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "YOU",
            "bigFont.fnt",
            0.30f,
            {0.0f, 1.0f},
            {left, top - 4.0f},
            corum::ranked::ui::Cyan
        );
        limitHudWidth(m_fields->ownNameLabel, 96.0f, 0.30f, 0.21f);

        m_fields->ownScoreCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            "SCORE",
            "bigFont.fnt",
            0.30f,
            {0.0f, 1.0f},
            {left, top - 18.0f},
            corum::ranked::ui::Text
        );
        m_fields->ownScoreLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "0",
            "black",
            0.42f,
            {0.0f, 1.0f},
            {left + 64.0f, top - 17.0f},
            corum::ranked::ui::Text
        );
        m_fields->ownScoreLabel->setID("ranked-own-score"_spr);
        limitHudWidth(m_fields->ownScoreLabel, 58.0f, 0.42f, 0.26f);
        m_fields->ownChecks[0] = checkIcon({left + 8.0f, top - 36.0f});
        m_fields->ownChecks[1] = checkIcon({left + 25.0f, top - 36.0f});
        m_fields->ownChecks[2] = checkIcon({left + 42.0f, top - 36.0f});
        for (auto* icon : m_fields->ownChecks) m_fields->hudRoot->addChild(icon, 3);
        m_fields->ownAttemptLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.28f,
            {0.0f, 1.0f},
            {left, top - 33.0f},
            corum::ranked::ui::Gold
        );
        setHudVisible(m_fields->ownAttemptLabel, false);

        m_fields->roundLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.31f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 2.0f},
            corum::ranked::ui::Gold
        );
        m_fields->roundLabel->setID("ranked-current-round"_spr);

        // Keep the authoritative clock directly below the current round.
        m_fields->timerLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "black",
            0.60f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 15.0f},
            corum::ranked::ui::Text
        );
        m_fields->timerLabel->setID("ranked-countdown"_spr);
        m_fields->stateLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.26f,
            {0.5f, 1.0f},
            {size.width / 2.0f, top - 38.0f},
            corum::ranked::ui::Gold
        );
        m_fields->stateLabel->setID("ranked-round-state"_spr);

        m_fields->opponentNameLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "OPPONENT",
            "bigFont.fnt",
            0.30f,
            {1.0f, 1.0f},
            {right, top - 4.0f},
            corum::ranked::ui::Red
        );
        limitHudWidth(m_fields->opponentNameLabel, 96.0f, 0.30f, 0.21f);

        m_fields->opponentScoreCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            "SCORE",
            "bigFont.fnt",
            0.30f,
            {1.0f, 1.0f},
            {right - 70.0f, top - 18.0f},
            corum::ranked::ui::Text
        );
        m_fields->opponentScoreLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "0",
            "black",
            0.42f,
            {1.0f, 1.0f},
            {right, top - 17.0f},
            corum::ranked::ui::Text
        );
        m_fields->opponentScoreLabel->setID("ranked-opponent-score"_spr);
        limitHudWidth(m_fields->opponentScoreLabel, 58.0f, 0.42f, 0.26f);
        m_fields->opponentChecks[0] = checkIcon({right - 42.0f, top - 36.0f});
        m_fields->opponentChecks[1] = checkIcon({right - 25.0f, top - 36.0f});
        m_fields->opponentChecks[2] = checkIcon({right - 8.0f, top - 36.0f});
        for (auto* icon : m_fields->opponentChecks) m_fields->hudRoot->addChild(icon, 3);
        m_fields->opponentAttemptLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "",
            "bigFont.fnt",
            0.28f,
            {1.0f, 1.0f},
            {right, top - 33.0f},
            corum::ranked::ui::Gold
        );
        setHudVisible(m_fields->opponentAttemptLabel, false);

        m_fields->qualifyingCaption = addShadowedHudLabel(
            m_fields->hudRoot,
            "Qualifying",
            "bigFont.fnt",
            0.30f,
            {0.0f, 0.0f},
            {layout.bottomLeftX + 4.0f, layout.bottomY + 4.0f},
            corum::ranked::ui::Text
        );
        m_fields->qualifyingLabel = addShadowedHudLabel(
            m_fields->hudRoot,
            "-",
            "black",
            0.42f,
            {0.0f, 0.0f},
            {layout.bottomLeftX + 82.0f, layout.bottomY + 2.0f},
            corum::ranked::ui::Text
        );
        m_fields->qualifyingLabel->setID("ranked-qualifying"_spr);
        limitHudWidth(m_fields->qualifyingLabel, 76.0f, 0.42f, 0.28f);

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
            "LIVE SPECTATOR",
            "bigFont.fnt",
            0.23f,
            {0.5f, 0.5f},
            {0.0f, 15.0f},
            corum::ranked::ui::Gold,
            2
        );
        m_fields->spectatorNameLabel = addShadowedHudLabel(
            m_fields->spectatorPanel,
            "OPPONENT",
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
            "CURRENT : -",
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
            .roundNumber = match.roundNumber,
            .scoreA = match.scoreA,
            .scoreB = match.scoreB,
            .clearsA = match.clearsA,
            .clearsB = match.clearsB,
            .deathmatch = match.state == "DEATHMATCH_PLAYING",
            .deathmatchSequence = match.deathmatchSequence,
            .deathmatchAttemptsUsedA = attemptsUsedA,
            .deathmatchAttemptsUsedB = attemptsUsedB,
            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,
            .remainingMillis = runtime.deadlineMillis(),
            .signedRemainingMillis = runtime.signedDeadlineMillis(),
            .renderFps = m_fields->fpsMeter.fps(),
            .spectatorActive = match.spectatorActive,
            .spectatorOpponentName = match.spectatorOpponentName,
            .spectatorCurrentProgress = match.spectatorCurrentProgress,
        });

        setHudText(m_fields->fpsLabel, presentation.fpsText);
        auto const ownIsA = match.side == "A";
        if (m_fields->ownNameLabel) {
            setHudText(m_fields->ownNameLabel, ownIsA ? match.playerAName : match.playerBName);
            limitHudWidth(m_fields->ownNameLabel, 96.0f, 0.30f, 0.21f);
        }
        if (m_fields->opponentNameLabel) {
            setHudText(m_fields->opponentNameLabel, ownIsA ? match.playerBName : match.playerAName);
            limitHudWidth(m_fields->opponentNameLabel, 96.0f, 0.30f, 0.21f);
        }
        auto const liveProgress = m_fields->attemptEndReported
            ? -1.0
            : rankedProgressPercent(this);
        auto const ownLiveScore = runtime.localDisplayScore(
            liveProgress,
            m_fields->qualifyingPercent
        );
        setHudText(m_fields->ownScoreLabel, formatScore(ownLiveScore));
        limitHudWidth(m_fields->ownScoreLabel, 58.0f, 0.42f, 0.26f);
        auto const opponentScore = ownIsA ? match.scoreB : match.scoreA;
        setHudText(m_fields->opponentScoreLabel, formatScore(opponentScore));
        limitHudWidth(m_fields->opponentScoreLabel, 58.0f, 0.42f, 0.26f);
        auto ownChecks = presentation.ownChecks;
        if (!presentation.deathmatch) ownChecks = corum::ranked::clearChecks(runtime.localDisplayClears());
        for (std::size_t index = 0; index < m_fields->ownChecks.size(); ++index) {
            applyCheck(m_fields->ownChecks[index], ownChecks[index]);
            applyCheck(m_fields->opponentChecks[index], presentation.opponentChecks[index]);
            m_fields->ownChecks[index]->setVisible(!presentation.deathmatch);
            m_fields->opponentChecks[index]->setVisible(!presentation.deathmatch);
        }
        setHudVisible(m_fields->ownAttemptLabel, presentation.deathmatch);
        setHudVisible(m_fields->opponentAttemptLabel, presentation.deathmatch);
        if (presentation.deathmatch) {
            setHudText(m_fields->ownAttemptLabel, presentation.ownAttemptText);
            setHudText(m_fields->opponentAttemptLabel, presentation.opponentAttemptText);
        }
        auto const qualifying = match.currentMap ? match.currentMap->qualifyingPercent : 100.0;
        setHudText(m_fields->qualifyingLabel, formatScore(qualifying) + "%");
        limitHudWidth(m_fields->qualifyingLabel, 76.0f, 0.42f, 0.28f);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if (m_fields->debugLabel) setHudVisible(m_fields->debugLabel, match.debug);
#endif

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const top = corum::ranked::layoutHud(size.width, size.height).topY;
        setHudText(m_fields->roundLabel, presentation.roundText);
        setHudText(m_fields->timerLabel, presentation.timerText);
        setHudText(m_fields->stateLabel, presentation.stateText);
        setHudVisible(m_fields->roundLabel, !presentation.roundText.empty());
        setHudVisible(m_fields->timerLabel, !presentation.timerText.empty());
        setHudVisible(m_fields->stateLabel, !presentation.stateText.empty());
        setHudPosition(m_fields->roundLabel, {size.width / 2.0f, top - 2.0f});
        setHudPosition(m_fields->timerLabel, {size.width / 2.0f, top - 15.0f});
        setHudPosition(m_fields->stateLabel, {size.width / 2.0f, top - 38.0f});
        auto const urgent =
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "ROUND_SETTLING";
        m_fields->timerLabel->setColor(urgent ? corum::ranked::ui::Red : corum::ranked::ui::Text);
        m_fields->stateLabel->setColor(urgent ? corum::ranked::ui::Red : corum::ranked::ui::Gold);

        m_fields->spectatorPanel->setVisible(presentation.spectatorVisible);
        if (presentation.spectatorVisible) {
            setHudText(m_fields->spectatorNameLabel, presentation.spectatorOpponentText);
            setHudText(m_fields->spectatorProgressLabel, presentation.spectatorProgressText);
            setHudText(m_fields->spectatorTimerLabel, presentation.timerText);
        }
    }
};
