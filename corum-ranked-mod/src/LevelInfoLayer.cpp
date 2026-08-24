#include "RankedRuntime.hpp"
#include "RankedSongGate.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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
    if (!level || level->m_levelString.empty() || level->m_levelNotDownloaded) return false;
    auto const expectedLevelId = corum::ranked::RankedRuntime::get().currentLevelId();
    return expectedLevelId > 0 && expectedLevelId == static_cast<int>(level->m_levelID);
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

bool songFileExists(MusicDownloadManager* manager, int id) {
#if defined(GEODE_IS_WINDOWS)
    if (!manager || id <= 0) return false;
    auto const path = manager->pathForSong(id);
    if (path.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(path.c_str()), error) && !error;
#else
    (void)manager;
    (void)id;
    return false;
#endif
}

bool songIdReady(MusicDownloadManager* manager, int id) {
    if (!manager || id <= 0) return false;
    return
        manager->isResourceSong(id) ||
        manager->isSongDownloaded(id) ||
        songFileExists(manager, id);
}

bool songsReady(GJGameLevel* level, CustomSongWidget* songWidget) {
    if (!mapReady(level)) return false;

    // Trust the real vanilla widget first. In particular, built-in RobTop songs
    // (the Windows Stereo Madness case) do not require a MusicDownloadManager
    // custom-song download even when raw song ID fields are populated.
    if (songWidget && songWidget->m_isRobtopSong) return true;

    auto* manager = MusicDownloadManager::sharedState();
    if (songWidget && songWidget->m_customSongID > 0) {
        return songIdReady(manager, songWidget->m_customSongID);
    }

    auto ids = collectSongIds(level);
    if (ids.empty()) return true;
    if (!manager) return false;
    for (auto id : ids) {
        if (!songIdReady(manager, id)) return false;
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

void normalizeRankedLevelInfo(GJGameLevel* level) {
    if (!isCurrentRankedLevel(level)) return;
    auto const& map = corum::ranked::RankedRuntime::get().view().match.currentMap;

    // Only the synthetic GJGameLevel::create() shell needs normalization.
    // GameLevelManager::getSavedLevel() returns a canonical Saved payload;
    // changing that shared object back to SearchResult makes vanilla
    // shouldDownloadLevel() treat an already downloaded map as download/update
    // work on some platforms.
    if (level->m_levelType == GJLevelType::Default) {
        level->m_levelType = GJLevelType::SearchResult;
    }
    if (map) {
        if (!map->title.empty()) level->m_levelName = map->title;
        if (!map->creator.empty()) level->m_creatorName = map->creator;
    }
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
    // Restore the LevelInfo controls when its prepare gate is torn down. A
    // successful vanilla Play transition reaches onExit before this runs.
    setMenuItemsEnabled(root, true);
}

bool nodeContains(CCNode* root, CCNode* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    for (auto* child : root->getChildrenExt()) {
        if (nodeContains(child, target)) return true;
    }
    return false;
}

CCMenuItem* vanillaPlayButton(CCMenu* menu, CCNode* playSprite) {
    if (!menu || !playSprite) return nullptr;

    auto verifiedPlayItem = [playSprite](CCNode* node) -> CCMenuItem* {
        auto* item = typeinfo_cast<CCMenuItemSprite*>(node);
        if (!item || !nodeContains(item->getNormalImage(), playSprite)) return nullptr;
        return item;
    };

    // Node IDs are the most explicit route when geode.node-ids (or another
    // compatible ID provider) is present, but an ID alone is not sufficient:
    // while LevelInfo is rebuilding, the central item can temporarily represent
    // download/update work. Require the live native m_playSprite identity too.
    if (auto* identified = verifiedPlayItem(menu->getChildByID("play-button"))) {
        return identified;
    }

    for (auto* child : menu->getChildrenExt()) {
        if (auto* item = verifiedPlayItem(child)) return item;
    }
    return nullptr;
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
        // Automatic start and song-gate completion share one scene-transition
        // latch. Physical Play/Space input remains blocked for Ranked.
        bool gameplayLaunchStarted = false;
        bool vanillaPlayActivation = false;
        int gameplayLaunchAttempts = 0;
        int gameplayLaunchBusyChecks = 0;
        SteadyClock::time_point nextGameplayLaunchAt {};
        bool songGateActive = false;
        SteadyClock::time_point lastReadySubmitAt {};
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

    bool init(GJGameLevel* level, bool challenge) {
        normalizeRankedLevelInfo(level);
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (isCurrentRankedLevel(m_level)) {
            normalizeRankedLevelInfo(m_level);
            // Preserve vanilla's canonical Saved/SearchResult distinction.
            m_levelType = m_level->m_levelType;
        }
        return true;
    }

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
        corum::ranked::RankedRuntime::get().setSongBypassAllowed(false);
        m_fields->songGateBypassed = false;
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

    bool vanillaLevelReadyForPlay() {
        // m_levelString alone is not a sufficient launch boundary. Vanilla owns
        // the download/update decision that LevelInfoLayer::onPlay will use.
        return mapReady(m_level) && !LevelInfoLayer::shouldDownloadLevel();
    }

    void requestVanillaMapDownload() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& state = runtime.view().match.state;
        if (
            m_fields->returningToRanked ||
            !isCurrentRankedLevel(m_level) ||
            (!rankedDownloadGateState(state) && !rankedPlayingState(state)) ||
            m_fields->mapDownloadRequested ||
            vanillaLevelReadyForPlay()
        ) return;

        auto const now = SteadyClock::now();
        if (m_fields->mapDownloadStartedAt == SteadyClock::time_point{}) {
            m_fields->mapDownloadStartedAt = now;
        }
        m_fields->lastMapDownloadRequestAt = now;
        m_fields->mapDownloadRequested = true;

        log::info(
            "Ranked map requires vanilla download/update: level={} type={} hasPayload={} notDownloaded={}",
            m_level ? static_cast<int>(m_level->m_levelID) : 0,
            m_level ? static_cast<int>(m_level->m_levelType) : -1,
            m_level && !m_level->m_levelString.empty(),
            m_level ? m_level->m_levelNotDownloaded : true
        );

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
        auto const hasLevelPayload = mapReady(m_level);
        auto const levelReady = vanillaLevelReadyForPlay();

        // Base LevelInfoLayer may replace/rebuild its song widget when the level
        // payload arrives. Re-apply the interaction lock every tick so the newly
        // created vanilla song control is the only clickable UI.
        lockInteractionToSong(this, m_songWidget);

        if (!levelReady) {
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString(
                    hasLevelPayload ? "UPDATING MAP..." : "DOWNLOADING MAP..."
                );
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

            auto const ready = songsReady(m_level, m_songWidget);
            if (ready) {
                runtime.setSongBypassAllowed(false);
                m_fields->songGateBypassed = false;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG DOWNLOADED");
                    m_fields->gateStatus->setColor({78, 232, 112});
                }
            } else if (now >= m_fields->countdownEndsAt) {
                // The visible Ranked countdown is the resource wait budget for
                // optional music. A broken/slow custom-song API must never keep a
                // fully downloaded playable map stuck after STARTS IN 00.
                // The vanilla widget remains alive, so audio may still finish
                // downloading while gameplay proceeds without it.
                runtime.setSongBypassAllowed(true);
                m_fields->songGateBypassed = true;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG NOT READY - STARTING WITHOUT SONG");
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
        auto const readyForStart = levelReady && (songsReady(m_level, m_songWidget) || runtime.songBypassAllowed());

        auto const ownReady = match.side == "A" ? match.readyA : match.readyB;
        auto const opponentReady = match.side == "A" ? match.readyB : match.readyA;

        if (m_fields->gateCountdown) {
            if (countdownRemaining > 0) {
                m_fields->gateCountdown->setString(fmt::format("STARTS IN  {:02d}", countdownRemaining).c_str());
            } else if (!levelReady) {
                m_fields->gateCountdown->setString("WAITING FOR MAP");
            } else if (!readyForStart) {
                m_fields->gateCountdown->setString("WAITING FOR SONG");
            } else if (rankedPlayingState(match.state)) {
                m_fields->gateCountdown->setString("STARTING...");
            } else if (rankedPrepareState(match.state) && ownReady && !opponentReady) {
                m_fields->gateCountdown->setString("WAITING FOR OPPONENT");
            } else if (rankedPrepareState(match.state) && ownReady && opponentReady) {
                m_fields->gateCountdown->setString("STARTING...");
            } else if (rankedPrepareState(match.state) && !runtime.view().error.empty()) {
                // Do not hide an authoritative Ready endpoint failure behind an
                // endless SENDING READY label. The bounded retry below remains
                // active, while the player can see that the server rejected or
                // failed the previous request.
                m_fields->gateCountdown->setString("SERVER ERROR - RETRYING");
            } else {
                m_fields->gateCountdown->setString("SENDING READY...");
            }
        }

        if (
            rankedPrepareState(match.state) &&
            now >= m_fields->countdownEndsAt &&
            readyForStart &&
            !ownReady
        ) {
            // Ready is server-idempotent. Do not make this a one-shot client event:
            // submitReady() can legitimately be skipped while another control
            // request is busy, and a transient HTTP failure must not strand the
            // LevelInfo gate at READY forever. Retry at a bounded cadence until
            // authority actually publishes PLAYING.
            auto const readyRetryDue =
                m_fields->lastReadySubmitAt == SteadyClock::time_point{} ||
                std::chrono::duration<double>(now - m_fields->lastReadySubmitAt).count() >= 1.0;
            if (readyRetryDue) {
                m_fields->lastReadySubmitAt = now;
                runtime.submitReady();
            }
            return;
        }

        if (rankedPlayingState(match.state)) {
            if (
                !readyForStart ||
                !runtime.canEnterCurrentLevel() ||
                now < m_fields->nextGameplayLaunchAt ||
                m_fields->songGateStartingPlay ||
                m_fields->gameplayLaunchStarted
            ) return;

            // Keep the gate and its polling callback alive until gameplay has
            // actually been armed. READY -> PLAYING can land between client/GD
            // frames; tearing the gate down before armCurrentLevelForGameplay()
            // succeeds removes the only retry loop and can strand this page at
            // READY forever.
            m_fields->songGateStartingPlay = true;
            if (!launchRankedGameplay()) {
                m_fields->songGateStartingPlay = false;
                return;
            }
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
        if (!m_fields->songGateActive) {
            normalizeRankedLevelInfo(level);
            LevelInfoLayer::levelDownloadFinished(level);
            if (isCurrentRankedLevel(m_level)) {
                normalizeRankedLevelInfo(m_level);
                m_levelType = m_level->m_levelType;
            }
            m_fields->mapDownloadRequested = false;
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (
                vanillaLevelReadyForPlay() &&
                rankedPlayingState(runtime.view().match.state) &&
                runtime.canEnterCurrentLevel()
            ) scheduleAutoPlay(0.10f);
            return;
        }

        // Reject a callback for anything except the exact server-selected playable
        // (alternate) Level ID before vanilla is allowed to replace m_level. This
        // keeps the original expected-ID shell intact for a retry instead of
        // accidentally switching the Ranked page to an unrelated cached payload.
        auto const expectedLevelId = corum::ranked::RankedRuntime::get().currentLevelId();
        auto const validPayload =
            level &&
            expectedLevelId > 0 &&
            static_cast<int>(level->m_levelID) == expectedLevelId &&
            !level->m_levelString.empty() &&
            !level->m_levelNotDownloaded;
        if (!validPayload) {
            m_fields->mapDownloadRequested = false;
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString("MAP PAYLOAD MISMATCH - RETRYING");
                m_fields->gateStatus->setColor({255, 92, 92});
            }
            requestVanillaMapDownload();
            return;
        }

        // Only a verified payload may become the LevelInfoLayer's live level.
        // Vanilla then rebuilds its song metadata/widget from that exact payload.
        normalizeRankedLevelInfo(level);
        LevelInfoLayer::levelDownloadFinished(level);
        normalizeRankedLevelInfo(m_level);
        if (m_level) m_levelType = m_level->m_levelType;
        m_fields->mapDownloadRequested = false;
        if (!isCurrentRankedLevel(m_level) || !vanillaLevelReadyForPlay()) {
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString("MAP UPDATE INCOMPLETE - RETRYING");
                m_fields->gateStatus->setColor({255, 92, 92});
            }
            requestVanillaMapDownload();
            return;
        }
        m_fields->songWaitStartedAt = SteadyClock::now();
        lockInteractionToSong(this, m_songWidget);
        updateSongDownloadGate(0.0f);
    }

    void levelDownloadFailed(int response) override {
        LevelInfoLayer::levelDownloadFailed(response);
        m_fields->mapDownloadRequested = false;
        if (!m_fields->songGateActive) return;
        if (m_fields->gateStatus) {
            m_fields->gateStatus->setString(fmt::format("MAP DOWNLOAD FAILED ({}) - RETRYING", response).c_str());
            m_fields->gateStatus->setColor({255, 92, 92});
        }
        lockInteractionToSong(this, m_songWidget);
    }

    bool launchRankedGameplay() {
        if (m_fields->gameplayLaunchStarted || m_fields->returningToRanked) return false;

        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            !isCurrentRankedLevel(m_level) ||
            !rankedPlayingState(runtime.view().match.state) ||
            !runtime.canEnterCurrentLevel()
        ) {
            return false;
        }
        if (!vanillaLevelReadyForPlay()) {
            requestVanillaMapDownload();
            return false;
        }

        // Arm only when the real, visible Geometry Dash Play item exists and the
        // vanilla level page is no longer processing another action. This keeps
        // retries harmless: no synthetic scene and no attempt timestamp are
        // created while LevelInfo is still rebuilding after a map download.
        auto* nativePlayButton = vanillaPlayButton(m_playBtnMenu, m_playSprite);
        if (!nativePlayButton || m_isBusy) return false;

        auto const ready = songsReady(m_level, m_songWidget);
        if (!ready && !runtime.songBypassAllowed()) return false;
        if (!ready && runtime.songBypassAllowed()) m_level->m_showedSongWarning = true;

        // Snapshot the authoritative start before handing control to the real GD
        // Play button. The subsequent vanilla transition may cross the deadline,
        // but this visual attempt began while the server still allowed starts.
        if (!runtime.armCurrentLevelForGameplay()) return false;

        m_fields->gameplayLaunchStarted = true;
        ++m_fields->gameplayLaunchAttempts;
        m_fields->gameplayLaunchBusyChecks = 0;
        m_fields->autoPlayScheduled = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked));
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch),
            4.0f
        );

        // Automatic entry must behave like a real press, not construct or
        // replace a PlayLayer scene behind Geometry Dash's state machine.
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton),
            0.0f
        );
        return true;
    }

    void activateRankedPlayButton(float) {
        if (!m_fields->gameplayLaunchStarted || m_fields->returningToRanked) return;
        auto* nativePlayButton = vanillaPlayButton(m_playBtnMenu, m_playSprite);
        if (!nativePlayButton || m_isBusy) return;

        // Preserve the callback Geometry Dash installed on the real Play item.
        // Replacing it with this hook's selector re-entered the Geode hook chain
        // twice on iOS, which was visible as two forwarded onPlay lines for one
        // activation. The original callback still traverses this hook normally.
        if (!nativePlayButton->m_pListener || !nativePlayButton->m_pfnSelector) {
            log::warn("Verified Ranked Play item has no native callback");
            return;
        }
        m_fields->vanillaPlayActivation = true;
        log::info(
            "Activating verified Ranked Play button: match={} level={} attempt={} node={}",
            corum::ranked::RankedRuntime::get().view().match.matchId,
            static_cast<int>(m_level->m_levelID),
            m_fields->gameplayLaunchAttempts,
            nativePlayButton->getID()
        );
        // activate() can synchronously begin a scene transition and run
        // onExit. Keep this layer alive until the activation callback returns so
        // clearing the one-shot bypass below never touches a released object.
        retain();
        nativePlayButton->setEnabled(true);
        nativePlayButton->activate();
        // If Geometry Dash stayed on this page, restore only the Play lock. Do
        // not recursively mutate every LevelInfo control while vanilla's
        // asynchronous play/update pipeline may still be running.
        if (m_fields->gameplayLaunchStarted) {
            nativePlayButton->setEnabled(false);
        }
        m_fields->vanillaPlayActivation = false;
        release();
    }

    void verifyGameplayLaunch(float) {
        if (!m_fields->gameplayLaunchStarted) return;

        // Vanilla may still be finishing its update/play steps. Re-pressing the
        // item while that work owns the page restarts the map spinner forever.
        if (m_isBusy && ++m_fields->gameplayLaunchBusyChecks <= 10) {
            log::debug(
                "Ranked vanilla Play is still processing (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
            scheduleOnce(
                schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch),
                1.0f
            );
            return;
        }

        if (m_isBusy) {
            log::warn(
                "Ranked vanilla Play/update remained busy; releasing launch latch (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            m_fields->mapDownloadRequested = false;
            m_fields->nextGameplayLaunchAt = SteadyClock::now() + std::chrono::seconds(1);
            if (m_fields->gateCountdown) {
                m_fields->gateCountdown->setString("MAP UPDATE STALLED - RETRYING");
                m_fields->gateCountdown->setColor({255, 92, 92});
            }
            if (!m_fields->songGateActive) scheduleAutoPlay(1.0f);
            return;
        }

        auto const updateRequired = !vanillaLevelReadyForPlay();

        if (updateRequired) {
            log::warn(
                "Ranked Play resolved to map download/update (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
        } else {
            log::warn(
                "Verified Ranked Play button did not leave LevelInfo (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
        }
        m_fields->gameplayLaunchStarted = false;
        m_fields->vanillaPlayActivation = false;
        m_fields->songGateStartingPlay = false;
        m_fields->gameplayLaunchBusyChecks = 0;

        m_fields->nextGameplayLaunchAt = SteadyClock::now() + std::chrono::milliseconds(450);
        if (m_fields->gateCountdown) {
            m_fields->gateCountdown->setString(
                updateRequired ? "UPDATING MAP..." : "PLAY FAILED - RETRYING"
            );
            m_fields->gateCountdown->setColor(
                updateRequired ? ccColor3B{95, 180, 255} : ccColor3B{255, 92, 92}
            );
        }

        if (updateRequired) {
            m_fields->mapDownloadRequested = false;
            requestVanillaMapDownload();
        }

        if (!m_fields->songGateActive) {
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (
                isCurrentRankedLevel(m_level) &&
                rankedPlayingState(runtime.view().match.state) &&
                runtime.canEnterCurrentLevel()
            ) {
                scheduleAutoPlay(0.45f);
            } else {
                returnToRanked(0.0f);
            }
        }
    }

    void onExit() override {
        // onExit is the acknowledgement that Geometry Dash's own Play button
        // accepted the transition.
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
        auto const acceptedRankedLaunch = m_fields->gameplayLaunchStarted;
        if (acceptedRankedLaunch && m_fields->songGateActive) teardownSongDownloadGate();
        if (acceptedRankedLaunch) {
            // PlayLayer::onQuit returns to this same vanilla LevelInfo instance.
            // Clear only the UI launch latch here so onEnterTransitionDidFinish
            // can automatically activate the real Play button for the next legal
            // attempt. RankedRuntime remains the authority for timeout, result,
            // spectator, three-clear, and Deathmatch-attempt limits.
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            m_fields->autoPlayScheduled = false;
            m_fields->gameplayLaunchAttempts = 0;
            m_fields->nextGameplayLaunchAt = {};
        }
        LevelInfoLayer::onExit();
    }

    void scheduleAutoPlay(float delay) {
        if (
            m_fields->gameplayLaunchStarted ||
            m_fields->autoPlayScheduled ||
            m_fields->returningToRanked ||
            m_fields->songGateActive
        ) return;
        m_fields->autoPlayScheduled = true;
        scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked), delay);
    }

    void autoPlayRanked(float) {
        m_fields->autoPlayScheduled = false;
        if (m_fields->gameplayLaunchStarted) return;
        if (launchRankedGameplay()) return;

        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            isCurrentRankedLevel(m_level) &&
            rankedPlayingState(runtime.view().match.state) &&
            runtime.canEnterCurrentLevel()
        ) {
            // The real button can be transiently absent/busy while vanilla
            // rebuilds LevelInfo. Stay automatic and retry instead of exposing a
            // manual Play fallback or abandoning the Ranked page.
            scheduleAutoPlay(0.25f);
            return;
        }
        returnToRanked(0.0f);
    }

    void onPlay(CCObject* sender) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (m_fields->vanillaPlayActivation) {
            log::debug(
                "Ranked LevelInfo onPlay forwarded to vanilla: match={} level={}",
                runtime.view().match.matchId,
                m_level ? static_cast<int>(m_level->m_levelID) : 0
            );
            LevelInfoLayer::onPlay(sender);
            return;
        }
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) {
            // Only song acquisition is legal during the gate, and repeated
            // Play/Space becomes inert once the Ranked scene launch has started.
            return;
        }
        if (isCurrentRankedLevel(m_level) && rankedPlayingState(runtime.view().match.state)) {
            // Ranked owns automatic activation. A physical tap cannot create a
            // second attempt or bypass the authoritative entry checks.
            return;
        }
        LevelInfoLayer::onPlay(sender);
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            isCurrentRankedLevel(m_level) &&
            (
                m_fields->songGateActive ||
                m_fields->gameplayLaunchStarted ||
                rankedPrepareState(runtime.view().match.state) ||
                rankedPlayingState(runtime.view().match.state)
            )
        ) {
            // LevelInfoLayer maps keyboard shortcuts such as Space directly to
            // Play. During Ranked prepare/launch that bypasses the disabled menu
            // item and can start a second, re-entrant scene transition while the
            // keyboard dispatcher is still iterating its delegates. iOS then
            // dereferences the released delegate (the observed SIGBUS). Ranked
            // already owns automatic Play activation, so keyboard shortcuts on
            // this transient page must be inert.
            return;
        }
        LevelInfoLayer::keyDown(key, timestamp);
    }

    void returnToRanked(float) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const currentLevel = isCurrentRankedLevel(m_level);

        // Authority may replace ROUND_PLAYING with a Death Match (or another
        // Round/map) while the old LevelInfo selector is still waiting for a
        // transition. Do not let the old launch latch hold that obsolete scene
        // for the full watchdog interval; it otherwise stacks a second
        // LevelInfo page and starts two contexts against one visible screen.
        if (!currentLevel) {
            log::warn(
                "Discarding obsolete Ranked LevelInfo launch: shownLevel={} expectedLevel={} state={}",
                m_level ? static_cast<int>(m_level->m_levelID) : 0,
                runtime.currentLevelId(),
                runtime.view().match.state
            );
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton));
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            if (m_fields->songGateActive) teardownSongDownloadGate();
        } else if (m_fields->gameplayLaunchStarted) {
            return;
        }
        if (m_fields->songGateActive && rankedPrepareState(runtime.view().match.state)) return;
        if (currentLevel && rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) return;
        if (m_fields->returningToRanked) return;
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void onBack(CCObject* sender) {
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) return;
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
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) return;
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
