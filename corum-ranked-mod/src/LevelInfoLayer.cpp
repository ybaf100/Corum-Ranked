#include "RankedRuntime.hpp"
#include "RankedSongGate.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

using SteadyClock = std::chrono::steady_clock;

struct PendingPrepareGate {
    int levelId = 0;
    double countdownRemainingSeconds = 0.0;
    bool pending = false;
};

PendingPrepareGate g_pendingPrepareGate;

bool rankedPrepareState(std::string const& state) {
    return state == "ROUND_PREPARE" || state == "DEATHMATCH_PREPARE";
}

bool rankedPlayingState(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING";
}

bool rankedDownloadGateState(std::string const& state) {
    return rankedPrepareState(state) || state == "ROUND_PLAYING" || state == "DEATHMATCH_PLAYING";
}

std::string rankedGateTitle() {
    auto const& match = corum::ranked::RankedRuntime::get().view().match;
    if (match.deathmatchSequence > 0 || match.state == "DEATHMATCH_PREPARE" || match.state == "DEATHMATCH_PLAYING") {
        return "DEATH MATCH";
    }
    if (match.roundNumber <= 0) return "RANKED MATCH";
    if (match.roundNumber == 2) return "ROUND 2 - MATCH POINT";
    if (match.roundNumber == 3) return "ROUND 3 - TIEBREAKER";
    return fmt::format("ROUND {}", match.roundNumber);
}

bool mapReady(GJGameLevel* level) {
    return level && !level->m_levelString.empty() && !level->m_levelNotDownloaded;
}

std::vector<int> collectSongIds(GJGameLevel* level) {
    std::vector<int> result;
    if (!level) return result;
    auto add = [&result](int id) {
        if (id <= 0) return;
        if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    };
    add(static_cast<int>(level->m_songID));
    std::stringstream stream(std::string(level->m_songIDs.c_str()));
    std::string token;
    while (std::getline(stream, token, ',')) {
        try { add(std::stoi(token)); } catch (...) {}
    }
    return result;
}

bool songsReady(GJGameLevel* level) {
    if (!mapReady(level)) return false;
    auto ids = collectSongIds(level);
    if (ids.empty()) return true;
    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) return false;
    for (auto id : ids) {
        if (!manager->isResourceSong(id) && !manager->isSongDownloaded(id)) return false;
    }
    return true;
}

bool isCurrentRankedLevel(GJGameLevel* level) {
    auto& runtime = corum::ranked::RankedRuntime::get();
    return
        level &&
        runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
        runtime.currentLevelId() == static_cast<int>(level->m_levelID);
}

bool consumePrepareGateRequest(GJGameLevel* level, double& countdownRemainingSeconds) {
    if (!level || !g_pendingPrepareGate.pending) return false;
    if (g_pendingPrepareGate.levelId != static_cast<int>(level->m_levelID)) return false;
    countdownRemainingSeconds = std::max(0.0, g_pendingPrepareGate.countdownRemainingSeconds);
    g_pendingPrepareGate = {};
    return true;
}

void setMenuItemsEnabled(CCNode* root, bool enabled) {
    if (!root) return;
    if (auto* item = typeinfo_cast<CCMenuItem*>(root)) item->setEnabled(enabled);
    for (CCNode* child : root->getChildrenExt()) {
        if (child) setMenuItemsEnabled(child, enabled);
    }
}

void enableSongAcquisitionControls(CustomSongWidget* songWidget) {
    if (!songWidget) return;

    // CustomSongWidget is a CCNode with its own button menu. Disabling parent
    // CCMenu touch dispatch (the alpha.21 approach) can leave the visible song
    // download button completely inert. Keep menu dispatch alive and gate at the
    // individual menu-item level instead.
    if (songWidget->m_buttonMenu) songWidget->m_buttonMenu->setTouchEnabled(true);

    auto enable = [](CCMenuItemSpriteExtra* item) {
        if (!item) return;
        item->setEnabled(true);
    };
    enable(songWidget->m_downloadBtn);
    enable(songWidget->m_cancelDownloadBtn);
    enable(songWidget->m_getSongInfoBtn);
}

// During prepare, leave Cocos menu touch dispatch enabled and disable the actual
// menu items instead. Then explicitly restore only the vanilla song acquisition
// controls. This makes the on-screen GD download button clickable while blocking
// Play / Back / Like / Info / leaderboard / refresh and every other menu action.
void lockInteractionToSong(CCNode* root, CustomSongWidget* songWidget) {
    if (!root) return;
    setMenuItemsEnabled(root, false);
    enableSongAcquisitionControls(songWidget);
}

void unlockRankedPrepareInteraction(CCNode* root) {
    // The gate is torn down immediately before vanilla onPlay(), so restoring the
    // menu items here is safe and avoids carrying disabled controls into the
    // normal Geometry Dash scene stack.
    setMenuItemsEnabled(root, true);
}

CCLabelBMFont* gateLabel(std::string const& text, float scale, CCPoint position, ccColor3B color = {255, 255, 255}) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition(position);
    return label;
}

CCNode* gatePanel(CCSize size, CCPoint position, ccColor3B accent = {52, 214, 255}) {
    auto* node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint({0.5f, 0.5f});
    node->setPosition(position);

    auto* shadow = CCLayerColor::create({2, 6, 16, 88});
    shadow->setContentSize({size.width + 4.0f, size.height + 4.0f});
    shadow->setPosition({-2.0f, -4.0f});
    node->addChild(shadow, -1);

    auto* border = CCLayerColor::create({accent.r, accent.g, accent.b, 118});
    border->setContentSize(size);
    node->addChild(border, 0);

    auto* inner = CCLayerColor::create({7, 18, 40, 228});
    inner->setContentSize({std::max(2.0f, size.width - 4.0f), std::max(2.0f, size.height - 4.0f)});
    inner->setPosition({2.0f, 2.0f});
    node->addChild(inner, 1);

    auto* line = CCLayerColor::create({accent.r, accent.g, accent.b, 220});
    line->setContentSize({std::max(4.0f, size.width - 16.0f), 2.0f});
    line->setPosition({8.0f, size.height - 4.0f});
    node->addChild(line, 2);
    return node;
}

} // namespace

namespace corum::ranked {

bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds) {
    if (!level) return false;
    g_pendingPrepareGate.levelId = static_cast<int>(level->m_levelID);
    g_pendingPrepareGate.countdownRemainingSeconds = std::max(0.0, countdownRemainingSeconds);
    g_pendingPrepareGate.pending = true;

    auto* scene = LevelInfoLayer::scene(level, false);
    if (!scene) {
        g_pendingPrepareGate = {};
        return false;
    }
    CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.20f, scene));
    return true;
}

} // namespace corum::ranked

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        bool returningToRanked = false;
        bool autoPlayScheduled = false;
        bool songGateActive = false;
        bool songGateReadySent = false;
        bool songGateStartingPlay = false;
        bool songGateBypassed = false;
        bool mapDownloadRequested = false;
        bool mapFailureReported = false;
        SteadyClock::time_point gateOpenedAt {};
        SteadyClock::time_point countdownEndsAt {};
        SteadyClock::time_point mapDownloadStartedAt {};
        SteadyClock::time_point lastMapDownloadRequestAt {};
        SteadyClock::time_point songWaitStartedAt {};
        CCNode* gateOverlay = nullptr;
        CCLabelBMFont* gateTitle = nullptr;
        CCLabelBMFont* gateStatus = nullptr;
        CCLabelBMFont* gateCountdown = nullptr;
    };

    void onEnterTransitionDidFinish() override {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;

        double countdownRemaining = 0.0;
        if (
            rankedDownloadGateState(runtime.view().match.state) &&
            consumePrepareGateRequest(m_level, countdownRemaining)
        ) {
            setupSongDownloadGate(countdownRemaining);
            return;
        }

        if (rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) {
            scheduleAutoPlay(0.22f);
        } else {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.10f);
        }
    }

    void setupSongDownloadGate(double countdownRemaining) {
        if (m_fields->songGateActive) return;
        m_fields->songGateActive = true;
        m_fields->gateOpenedAt = SteadyClock::now();
        m_fields->countdownEndsAt = m_fields->gateOpenedAt + std::chrono::milliseconds(
            static_cast<long long>(std::ceil(std::max(0.0, countdownRemaining) * 1000.0))
        );

        auto const size = CCDirector::sharedDirector()->getWinSize();

        // Keep the real Geometry Dash level page visible and add a compact
        // competitive Ranked banner in the open space above the song widget.
        // This is intentionally native Cocos/Geode UI, not a screenshot overlay,
        // so the vanilla song button remains a real interactive control.
        m_fields->gateOverlay = gatePanel({174.0f, 52.0f}, {size.width / 2.0f, size.height / 2.0f - 30.0f});
        m_fields->gateOverlay->setZOrder(1001);
        addChild(m_fields->gateOverlay, 1001);

        m_fields->gateTitle = gateLabel(rankedGateTitle(), 0.16f, {87.0f, 40.0f}, {52, 214, 255});
        m_fields->gateTitle->limitLabelWidth(158.0f, 0.16f, 0.10f);
        m_fields->gateOverlay->addChild(m_fields->gateTitle, 3);
        m_fields->gateCountdown = gateLabel("STARTS IN  10", 0.27f, {87.0f, 25.0f}, {255, 216, 86});
        m_fields->gateOverlay->addChild(m_fields->gateCountdown, 3);
        m_fields->gateStatus = gateLabel("ONLY SONG DOWNLOAD AVAILABLE", 0.11f, {87.0f, 10.0f}, {215, 228, 245});
        m_fields->gateStatus->limitLabelWidth(158.0f, 0.11f, 0.09f);
        m_fields->gateOverlay->addChild(m_fields->gateStatus, 3);

        lockInteractionToSong(this, m_songWidget);
        requestVanillaMapDownload();

        schedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate), 0.20f);
        updateSongDownloadGate(0.0f);
    }

    void requestVanillaMapDownload() {
        if (!m_fields->songGateActive || mapReady(m_level) || m_fields->mapDownloadRequested) return;

        auto const now = SteadyClock::now();
        if (m_fields->mapDownloadStartedAt == SteadyClock::time_point{}) {
            m_fields->mapDownloadStartedAt = now;
        }
        m_fields->lastMapDownloadRequestAt = now;
        m_fields->mapDownloadRequested = true;

        // Important: do not call GameLevelManager::downloadLevel directly here.
        // Let the real on-screen LevelInfoLayer own its normal download delegate,
        // loading UI, saved-level replacement and song metadata initialization.
        LevelInfoLayer::downloadLevel();
    }

    void updateSongDownloadGate(float) {
        if (!m_fields->songGateActive) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();

        if (!isCurrentRankedLevel(m_level)) {
            returnToRanked(0.0f);
            return;
        }

        auto const& match = runtime.view().match;
        if (m_fields->gateTitle) m_fields->gateTitle->setString(rankedGateTitle().c_str());
        auto const now = SteadyClock::now();
        auto const levelReady = mapReady(m_level);

        // Base LevelInfoLayer may replace/rebuild its song widget when the level
        // payload arrives. Re-apply the interaction lock every tick so the newly
        // created vanilla song control is the only clickable UI.
        lockInteractionToSong(this, m_songWidget);

        if (!levelReady) {
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString("DOWNLOADING MAP...");
                m_fields->gateStatus->setColor({95, 180, 255});
            }

            if (m_fields->mapDownloadStartedAt == SteadyClock::time_point{}) {
                requestVanillaMapDownload();
            } else {
                auto const mapElapsed = std::chrono::duration<double>(now - m_fields->mapDownloadStartedAt).count();
                auto const sinceRequest = std::chrono::duration<double>(now - m_fields->lastMapDownloadRequestAt).count();
                if (m_fields->mapDownloadRequested && sinceRequest >= 5.0) {
                    // If vanilla never produced a completion/failure callback, let
                    // LevelInfoLayer retry its own download rather than deadlocking.
                    m_fields->mapDownloadRequested = false;
                }
                if (!m_fields->mapDownloadRequested && sinceRequest >= 1.0) requestVanillaMapDownload();
                if (mapElapsed >= 30.0 && !m_fields->mapFailureReported) {
                    m_fields->mapFailureReported = true;
                    runtime.reportMapDownloadFailure();
                }
            }
        } else {
            if (m_fields->songWaitStartedAt == SteadyClock::time_point{}) {
                m_fields->songWaitStartedAt = now;
            }

            auto const ready = songsReady(m_level);
            auto const songElapsed = std::chrono::duration<double>(now - m_fields->songWaitStartedAt).count();
            if (ready) {
                runtime.setSongBypassAllowed(false);
                m_fields->songGateBypassed = false;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG DOWNLOADED");
                    m_fields->gateStatus->setColor({78, 232, 112});
                }
            } else if (songElapsed >= 20.0) {
                // Song is optional after the existing 20-second grace period.
                // The vanilla song widget remains alive, so a background download
                // can continue while Ranked starts without audio.
                runtime.setSongBypassAllowed(true);
                m_fields->songGateBypassed = true;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG TIMEOUT - STARTING WITHOUT SONG");
                    m_fields->gateStatus->setColor({255, 216, 86});
                }
            } else if (m_fields->gateStatus) {
                m_fields->gateStatus->setString("ONLY SONG DOWNLOAD IS AVAILABLE");
                m_fields->gateStatus->setColor({95, 180, 255});
            }
        }

        auto const countdownRemaining = std::max(
            0,
            static_cast<int>(std::ceil(std::chrono::duration<double>(m_fields->countdownEndsAt - now).count()))
        );
        auto const readyForStart = levelReady && (songsReady(m_level) || runtime.songBypassAllowed());

        if (m_fields->gateCountdown) {
            if (countdownRemaining > 0) {
                m_fields->gateCountdown->setString(fmt::format("STARTS IN  {:02d}", countdownRemaining).c_str());
            } else if (!levelReady) {
                m_fields->gateCountdown->setString("WAITING FOR MAP");
            } else if (!readyForStart) {
                m_fields->gateCountdown->setString("WAITING FOR SONG");
            } else {
                m_fields->gateCountdown->setString("READY");
            }
        }

        if (
            rankedPrepareState(match.state) &&
            now >= m_fields->countdownEndsAt &&
            readyForStart &&
            !m_fields->songGateReadySent
        ) {
            m_fields->songGateReadySent = true;
            runtime.submitReady();
            return;
        }

        if (rankedPlayingState(match.state)) {
            if (!readyForStart || !runtime.canEnterCurrentLevel() || m_fields->songGateStartingPlay) return;
            m_fields->songGateStartingPlay = true;
            if (!songsReady(m_level) && runtime.songBypassAllowed()) {
                m_level->m_showedSongWarning = true;
            }
            teardownSongDownloadGate();
            if (!runtime.armCurrentLevelForGameplay()) {
                returnToRanked(0.0f);
                return;
            }
            // Start directly from the same real LevelInfoLayer that downloaded the
            // map. This keeps Geometry Dash's normal load/audio/return stack.
            LevelInfoLayer::onPlay(nullptr);
            return;
        }

        if (
            match.state == "CANCELLED" ||
            match.state == "MATCH_RESULT" ||
            match.state == "ROUND_RESULT" ||
            match.state == "DEATHMATCH_RESULT"
        ) {
            returnToRanked(0.0f);
        }
    }

    void teardownSongDownloadGate() {
        if (!m_fields->songGateActive) return;
        m_fields->songGateActive = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate));

        // Restore vanilla LevelInfo interaction before invoking its onPlay path.
        unlockRankedPrepareInteraction(this);

        if (m_fields->gateOverlay) {
            m_fields->gateOverlay->removeFromParentAndCleanup(true);
            m_fields->gateOverlay = nullptr;
        }
        m_fields->gateTitle = nullptr;
        m_fields->gateStatus = nullptr;
        m_fields->gateCountdown = nullptr;
    }

    void levelDownloadFinished(GJGameLevel* level) override {
        // Let vanilla LevelInfoLayer replace/update its level object and rebuild
        // all normal UI first. Ranked only re-locks controls afterwards.
        LevelInfoLayer::levelDownloadFinished(level);
        if (!m_fields->songGateActive) return;

        m_fields->mapDownloadRequested = false;
        if (mapReady(m_level)) {
            m_fields->songWaitStartedAt = SteadyClock::now();
        }
        lockInteractionToSong(this, m_songWidget);
        updateSongDownloadGate(0.0f);
    }

    void levelDownloadFailed(int response) override {
        LevelInfoLayer::levelDownloadFailed(response);
        if (!m_fields->songGateActive) return;
        m_fields->mapDownloadRequested = false;
        if (m_fields->gateStatus) {
            m_fields->gateStatus->setString(fmt::format("MAP DOWNLOAD FAILED ({}) - RETRYING", response).c_str());
            m_fields->gateStatus->setColor({255, 92, 92});
        }
        lockInteractionToSong(this, m_songWidget);
    }

    void scheduleAutoPlay(float delay) {
        if (m_fields->autoPlayScheduled || m_fields->returningToRanked || m_fields->songGateActive) return;
        m_fields->autoPlayScheduled = true;
        scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked), delay);
    }

    void autoPlayRanked(float) {
        m_fields->autoPlayScheduled = false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;
        if (!rankedPlayingState(runtime.view().match.state)) {
            returnToRanked(0.0f);
            return;
        }
        if (!runtime.canEnterCurrentLevel() || !mapReady(m_level)) {
            returnToRanked(0.0f);
            return;
        }

        auto const ready = songsReady(m_level);
        if (!ready && !runtime.songBypassAllowed()) {
            returnToRanked(0.0f);
            return;
        }

        if (!ready && runtime.songBypassAllowed()) m_level->m_showedSongWarning = true;
        if (!runtime.armCurrentLevelForGameplay()) {
            returnToRanked(0.0f);
            return;
        }
        LevelInfoLayer::onPlay(nullptr);
    }

    void onPlay(CCObject* sender) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (m_fields->songGateActive) {
            // During the countdown only the vanilla song download control is legal.
            return;
        }
        if (isCurrentRankedLevel(m_level) && rankedPlayingState(runtime.view().match.state)) {
            if (!mapReady(m_level)) return;
            if (!songsReady(m_level) && runtime.songBypassAllowed()) m_level->m_showedSongWarning = true;
            if (!runtime.armCurrentLevelForGameplay()) return;
            LevelInfoLayer::onPlay(sender);
            return;
        }
        LevelInfoLayer::onPlay(sender);
    }

    void returnToRanked(float) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (m_fields->songGateActive && rankedPrepareState(runtime.view().match.state)) return;
        if (isCurrentRankedLevel(m_level) && rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) return;
        if (m_fields->returningToRanked) return;
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void onBack(CCObject* sender) {
        if (m_fields->songGateActive) return;
        if (!isCurrentRankedLevel(m_level)) {
            LevelInfoLayer::onBack(sender);
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            if (runtime.canEnterCurrentLevel()) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }

    void keyBackClicked() override {
        if (m_fields->songGateActive) return;
        if (!isCurrentRankedLevel(m_level)) {
            LevelInfoLayer::keyBackClicked();
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            if (runtime.canEnterCurrentLevel()) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }
};
