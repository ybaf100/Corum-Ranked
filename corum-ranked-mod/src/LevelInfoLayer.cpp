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

void setMenusEnabled(CCNode* root, bool enabled) {
    if (!root) return;
    if (auto* menu = typeinfo_cast<CCMenu*>(root)) menu->setTouchEnabled(enabled);
    for (CCNode* child : root->getChildrenExt()) {
        if (child) setMenusEnabled(child, enabled);
    }
}

// Disable every vanilla LevelInfo menu while preparing, then selectively restore
// the actual Geometry Dash song widget. This leaves map download fully owned by
// LevelInfoLayer while preventing Play/Back/Like/etc. from being tapped early.
void lockInteractionToSong(CCNode* root, CCNode* songWidget) {
    if (!root) return;
    setMenusEnabled(root, false);
    if (!songWidget) return;

    // If a parent menu owns the widget, it must stay enabled for the child's
    // vanilla song download controls to receive touches.
    for (CCNode* node = songWidget; node && node != root; node = node->getParent()) {
        if (auto* menu = typeinfo_cast<CCMenu*>(node)) menu->setTouchEnabled(true);
    }
    setMenusEnabled(songWidget, true);
}

CCLabelBMFont* gateLabel(std::string const& text, float scale, CCPoint position, ccColor3B color = {255, 255, 255}) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition(position);
    return label;
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
        CCLabelBMFont* gateStatus = nullptr;
        CCLabelBMFont* gateCountdown = nullptr;
    };

    void onEnterTransitionDidFinish() override {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;

        double countdownRemaining = 0.0;
        if (rankedPrepareState(runtime.view().match.state) && consumePrepareGateRequest(m_level, countdownRemaining)) {
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

        // Keep the normal Geometry Dash level page visible. Only these small
        // Ranked labels are added in otherwise unused space; no full-screen mask
        // is used anymore.
        m_fields->gateStatus = gateLabel("DOWNLOADING MAP...", 0.22f, {size.width / 2.0f, 47.0f}, {95, 180, 255});
        m_fields->gateStatus->setZOrder(1002);
        addChild(m_fields->gateStatus, 1002);

        m_fields->gateCountdown = gateLabel("STARTS IN...", 0.34f, {size.width / 2.0f, 27.0f}, {255, 216, 86});
        m_fields->gateCountdown->setZOrder(1002);
        addChild(m_fields->gateCountdown, 1002);

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
                m_fields->gateCountdown->setString(fmt::format("STARTS IN... {}", countdownRemaining).c_str());
            } else if (!levelReady) {
                m_fields->gateCountdown->setString("WAITING FOR MAP...");
            } else if (!readyForStart) {
                m_fields->gateCountdown->setString("WAITING FOR SONG...");
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
        setMenusEnabled(this, true);

        if (m_fields->gateStatus) {
            m_fields->gateStatus->removeFromParentAndCleanup(true);
            m_fields->gateStatus = nullptr;
        }
        if (m_fields->gateCountdown) {
            m_fields->gateCountdown->removeFromParentAndCleanup(true);
            m_fields->gateCountdown = nullptr;
        }
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
