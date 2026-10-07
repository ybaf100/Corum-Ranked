#include "RankedRoundPreparation.hpp"
#include "RankedRuntime.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/modify/GameManager.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace corum::ranked {
namespace {
double clockSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool effectiveFileExists(gd::string const& path) {
    if (path.empty()) return false;
    std::error_code error;
    auto file = std::filesystem::path(path.c_str());
    return std::filesystem::is_regular_file(file, error) && !error &&
        std::filesystem::file_size(file, error) > 0 && !error;
}
bool roundModalOpen(CCNode* node) {
    if (!node || !node->isVisible()) return false;
    for (auto* child : node->getChildrenExt()) {
        if (!child || !child->isVisible()) continue;
        if (typeinfo_cast<FLAlertLayer*>(child) || roundModalOpen(child)) return true;
    }
    return false;
}
bool roundSceneContains(CCNode* root, CCNode* node) {
    if (!root || !node) return false;
    if (root == node) return true;
    for (auto* child : root->getChildrenExt()) if (roundSceneContains(child, node)) return true;
    return false;
}
}

RankedRoundPreparation& RankedRoundPreparation::get() {
    static auto* instance = [] {
        auto* preparation = new RankedRoundPreparation;
        preparation->init();
        CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(preparation, 1, false);
        preparation->release();
        return preparation;
    }();
    return *instance;
}
void RankedRoundPreparation::update(float dt) {
    m_timer -= dt;
    if (m_timer > 0) return;
    m_timer = 0.2f;
    pump();
}
bool RankedRoundPreparation::ready() const {
    return m_context == currentPreparationContext() && m_view.mapReady && m_view.audioReady && m_view.error.empty();
}
void RankedRoundPreparation::retry() {
    // Do not reissue a timed-out active request or rebind its untagged delegate.
    if (m_pendingMap || !m_tickets.empty() || m_urlPending) return;
    m_mapRetry = {};
    m_retries.clear();
    m_urlAttempts = 0;
    m_urlNextAt = 0;
    m_view.error.clear();
    m_contextBegan = clockSeconds();
    m_mapTimeoutReported = false;
}

void RankedRoundPreparation::pump() {
    auto const now = clockSeconds();
    // Both the process heartbeat and a visible round UI may ask to pump. Count
    // quiet native frames once, never twice in the same response/render tick.
    if (now < m_nextPumpAt) return;
    m_nextPumpAt = now + 0.2;
    auto const& runtime = RankedRuntime::get();
    auto context = runtime.view().stage == RuntimeStage::Matched &&
        roundAcquireState(runtime.view().match.state) ? currentPreparationContext() : LevelPreparationContext{};
    // The Debug RECORD/TEST harness keeps its independent native LevelInfo path.
    if (context.matchId != runtime.view().match.matchId) context = {};
    if (m_context != context) {
        m_context = context;
        m_contextBegan = now;
        m_nextLaunchAt = 0;
        m_view = {};
        m_mapRetry = {};
        m_mapTimeoutReported = false;
        m_retries.clear();
        m_songs.clear(); m_sfx.clear();
        if (!m_urlPending) { m_urlAttempts = 0; m_urlNextAt = 0; }
        if (m_pendingMap) m_discardedMap = true;
        for (auto& [key, ticket] : m_tickets) { (void)key; ticket.discarded = true; }
    }

    // Drain old generations even while this screen is covered or the match ended.
    pumpMap(now);
    for (auto it = m_tickets.begin(); it != m_tickets.end();) {
        auto const key = it->first;
        auto& ticket = it->second;
        auto const active = nativeActive(key);
        ticket.quiet = active ? 0 : ticket.quiet + 1;
        if (!active && ticket.quiet >= 3) {
            if (!ticket.discarded && ticket.context == m_context && !resourceReady(key)) {
                m_retries[key].failed(now);
                if (m_retries[key].attempts >= 3)
                    m_view.error = fmt::format("{} {} FAILED",
                        key.kind == RoundResourceKind::Sfx ? "SFX" : "SONG", key.id);
            }
            it = m_tickets.erase(it);
        } else {
            if (now - ticket.began >= 120 && !ticket.discarded && ticket.context == m_context)
                m_view.error = "AUDIO REQUEST STILL ACTIVE - WAITING FOR NATIVE COMPLETION";
            ++it;
        }
    }
    if (m_urlPending) {
        auto* music = MusicDownloadManager::sharedState();
        bool active = music->m_activeDownloads && music->m_activeDownloads->count() != 0;
        m_urlQuiet = active ? 0 : m_urlQuiet + 1;
        if (!music->m_customContentURL.empty() || (!active && m_urlQuiet >= 3)) {
            m_urlPending = false;
            m_urlNextAt = now + 2.0 * m_urlAttempts;
        } else if (now - m_urlBegan >= 30 && context.valid()) {
            m_view.error = "CONTENT URL REQUEST STILL ACTIVE - WAITING";
        }
    }
    if (!context.valid()) return;
    auto& metadata = RankedLevelPreparation::get();
    metadata.poll();
    metadata.adoptVerifiedSavedLevel(context);
    auto* level = metadata.level();
    m_view.mapReady = level && level->m_levelType == GJLevelType::Saved &&
        !level->m_levelString.empty() && !level->m_levelNotDownloaded;
    if (!m_view.mapReady) {
        if (!metadata.error().empty()) m_view.error = metadata.error();
        if (now - m_contextBegan >= 30 && !m_mapTimeoutReported) {
            m_mapTimeoutReported = true;
            RankedRuntime::get().reportMapDownloadFailure();
        }
        if (!level || m_pendingMap || !m_mapRetry.available(now)) return;
        auto* manager = GameLevelManager::sharedState();
        auto const key = std::string(manager->getLevelDownloadKey(context.levelId, false, 0));
        // Respect foreign native work; never steal its singleton callback slot.
        if (manager->m_levelDownloadDelegate || manager->isDLActive(key.c_str())) return;
        m_pendingMap = true;
        m_completedMap = m_discardedMap = m_downloadFailure = false;
        m_downloadContext = context;
        m_downloaded = nullptr;
        m_mapQuiet = 0; m_mapBegan = now; m_mapKey = key;
        m_mapRetry.started();
        manager->m_levelDownloadDelegate = this;
        tracePreparedLevel("round-download-request", level, nullptr, this);
        manager->downloadLevel(context.levelId, false, 0);
        return;
    }
    pumpAudio(level, now);
}

void RankedRoundPreparation::levelDownloadFinished(GJGameLevel* level) {
    if (!m_pendingMap || m_completedMap || GameLevelManager::sharedState()->m_levelDownloadDelegate != this) return;
    if (level && static_cast<int>(level->m_levelID) != m_downloadContext.levelId) return;
    m_downloaded = level;
    m_downloadFailure = !level;
    m_completedMap = true;
}
void RankedRoundPreparation::levelDownloadFailed(int response) {
    if (!m_pendingMap || m_completedMap || GameLevelManager::sharedState()->m_levelDownloadDelegate != this) return;
    // GD passes an error/response code, NOT the requested level ID.
    log::warn("[RankedPrepare] map failed level={} response={}", m_downloadContext.levelId, response);
    m_downloadFailure = m_completedMap = true;
}
void RankedRoundPreparation::pumpMap(double now) {
    if (!m_pendingMap) return;
    auto* manager = GameLevelManager::sharedState();
    bool const active = manager->isDLActive(m_mapKey.c_str());
    m_mapQuiet = active ? 0 : m_mapQuiet + 1;
    if (active || m_mapQuiet < 3) return;
    if (manager->m_levelDownloadDelegate == this) manager->m_levelDownloadDelegate = nullptr;
    m_pendingMap = false;
    if (!m_discardedMap && m_downloadContext == m_context) {
        auto& metadata = RankedLevelPreparation::get();
        tracePreparedLevel("round-download-callback", m_downloaded.data(), nullptr, this);
        if (!m_downloadFailure && m_completedMap && m_downloaded &&
            metadata.authorizes(m_downloaded.data(), m_context) &&
            metadata.adoptVerifiedSavedLevel(m_context)) {
            m_view.error.clear();
        } else {
            // A version/song/creator change requires fresh online attestation.
            // Never copy the downloaded string into the older list object.
            if (m_downloaded && !metadata.authorizes(m_downloaded.data(), m_context)) metadata.invalidate(m_context);
            m_mapRetry.failed(now);
            if (m_mapRetry.attempts >= 3)
                m_view.error = "MAP DOWNLOAD FAILED";
        }
    }
    m_downloaded = nullptr;
}

bool RankedRoundPreparation::nativeActive(RoundResourceKey key) const {
    auto* music = MusicDownloadManager::sharedState();
    if (key.kind == RoundResourceKind::SongInfo) return music->isDLActive(music->getSongInfoKey(key.id));
    if (key.kind == RoundResourceKind::Sfx) return music->isDLActive(music->getSFXDownloadKey(key.id));
    return music->isRunningActionForSongID(key.id) || music->isDLActive(music->getSongDownloadKey(key.id));
}
bool RankedRoundPreparation::resourceReady(RoundResourceKey key) const {
    auto* music = MusicDownloadManager::sharedState();
    if (key.kind == RoundResourceKind::SongInfo) {
        auto* info = music->getSongInfoObject(key.id);
        return info && info->m_songID == key.id && !info->m_unloaded && !info->m_songUrl.empty();
    }
    if (key.kind == RoundResourceKind::Sfx)
        return music->isResourceSFX(key.id) || effectiveFileExists(music->pathForSFX(key.id));
    // pathForSong preserves Jukebox/NONG overrides. A downloaded cache flag or
    // successful callback alone must not attest a missing/empty effective file.
    return music->isResourceSong(key.id) || effectiveFileExists(music->pathForSong(key.id));
}
void RankedRoundPreparation::complete(RoundResourceKey key) {
    if (auto found = m_tickets.find(key); found != m_tickets.end()) found->second.completed = true;
}
void RankedRoundPreparation::loadSongInfoFinished(SongInfoObject* info) {
    if (info) complete({RoundResourceKind::SongInfo, info->m_songID});
}
void RankedRoundPreparation::loadSongInfoFailed(int id, GJSongError) { complete({RoundResourceKind::SongInfo, id}); }
void RankedRoundPreparation::downloadSongFinished(int id) { complete({RoundResourceKind::Song, id}); }
void RankedRoundPreparation::downloadSongFailed(int id, GJSongError) { complete({RoundResourceKind::Song, id}); }
void RankedRoundPreparation::downloadSFXFinished(int id) { complete({RoundResourceKind::Sfx, id}); }
void RankedRoundPreparation::downloadSFXFailed(int id, GJSongError) { complete({RoundResourceKind::Sfx, id}); }
void RankedRoundPreparation::issue(RoundResourceKey key, double now) {
    if (m_tickets.contains(key) || nativeActive(key) || !m_retries[key].available(now)) return;
    m_tickets.emplace(key, ResourceTicket{m_context, now});
    m_retries[key].started(); // before a potentially synchronous native callback
    auto* music = MusicDownloadManager::sharedState();
    log::info("[RankedPrepare] audio request context={} kind={} id={} attempt={}",
        preparationContextText(m_context), static_cast<int>(key.kind), key.id, m_retries[key].attempts);
    if (key.kind == RoundResourceKind::SongInfo) music->getSongInfo(key.id, true);
    else if (key.kind == RoundResourceKind::Song) music->downloadSong(key.id);
    else music->downloadSFX(key.id);
}
void RankedRoundPreparation::pumpAudio(GJGameLevel* level, double now) {
    std::vector<int> songs, sfx;
    if (level->m_songID > 0) songs.push_back(level->m_songID);
    if (!appendRoundResourceIds(songs, std::string_view(level->m_songIDs.c_str())) ||
        !appendRoundResourceIds(sfx, std::string_view(level->m_sfxIDs.c_str()))) {
        m_view.audioReady = false;
        m_view.error = "INVALID NATIVE AUDIO DEPENDENCY IDS";
        return;
    }
    m_songs = std::move(songs); m_sfx = std::move(sfx);
    auto* music = MusicDownloadManager::sharedState();
    if (!m_musicWatching) {
        music->tryLoadLibraries();
        music->addMusicDownloadDelegate(this);
        m_musicWatching = true; // process lifetime, never unregister inside callback enumeration
    }
    m_view.songsTotal = static_cast<int>(m_songs.size());
    m_view.sfxTotal = static_cast<int>(m_sfx.size());
    m_view.songsDone = m_view.sfxDone = 0;
    double units = 0;
    bool needsURL = false;
    for (int id : m_songs) {
        bool done = resourceReady({RoundResourceKind::Song, id});
        m_view.songsDone += done;
        units += done ? 1.0 : std::clamp(music->getDownloadProgress(id), 0, 99) / 100.0;
        needsURL |= !done && id > 10000000;
    }
    for (int id : m_sfx) {
        bool done = resourceReady({RoundResourceKind::Sfx, id});
        m_view.sfxDone += done;
        units += done ? 1.0 : std::clamp(music->getSFXDownloadProgress(id), 0, 99) / 100.0;
        needsURL |= !done;
    }
    auto const total = m_view.songsTotal + m_view.sfxTotal;
    m_view.audioReady = m_view.songsDone + m_view.sfxDone == total;
    m_view.audioProgress = total ? static_cast<float>(units / total) : 1.0f;
    if (m_view.audioReady) { m_view.error.clear(); return; }
    if (needsURL && music->m_customContentURL.empty()) {
        if (!m_urlPending && now >= m_urlNextAt && m_urlAttempts < 3) {
            m_urlPending = true; m_urlQuiet = 0; m_urlBegan = now; ++m_urlAttempts;
            music->getCustomContentURL();
        }
        if (m_urlAttempts >= 3 && !m_urlPending) m_view.error = "CONTENT URL FAILED";
        return;
    }
    // Keep native concurrency bounded, including stale tickets and foreign work.
    unsigned occupied = static_cast<unsigned>(m_tickets.size());
    for (int id : m_songs) {
        for (auto kind : {RoundResourceKind::SongInfo, RoundResourceKind::Song}) {
            RoundResourceKey key{kind, id};
            if (!m_tickets.contains(key) && nativeActive(key)) ++occupied;
        }
    }
    for (int id : m_sfx) {
        RoundResourceKey key{RoundResourceKind::Sfx, id};
        if (!m_tickets.contains(key) && nativeActive(key)) ++occupied;
    }
    auto request = [&](RoundResourceKey key) {
        if (occupied >= 3 || m_tickets.contains(key) || nativeActive(key)) return;
        if (m_retries[key].attempts >= 3) {
            m_view.error = fmt::format("AUDIO {} FAILED", key.id);
            return;
        }
        if (!m_retries[key].available(now)) return;
        issue(key, now); ++occupied;
    };
    for (int id : m_songs) {
        if (resourceReady({RoundResourceKind::Song, id}) || nativeActive({RoundResourceKind::Song, id})) continue;
        if (id <= 10000000 && !resourceReady({RoundResourceKind::SongInfo, id}))
            request({RoundResourceKind::SongInfo, id});
        else request({RoundResourceKind::Song, id});
    }
    for (int id : m_sfx) if (!resourceReady({RoundResourceKind::Sfx, id})) request({RoundResourceKind::Sfx, id});
}

bool RankedRoundPreparation::launch(CCScene* origin) {
    pump();
    if (clockSeconds() < m_nextLaunchAt) return false;
    auto& runtime = RankedRuntime::get();
    auto& preparation = RankedLevelPreparation::get();
    auto* level = preparation.level();
    bool const spectator = runtime.canEnterCurrentLevelAsSpectator();
    bool const entering = runtime.canEnterNativeRoundScene();
    bool const allowed = runtime.canEnterCurrentLevel() || entering || spectator;
    if (roundPrepareState(runtime.view().match.state) || !allowed || !ready() ||
        !origin || origin != CCDirector::sharedDirector()->getRunningScene() ||
        roundModalOpen(origin) || !preparation.authorizes(level, m_context)) return false;
    // Recheck the live object/files at the last launch boundary, even when the
    // 200ms heartbeat has not run again since READY or a mod changed a song path.
    if (!level || level->m_levelType != GJLevelType::Saved || level->m_levelString.empty() || level->m_levelNotDownloaded)
        return false;
    std::vector<int> songs, sfx;
    if (level->m_songID > 0) songs.push_back(level->m_songID);
    if (!appendRoundResourceIds(songs, std::string_view(level->m_songIDs.c_str())) ||
        !appendRoundResourceIds(sfx, std::string_view(level->m_sfxIDs.c_str()))) return false;
    for (int id : songs) if (!resourceReady({RoundResourceKind::Song, id})) return false;
    for (int id : sfx) if (!resourceReady({RoundResourceKind::Sfx, id})) return false;
    if (auto current = m_play.lock(); current && current->isRunning()) return false;
    // Preserve exact server start eligibility/time and all existing PlayLayer
    // journal/ACK gates. A viewer never arms a local attempt.
    if (!spectator && !entering && !runtime.armCurrentLevelForGameplay()) return false;
    tracePreparedLevel("round-native-launch", level, nullptr, this);
    auto* scene = PlayLayer::scene(level, false, false);
    auto* play = PlayLayer::get();
    if (!scene || !play || play->m_level != level || !roundSceneContains(scene, play) ||
        m_context != currentPreparationContext()) {
        m_view.error = "NATIVE GAMEPLAY LOAD FAILED - RETRYING";
        m_nextLaunchAt = clockSeconds() + 1.0;
        return false;
    }
    ++m_launchGeneration;
    m_play = play;
    m_origin = origin;
    m_launchedLevel = level;
    m_returnQueued = m_quitting = false;
    CCDirector::sharedDirector()->pushScene(scene);
    // pushScene schedules the transition. Actual entry is ACKed by the running
    // PlayLayer heartbeat, after onEnter rather than before the scene exists.
    return true;
}
bool RankedRoundPreparation::beginNativeQuit(PlayLayer* play) {
    auto owned = m_play.lock();
    if (!owned || owned.data() != play) return false;
    m_quitting = true;
    return true;
}
void RankedRoundPreparation::endNativeQuit() {
    m_quitting = false;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    // Our scoped return bypasses the native LevelInfo navigation. Restore the
    // desktop cursor for the round UI, as the direct-scene Versus path does.
    PlatformToolbox::toggleLockCursor(false);
    PlatformToolbox::showCursor();
#endif
}
bool RankedRoundPreparation::returnFromNativeQuit(GJGameLevel* level) {
    auto origin = m_origin.lock();
    if (!m_quitting || !origin || !m_launchedLevel || level != m_launchedLevel.data()) return false;
    if (m_returnQueued) return true;
    m_returnQueued = true;
    auto const generation = m_launchGeneration;
    // Never construct the next PlayLayer inside the departing onQuit call stack.
    queueInMainThread([this, generation] {
        auto origin = m_origin.lock();
        if (generation != m_launchGeneration || !m_returnQueued || !origin) return;
        m_play = nullptr; m_origin = nullptr; m_launchedLevel = nullptr;
        m_returnQueued = false;
        CCDirector::sharedDirector()->popScene();
    });
    return true;
}

} // namespace corum::ranked

class $modify(CorumRankedRoundReturn, GameManager) {
    void returnToLastScene(GJGameLevel* level) {
        if (corum::ranked::RankedRoundPreparation::get().returnFromNativeQuit(level)) return;
        GameManager::returnToLastScene(level);
    }
};
