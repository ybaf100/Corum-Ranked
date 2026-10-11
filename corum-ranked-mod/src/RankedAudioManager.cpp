#include "RankedAudioManager.hpp"
#include "RankedVolumeControl.hpp"
#include "domain/TransitionTrace.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <fstream>
#include <array>
#include <sstream>
#include <system_error>

using namespace geode::prelude;

namespace corum::ranked {
namespace {
constexpr auto kSongInfoTimeout = std::chrono::seconds(20);
constexpr auto kSongFileTimeout = std::chrono::seconds(90);
constexpr auto kDownloadGuardTimeout = std::chrono::seconds(105);
constexpr int kMusicLibraryFirstSongId = 10000000;
constexpr auto kLibraryDownloadQuietTimeout = std::chrono::seconds(2);
constexpr auto kPendingAudioStartTimeout = std::chrono::seconds(3);
constexpr std::uintmax_t kMinimumCachedSongBytes = 1024;
constexpr auto kPlaybackVerifyDelay = std::chrono::milliseconds(1000);
constexpr int kMaxPlaybackRetries = 2;
constexpr float kRankedRelativeVolume = 0.80f;
constexpr char kSongInfoEndpoint[] = "https://www.boomlings.com/database/getGJSongInfo.php";
constexpr char kGdSecret[] = "Wmfd2893gb7";

int clampProgress(int value) {
    return std::clamp(value, 0, 100);
}

bool nativeSongDownloadActive(MusicDownloadManager* manager, int songId) {
    return manager && (manager->isRunningActionForSongID(songId) ||
        manager->isDLActive(manager->getSongDownloadKey(songId)));
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

std::string urlDecode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            auto const hi = hexValue(value[i + 1]);
            auto const lo = hexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return result;
}

std::optional<std::string> songDownloadUrl(std::string const& response) {
    if (response.empty() || response == "-1") return std::nullopt;

    constexpr std::string_view delimiter = "~|~";
    std::size_t cursor = 0;
    std::vector<std::string_view> fields;
    while (cursor <= response.size()) {
        auto const next = response.find(delimiter, cursor);
        if (next == std::string::npos) {
            fields.emplace_back(response.data() + cursor, response.size() - cursor);
            break;
        }
        fields.emplace_back(response.data() + cursor, next - cursor);
        cursor = next + delimiter.size();
    }

    for (std::size_t i = 0; i + 1 < fields.size(); i += 2) {
        if (fields[i] != "10") continue;
        auto decoded = urlDecode(fields[i + 1]);
        if (decoded.starts_with("https://") || decoded.starts_with("http://")) return decoded;
        return std::nullopt;
    }
    return std::nullopt;
}

bool hasAudioSignature(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<unsigned char, 12> header {};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    auto const count = input.gcount();
    if (count < 4) return false;

    // MP3 with ID3 tag or a raw MPEG audio frame.
    if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') return true;
    if (header[0] == 0xff && (header[1] & 0xe0) == 0xe0) return true;
    // Common formats FMOD can decode; accepting them avoids depending on the
    // remote URL's file extension.
    if (header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S') return true;
    if (header[0] == 'f' && header[1] == 'L' && header[2] == 'a' && header[3] == 'C') return true;
    if (count >= 12 &&
        header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E') return true;
    if (count >= 8 && header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') return true;
    return false;
}

bool usableFile(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    if (std::filesystem::file_size(path, ec) < kMinimumCachedSongBytes || ec) return false;
    return hasAudioSignature(path);
}

enum class PreparedStreamState {
    Pending,
    Ready,
    Error,
};

PreparedStreamState preparedStreamState(FMOD::Sound* sound, FMOD_RESULT* resultOut = nullptr) {
    if (!sound) return PreparedStreamState::Error;

    FMOD_OPENSTATE state = FMOD_OPENSTATE_ERROR;
    bool starving = false;
    auto const result = sound->getOpenState(&state, nullptr, &starving, nullptr);
    if (resultOut) *resultOut = result;
    if (result == FMOD_ERR_NOTREADY) return PreparedStreamState::Pending;
    if (result != FMOD_OK || state == FMOD_OPENSTATE_ERROR) return PreparedStreamState::Error;
    if (!starving && (state == FMOD_OPENSTATE_READY || state == FMOD_OPENSTATE_PLAYING)) {
        return PreparedStreamState::Ready;
    }
    return PreparedStreamState::Pending;
}

RankedAudioMode pendingTimeoutFallback(RankedAudioMode mode) {
    switch (mode) {
        case RankedAudioMode::Round3DeathmatchWait:
        case RankedAudioMode::ResultWin:
        case RankedAudioMode::ResultLose:
            return RankedAudioMode::Round12;
        case RankedAudioMode::Round12:
        case RankedAudioMode::LobbyPromotion:
        case RankedAudioMode::LobbyDemotionWarning:
            return RankedAudioMode::Lobby;
        case RankedAudioMode::Lobby:
        case RankedAudioMode::Silent:
            return RankedAudioMode::Silent;
    }
    return RankedAudioMode::Silent;
}
} // namespace

RankedAudioManager& RankedAudioManager::get() {
    static RankedAudioManager instance;
    return instance;
}

std::string RankedAudioManager::configSignature(RankedClientPresentationView const& config) const {
    std::ostringstream stream;
    stream << (config.audio.enabled ? 1 : 0) << ':'
           << config.audio.fadeInSeconds << ':' << config.audio.fadeOutSeconds << ':'
           << config.ui.fadeInSeconds << ':' << config.ui.fadeOutSeconds;
    for (auto const& resource : config.audio.resources) {
        stream << '|' << resource.key << ':' << resource.songId << ':'
               << resource.startSeconds << ':' << (resource.loop ? 1 : 0);
    }
    return stream.str();
}

void RankedAudioManager::configure(RankedClientPresentationView const& config) {
    auto const signature = configSignature(config);
    // Common gain is deliberately excluded from the media identity. Updating
    // it must preserve the playing/seeked streams and the pending DSP fades.
    if (signature == m_configSignature) { m_config = config; return; }

    cancelActiveDownload();
    cancelPendingAudioStart();
    stopOwnedAudio();
    releasePreparedSounds();
    m_config = config;
    m_configSignature = signature;
    m_readySongPaths.clear();
    synchronizeSongCaches();
    m_downloadQueue.clear();
    m_failedSongIds.clear();
    m_downloadState = resourcesReady() ? RankedResourceDownloadState::Ready : RankedResourceDownloadState::Idle;
    if (m_downloadState == RankedResourceDownloadState::Ready) prewarmReadySounds();

    m_switchPending = false;
    m_playbackVerifyPending = false;
    m_playbackVerifyAt = {};
    m_playbackRetryCount = 0;
    m_pendingStartMs = 0;
    m_desiredMode = RankedAudioMode::Silent;
    m_requestedMode = RankedAudioMode::Silent;
    m_failedPlaybackKeys.clear();
    m_audioStartBeganAt = {};
}

bool RankedAudioManager::enabled() const {
    return m_config.audio.enabled && !m_config.audio.resources.empty();
}

std::vector<int> RankedAudioManager::uniqueSongIds() const {
    std::vector<int> result;
    for (auto const& resource : m_config.audio.resources) {
        if (resource.songId <= 0) continue;
        if (std::find(result.begin(), result.end(), resource.songId) == result.end()) {
            result.push_back(resource.songId);
        }
    }
    return result;
}

bool RankedAudioManager::gdSongReady(int songId) const {
    if (songId <= 0) return true;
    auto* manager = MusicDownloadManager::sharedState();
    return manager && (manager->isResourceSong(songId) || manager->isSongDownloaded(songId));
}

std::filesystem::path RankedAudioManager::cachedSongPath(int songId) const {
    auto* mod = Mod::get();
    if (!mod) return {};
    return mod->getSaveDir() / "ranked-audio" / (std::to_string(songId) + ".mp3");
}

std::filesystem::path RankedAudioManager::preferredDownloadPath(int songId) const {
    // The GD custom-song path is not a stable cache key on every Android setup:
    // storage providers and MusicDownloadManager hooks can resolve the same song
    // to a different external path after reopening Ranked. Keep the authoritative
    // copy inside this mod's persistent save directory and mirror it to GD only
    // for compatibility.
    return cachedSongPath(songId);
}

std::string RankedAudioManager::readySongPath(int songId) const {
    if (songId <= 0) return {};
    if (auto found = m_readySongPaths.find(songId); found != m_readySongPaths.end()) {
        return found->second;
    }

    auto const cached = cachedSongPath(songId);
    if (usableFile(cached)) {
        auto path = cached.string();
        m_readySongPaths.emplace(songId, path);
        return path;
    }

    if (auto* manager = MusicDownloadManager::sharedState()) {
        // A native download may already have an audio header while its file is
        // still incomplete. Only the persistent copy is usable during it.
        if (nativeSongDownloadActive(manager, songId)) return {};
        auto const path = manager->pathForSong(songId);
        if (!path.empty()) {
            auto const fsPath = std::filesystem::path(path.c_str());
            // Trust built-in resource songs, and also accept a valid on-disk song
            // even if GD's in-memory "downloaded" state has not refreshed yet.
            if (manager->isResourceSong(songId) || usableFile(fsPath)) {
                auto readyPath = std::string(path.c_str());
                m_readySongPaths.emplace(songId, readyPath);
                return readyPath;
            }
        }
    }
    return {};
}

bool RankedAudioManager::songReady(int songId) const {
    if (songId <= 0) return true;
    return !readySongPath(songId).empty();
}

bool RankedAudioManager::resourcesReady() const {
    if (!enabled()) return true;
    for (auto const songId : uniqueSongIds()) {
        if (!songReady(songId)) return false;
    }
    return true;
}

bool RankedAudioManager::requiresResourceDownload() const {
    return enabled() && !resourcesReady();
}

RankedResourceDownloadView RankedAudioManager::downloadView() const {
    RankedResourceDownloadView view;
    auto const ids = uniqueSongIds();
    view.total = static_cast<int>(ids.size());
    for (auto const id : ids) {
        if (songReady(id)) ++view.ready;
    }
    view.failed = static_cast<int>(m_failedSongIds.size());
    view.activeSongId = m_activeDownloadSongId;
    view.state = resourcesReady() ? RankedResourceDownloadState::Ready : m_downloadState;
    view.activeProgress = m_activeDownloadSongId > 0 ? clampProgress(m_activeDownloadProgress) : 0;
    return view;
}

void RankedAudioManager::cancelActiveDownload() {
    ++m_downloadGeneration;
    m_songInfoRequest.cancel();
    m_songFileRequest.cancel();
    m_activeDownloadSongId = 0;
    m_activeDownloadProgress = 0;
    m_downloadStartedAt = {};
    m_libraryUrlRequested = false;
    m_librarySongRequested = false;
    m_libraryQuietSince = {};
}

void RankedAudioManager::downloadAll() {
    cancelActiveDownload();
    if (!enabled()) {
        m_downloadState = RankedResourceDownloadState::Ready;
        return;
    }

    m_downloadQueue.clear();
    m_failedSongIds.clear();
    for (auto const id : uniqueSongIds()) {
        if (!songReady(id)) m_downloadQueue.push_back(id);
    }
    if (m_downloadQueue.empty()) {
        m_downloadState = RankedResourceDownloadState::Ready;
        prewarmReadySounds();
        return;
    }
    m_downloadState = RankedResourceDownloadState::Downloading;
    startNextDownload();
}

void RankedAudioManager::retryFailed() {
    downloadAll();
}

void RankedAudioManager::startNextDownload() {
    if (m_activeDownloadSongId > 0) return;
    while (!m_downloadQueue.empty() && songReady(m_downloadQueue.front())) {
        m_downloadQueue.pop_front();
    }
    if (m_downloadQueue.empty()) {
        m_downloadState = m_failedSongIds.empty()
            ? RankedResourceDownloadState::Ready
            : RankedResourceDownloadState::Failed;
        if (m_downloadState == RankedResourceDownloadState::Ready) prewarmReadySounds();
        return;
    }

    m_activeDownloadSongId = m_downloadQueue.front();
    m_downloadQueue.pop_front();
    m_activeDownloadProgress = 0;
    m_downloadStartedAt = std::chrono::steady_clock::now();
    m_libraryUrlRequested = false;
    m_librarySongRequested = false;
    m_libraryQuietSince = {};
    fetchSongInfo(m_activeDownloadSongId);
}

void RankedAudioManager::fetchSongInfo(int songId) {
    // GD Music Library uses the game's custom-content service, not the
    // Newgrounds metadata endpoint. Let GD resolve and download that source.
    if (songId >= kMusicLibraryFirstSongId) {
        updateLibraryDownload();
        return;
    }
    auto const generation = m_downloadGeneration;
    web::WebRequest request;
    request.timeout(kSongInfoTimeout);
    request.header("Content-Type", "application/x-www-form-urlencoded");
    request.bodyString(fmt::format("songID={}&secret={}", songId, kGdSecret));

    log::info("Ranked audio: fetching metadata for GD song {}", songId);
    m_songInfoRequest.spawn(
        request.post(kSongInfoEndpoint),
        [this, generation, songId](web::WebResponse response) {
            if (generation != m_downloadGeneration || songId != m_activeDownloadSongId) return;
            if (!response.ok()) {
                finishDownloadFailure(songId, fmt::format("song info HTTP {}", response.code()));
                return;
            }
            auto body = response.string();
            if (body.isErr()) {
                finishDownloadFailure(songId, "invalid song info response");
                return;
            }
            auto const url = songDownloadUrl(body.unwrap());
            if (!url) {
                finishDownloadFailure(songId, "song download URL missing");
                return;
            }
            downloadSongFile(songId, *url);
        }
    );
}

void RankedAudioManager::updateLibraryDownload() {
    auto const songId = m_activeDownloadSongId;
    if (songId < kMusicLibraryFirstSongId) return;
    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) {
        finishDownloadFailure(songId, "native music downloader unavailable");
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    if (nativeSongDownloadActive(manager, songId)) {
        // Join an existing request instead of issuing a duplicate or taking
        // over the shared MusicDownloadDelegate chain.
        m_librarySongRequested = true;
        m_libraryQuietSince = {};
        m_activeDownloadProgress = std::clamp(manager->getDownloadProgress(songId), 0, 99);
        return;
    }
    if (songReady(songId)) {
        finishDownloadSuccess(songId);
        return;
    }

    if (!m_librarySongRequested) {
        if (manager->m_customContentURL.empty()) {
            if (now - m_downloadStartedAt >= kSongInfoTimeout) {
                finishDownloadFailure(songId, "Music Library content URL timed out");
            } else if (!m_libraryUrlRequested) {
                m_libraryUrlRequested = true;
                manager->getCustomContentURL();
            }
            return;
        }
        m_librarySongRequested = true;
        m_libraryQuietSince = now;
        log::info("Ranked audio: downloading Music Library song {} through GD", songId);
        manager->downloadSong(songId);
        return;
    }

    // Native failures have no Corum web callback. Bound the inactive wait and
    // keep the existing resource dialog's retry path available.
    if (m_libraryQuietSince == std::chrono::steady_clock::time_point{}) m_libraryQuietSince = now;
    if (now - m_libraryQuietSince >= kLibraryDownloadQuietTimeout) {
        finishDownloadFailure(songId, "Music Library download ended without valid audio");
    }
}

void RankedAudioManager::downloadSongFile(int songId, std::string url) {
    auto const generation = m_downloadGeneration;
    web::WebRequest request;
    request.timeout(kSongFileTimeout);
    request.followRedirects(true);
    request.onProgress([this, generation, songId](web::WebProgress const& progress) {
        if (generation != m_downloadGeneration || songId != m_activeDownloadSongId) return;
        if (auto value = progress.downloadProgress()) {
            m_activeDownloadProgress = clampProgress(static_cast<int>(std::lround(*value)));
        }
    });

    log::info("Ranked audio: downloading GD song {}", songId);
    m_songFileRequest.spawn(
        request.get(std::move(url)),
        [this, generation, songId](web::WebResponse response) {
            if (generation != m_downloadGeneration || songId != m_activeDownloadSongId) return;
            if (!response.ok()) {
                finishDownloadFailure(songId, fmt::format("audio HTTP {}", response.code()));
                return;
            }

            auto const target = preferredDownloadPath(songId);
            if (target.empty()) {
                finishDownloadFailure(songId, "song path unavailable");
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                finishDownloadFailure(songId, "failed to create song directory");
                return;
            }
            auto write = response.into(target);
            if (write.isErr() || !usableFile(target)) {
                std::filesystem::remove(target, ec);
                finishDownloadFailure(songId, "failed to save downloaded audio");
                return;
            }
            finishDownloadSuccess(songId);
        }
    );
}

void RankedAudioManager::finishDownloadSuccess(int songId) {
    if (songId != m_activeDownloadSongId) return;
    log::info("Ranked audio: GD song {} saved successfully", songId);
    m_readySongPaths.erase(songId);
    synchronizeSongCaches();
    m_failedSongIds.erase(songId);
    m_activeDownloadProgress = 100;
    m_activeDownloadSongId = 0;
    m_downloadStartedAt = {};
    startNextDownload();
}

void RankedAudioManager::finishDownloadFailure(int songId, std::string const& reason) {
    if (songId != m_activeDownloadSongId) return;
    log::warn("Ranked audio: GD song {} download failed: {}", songId, reason);
    m_failedSongIds.insert(songId);
    m_activeDownloadSongId = 0;
    m_activeDownloadProgress = 0;
    m_downloadStartedAt = {};
    startNextDownload();
}

RankedAudioResourceView const* RankedAudioManager::findResource(std::string const& key) const {
    auto const found = std::find_if(
        m_config.audio.resources.begin(),
        m_config.audio.resources.end(),
        [&key](auto const& resource) { return resource.key == key; }
    );
    return found == m_config.audio.resources.end() ? nullptr : &*found;
}

RankedAudioResourceView const* RankedAudioManager::resourceForMode(RankedAudioMode mode) const {
    auto first = [this](std::initializer_list<char const*> keys) -> RankedAudioResourceView const* {
        for (auto const key : keys) {
            if (auto const* resource = findResource(key)) return resource;
        }
        return nullptr;
    };
    switch (mode) {
        case RankedAudioMode::Lobby:
            return first({"menu"});
        case RankedAudioMode::LobbyDemotionWarning:
            return first({"menu_demotion_warning", "menu"});
        case RankedAudioMode::LobbyPromotion:
            return first({"menu_promotion", "menu"});
        case RankedAudioMode::Round12:
            return first({"match", "menu"});
        case RankedAudioMode::Round3DeathmatchWait:
            return first({"round3_deathmatch_wait", "match", "menu"});
        case RankedAudioMode::ResultWin:
            return first({"result_win", "match", "menu"});
        case RankedAudioMode::ResultLose:
            return first({"result_lose", "match", "menu"});
        case RankedAudioMode::Silent:
            break;
    }
    return nullptr;
}

void RankedAudioManager::setMode(RankedAudioMode mode) {
    if (!enabled() || !resourcesReady()) mode = RankedAudioMode::Silent;
    if (mode != m_requestedMode) {
        m_requestedMode = mode;
        m_failedPlaybackKeys.clear();
        // Retry a failed preparation only on a NEW request, never every UI tick.
        std::erase_if(m_preparedChannels, [this](auto const& entry) {
            if (!entry.second.failed) return false;
            if (entry.second.channel) {
                entry.second.channel->stop();
                m_consumedPreparedKeys.insert(entry.first);
            }
            return true;
        });
    }
    mode = availableMode(mode);
    auto const* desired = resourceForMode(mode);
    auto const desiredKey = desired ? desired->key : std::string();

    if (mode == m_desiredMode) {
        if (m_switchPending || m_ownedFadeOutPending || m_playbackVerifyPending) return;
        if (!desired && (!m_ownsMusicChannel || !m_failedPlaybackKeys.empty())) return;
        if (desired && desiredKey == m_playingKey && ownedChannelPlaying()) return;
    }

    m_desiredMode = mode;
    TransitionTrace::get().mark("audio-mode-request");
    requestAudioSwitch();
}

bool RankedAudioManager::ownedChannelPlaying() const {
    if (!m_rankedChannel) return false;
    bool playing = false;
    bool paused = false;
    bool muted = false;
    if (m_rankedChannel->isPlaying(&playing) != FMOD_OK || !playing) return false;
    if (m_rankedChannel->getPaused(&paused) != FMOD_OK || paused) return false;
    if (m_rankedChannel->getMute(&muted) != FMOD_OK || (muted && !m_ownedStreamMuted)) return false;
    return true;
}

float RankedAudioManager::targetRankedVolume() const {
    return targetRankedVolume(m_playingKey);
}

float RankedAudioManager::targetRankedVolume(std::string const& key) const {
    auto* engine = FMODAudioEngine::sharedEngine();
    auto const gdMusicVolume = engine ? std::clamp(engine->m_musicVolume, 0.0f, 1.0f) : 1.0f;
    auto const* resource = findResource(key);
    auto const configured = resource ? resource->volume : 1.0;
    auto const common = std::isfinite(configured) ? std::clamp(configured, 0.0, 1.0) : 1.0;
    return gdMusicVolume * kRankedRelativeVolume * static_cast<float>(common);
}

void RankedAudioManager::refreshVolume() {
    if (!m_musicGroup) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || engine->m_system != m_musicSystem) {
        // Handles from a replaced FMOD System are no longer ours to dereference.
        m_musicGroup = nullptr; m_musicSystem = nullptr; m_appliedMenuVolume = -1;
        return;
    }
    auto const volume = RankedVolumeControl::get().volume();
    if (volume != m_appliedMenuVolume && m_musicGroup->setVolume(volume) == FMOD_OK)
        m_appliedMenuVolume = volume;
}

FMOD::ChannelGroup* RankedAudioManager::ownedMusicGroup() {
    refreshVolume();
    if (m_musicGroup) return m_musicGroup;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return nullptr;
    FMOD::ChannelGroup* group = nullptr;
    FMOD::ChannelGroup* master = nullptr;
    auto result = engine->m_system->createChannelGroup("Corum Ranked menu BGM", &group);
    if (result == FMOD_OK && group) result = engine->m_system->getMasterChannelGroup(&master);
    auto const volume = RankedVolumeControl::get().volume();
    if (result == FMOD_OK && master && group) result = group->setVolume(volume);
    if (result == FMOD_OK && master && group) result = master->addGroup(group);
    if (result != FMOD_OK || !master || !group) {
        if (group) group->release();
        return nullptr;
    }
    m_musicSystem = engine->m_system; m_musicGroup = group; m_appliedMenuVolume = volume;
    return group;
}

void RankedAudioManager::releaseMusicGroup() {
    // Call only after active, retiring, pending and standby channels have stopped.
    auto* engine = FMODAudioEngine::sharedEngine();
    if (m_musicGroup && engine && engine->m_system == m_musicSystem) m_musicGroup->release();
    m_musicGroup = nullptr; m_musicSystem = nullptr; m_appliedMenuVolume = -1;
}

void RankedAudioManager::ensureFmodOutputActive() const {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // iOS/iPadOS can leave the FMOD master group paused/muted across scene/audio
    // lifecycle transitions even while an individual Channel reports itself as
    // playing. Geometry Dash's background -> foreground resume path clears this,
    // which is why alpha.32 could suddenly become audible only after minimizing
    // the app once. Ranked explicitly normalizes the active-game output group.
    FMOD::ChannelGroup* master = nullptr;
    if (engine->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
        master->setPaused(false);
        master->setMute(false);
    }
}

void RankedAudioManager::stopOwnedAudio() {
    if (m_retiringChannel) {
        m_retiringChannel->stop();
        m_retiringChannel = nullptr;
    }
    m_retiringUntil = {};
    m_retiringKey.clear();
    m_retiringSound = nullptr;
    m_retiringStreamMuted = false;
    if (m_rankedChannel) {
        m_rankedChannel->stop();
        m_rankedChannel = nullptr;
    }
    // Used stream handles stay owned by the cache or retained list until their
    // active/fading channel stops. Each subsequent play uses a fresh buffer.
    m_rankedSound = nullptr;
    m_ownedStreamMuted = false;
    m_streamStarvedAt = {};
    m_ownsMusicChannel = false;
    m_playingKey.clear();
    m_playbackVerifyPending = false;
    m_playbackVerifyAt = {};
    m_playbackVerifyStartPositionMs = 0;
    m_ownedFadeInPending = false;
    m_ownedDspFadeIn = false;
    m_ownedFadeDspStart = 0;
    m_ownedFadeDspEnd = 0;
    m_ownedFadeOutPending = false;
    m_ownedFadeStartedAt = {};
    m_ownedFadeDurationSeconds = 0.0;
    // Avoid touching the Geometry Dash audio singleton during teardown.
    m_ownedFadeStartVolume = kRankedRelativeVolume;
}

void RankedAudioManager::clearPlaybackState() {
    cancelPendingAudioStart();
    stopOwnedAudio();
}

void RankedAudioManager::releasePreparedSounds() {
    // Stop every channel before releasing shared stream handles.
    cancelPendingAudioStart();
    stopOwnedAudio();
    stopPreparedChannels();
    releaseMusicGroup();
    m_rankedSound = nullptr;
    for (auto& [songId, sound] : m_preparedSounds) {
        (void)songId;
        if (sound) sound->release();
    }
    m_preparedSounds.clear();
    for (auto* sound : m_retainedSounds) if (sound) sound->release();
    m_retainedSounds.clear();
    m_consumedPreparedKeys.clear();
}

FMOD::Sound* RankedAudioManager::preparedSound(RankedAudioResourceView const& resource) {
    auto const songId = resource.songId;
    if (auto found = m_preparedSounds.find(resource.key); found != m_preparedSounds.end()) {
        if (!m_consumedPreparedKeys.contains(resource.key) &&
            preparedStreamState(found->second) != PreparedStreamState::Error) return found->second;
        // Retain active/fading/pending or still-loading handles. release() can
        // wait for decoder work; never release a loading stream in a switch.
        if (auto channel = m_preparedChannels.find(resource.key); channel != m_preparedChannels.end()) {
            if (channel->second.channel) channel->second.channel->stop();
            m_preparedChannels.erase(channel);
        }
        m_retainedSounds.push_back(found->second);
        m_preparedSounds.erase(found);
        m_consumedPreparedKeys.erase(resource.key);
    }
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return nullptr;
    auto const path = readySongPath(songId);
    if (path.empty()) return nullptr;

    FMOD_CREATESOUNDEXINFO info {};
    info.cbsize = sizeof(info);
    info.initialseekposition = static_cast<unsigned int>(std::clamp(
        resource.startSeconds * 1000.0, 0.0,
        static_cast<double>(std::numeric_limits<unsigned int>::max())));
    info.initialseekpostype = FMOD_TIMEUNIT_MS;
    auto const started = std::chrono::steady_clock::now();
    FMOD::Sound* sound = nullptr;
    auto const result = engine->m_system->createSound(path.c_str(),
        FMOD_CREATESTREAM | FMOD_2D | FMOD_NONBLOCKING | FMOD_ACCURATETIME |
            (resource.loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF), &info, &sound);
    auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    if (result != FMOD_OK || !sound) {
        log::error("Ranked audio: initial-offset open failed key='{}' song={} result={}",
            resource.key, songId, static_cast<int>(result));
        if (sound) m_retainedSounds.push_back(sound);
        return nullptr;
    }
    m_preparedSounds[resource.key] = sound;
    TransitionTrace::get().mark(fmt::format("audio-prebuffer-requested:{}:{}us", resource.key, elapsedUs));
    log::info("[RankedAudio] prebuffer key='{}' song={} initial={}ms open={}us",
        resource.key, songId, info.initialseekposition, elapsedUs);
    return sound;
}

void RankedAudioManager::prewarmReadySounds() {
    if (!enabled() || !resourcesReady()) return;
    for (auto const& resource : m_config.audio.resources) {
        if (resource.songId > 0 && !resource.key.empty()) (void)preparedSound(resource);
    }
}

FMOD::Channel* RankedAudioManager::preparedChannel(RankedAudioResourceView const& resource,
    FMOD::Sound* sound, int& result) {
    result = FMOD_ERR_NOTREADY;
    if (!sound) { result = FMOD_ERR_INVALID_HANDLE; return nullptr; }
    auto& prepared = m_preparedChannels[resource.key];
    auto const now = std::chrono::steady_clock::now();
    if (prepared.beganAt == std::chrono::steady_clock::time_point{}) prepared.beganAt = now;
    auto fail = [&](int error, char const* phase) -> FMOD::Channel* {
        prepared.failed = true; prepared.result = error; result = error;
        log::warn("[RankedAudio] channel preparation failed key='{}' phase={} result={}",
            resource.key, phase, error);
        return nullptr;
    };
    if (prepared.failed) { result = prepared.result; return nullptr; }

    FMOD_RESULT openResult = FMOD_OK;
    auto const state = preparedStreamState(sound, &openResult);
    if (state == PreparedStreamState::Error) return fail(static_cast<int>(openResult), "open");
    if (state == PreparedStreamState::Pending) {
        if (now - prepared.beganAt >= kPendingAudioStartTimeout)
            return fail(FMOD_ERR_NOTREADY, "buffer-timeout");
        return nullptr;
    }

    if (!prepared.channel) {
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return fail(FMOD_ERR_INVALID_HANDLE, "engine");
        auto* group = ownedMusicGroup();
        if (!group) return fail(FMOD_ERR_NOTREADY, "menu-group");
        sound->setMode(FMOD_2D | (resource.loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
        sound->setLoopCount(resource.loop ? -1 : 0);
        auto const playResult = engine->m_system->playSound(sound, group, true, &prepared.channel);
        if (playResult == FMOD_ERR_NOTREADY) {
            if (now - prepared.beganAt >= kPendingAudioStartTimeout)
                return fail(playResult, "play-timeout");
            return nullptr;
        }
        if (playResult != FMOD_OK || !prepared.channel)
            return fail(playResult == FMOD_OK ? FMOD_ERR_INVALID_HANDLE : playResult, "play");
        prepared.channel->setVolume(0);
        prepared.channel->setMute(true);
    }

    auto const startMs = static_cast<unsigned int>(std::clamp(resource.startSeconds * 1000.0,
        0.0, static_cast<double>(std::numeric_limits<unsigned int>::max())));
    unsigned int position = 0;
    auto const positionResult = prepared.channel->getPosition(&position, FMOD_TIMEUNIT_MS);
    if (positionResult != FMOD_OK && positionResult != FMOD_ERR_NOTREADY)
        return fail(positionResult, "position");
    if (positionResult == FMOD_OK && std::abs(static_cast<double>(position) - startMs) <= 250.0) {
        result = FMOD_OK;
        return prepared.channel;
    }
    if (now - prepared.beganAt >= kPendingAudioStartTimeout)
        return fail(FMOD_ERR_NOTREADY, "position-timeout");
    if (positionResult == FMOD_ERR_NOTREADY) return nullptr;
    if (!prepared.seekSubmitted) {
        // beta38 cancelled a successful playSound here. A READY Sound alone
        // does not prove that this Channel reports the requested initial seek.
        // Repair ONLY the silent candidate, once. NONBLOCKING flushes/decodes
        // asynchronously; getPosition may remain stale until it completes.
        auto const seekResult = prepared.channel->setPosition(startMs, FMOD_TIMEUNIT_MS);
        if (seekResult == FMOD_ERR_NOTREADY) return nullptr;
        log::info("[RankedAudio] prepare offset key='{}' expected={}ms observed={}ms seekResult={}",
            resource.key, startMs, position, static_cast<int>(seekResult));
        if (seekResult != FMOD_OK) return fail(seekResult, "seek");
        prepared.seekSubmitted = true;
    }
    // Wait for a subsequent audio heartbeat, READY/non-starving and the actual
    // position. Never replay/reseek an audible or fading predecessor.
    return nullptr;
}

void RankedAudioManager::prewarmReadyChannels() {
    if (!enabled() || m_requestedMode == RankedAudioMode::Silent) return;
    for (auto const& resource : m_config.audio.resources) {
        if (m_consumedPreparedKeys.contains(resource.key) || m_failedPlaybackKeys.contains(resource.key)) continue;
        auto const found = m_preparedSounds.find(resource.key);
        if (found == m_preparedSounds.end()) continue;
        int result = 0;
        (void)preparedChannel(resource, found->second, result);
    }
}

void RankedAudioManager::stopPreparedChannels() {
    for (auto const& [key, prepared] : m_preparedChannels) if (prepared.channel) {
        prepared.channel->stop();
        // Even a muted first play consumes initialseekposition.
        m_consumedPreparedKeys.insert(key);
    }
    m_preparedChannels.clear();
}

void RankedAudioManager::reapRetainedSounds() {
    std::erase_if(m_retainedSounds, [this](FMOD::Sound* sound) {
        if (sound == m_rankedSound || sound == m_retiringSound || sound == m_pendingSound ||
            preparedStreamState(sound) == PreparedStreamState::Pending) return false;
        if (sound) sound->release();
        return true;
    });
}

void RankedAudioManager::updateStreamHealth() {
    auto observe = [this](FMOD::Sound* sound, FMOD::Channel* channel, bool& muted, char const* owner) {
        if (!sound || !channel) return;
        FMOD_OPENSTATE state = FMOD_OPENSTATE_ERROR;
        unsigned int buffered = 0;
        bool starving = false, diskBusy = false;
        auto const result = sound->getOpenState(&state, &buffered, &starving, &diskBusy);
        auto const mustMute = result != FMOD_OK || starving ||
            (state != FMOD_OPENSTATE_READY && state != FMOD_OPENSTATE_PLAYING);
        channel->setMute(mustMute);
        if (muted != mustMute && m_streamDiagnostics++ < 12)
            log::warn("[RankedAudio] buffer owner={} key='{}' muted={} state={} starving={} buffered={} diskBusy={} result={}",
                owner, m_playingKey, mustMute, static_cast<int>(state), starving, buffered,
                diskBusy, static_cast<int>(result));
        muted = mustMute;
    };
    observe(m_rankedSound, m_rankedChannel, m_ownedStreamMuted, "active");
    observe(m_retiringSound, m_retiringChannel, m_retiringStreamMuted, "retiring");
    auto const now = std::chrono::steady_clock::now();
    if (!m_ownedStreamMuted) m_streamStarvedAt = {};
    else if (m_streamStarvedAt == std::chrono::steady_clock::time_point{}) m_streamStarvedAt = now;
    else if (!m_switchPending && now - m_streamStarvedAt >= kPendingAudioStartTimeout &&
        !m_failedPlaybackKeys.contains(m_playingKey)) failDesiredAudio("active-buffer-timeout");
}

void RankedAudioManager::synchronizeSongCaches() {
    auto* manager = MusicDownloadManager::sharedState();

    for (auto const songId : uniqueSongIds()) {
        // Never import an unfinished native file or overwrite a shared
        // downloader's destination with our compatibility mirror.
        if (nativeSongDownloadActive(manager, songId)) continue;
        auto const privatePath = cachedSongPath(songId);
        auto gdPath = std::filesystem::path();
        if (manager) {
            auto const gdRawPath = manager->pathForSong(songId);
            if (!gdRawPath.empty()) gdPath = std::filesystem::path(gdRawPath.c_str());
        }

        auto privateReady = usableFile(privatePath);
        auto gdReady = !gdPath.empty() && usableFile(gdPath);
        if (!privateReady && !gdReady) continue;

        std::error_code ec;
        if (!privateReady && gdReady) {
            if (!privatePath.parent_path().empty()) {
                std::filesystem::create_directories(privatePath.parent_path(), ec);
            }
            if (ec) {
                log::warn("Ranked audio: failed to create persistent cache directory for {}: {}", songId, ec.message());
                continue;
            }
            std::filesystem::copy_file(
                gdPath,
                privatePath,
                std::filesystem::copy_options::overwrite_existing,
                ec
            );
            privateReady = !ec && usableFile(privatePath);
            if (!privateReady) {
                log::warn("Ranked audio: failed to import GD song {} into persistent cache", songId);
                continue;
            }
            log::info("Ranked audio: imported song {} into persistent cache '{}'", songId, privatePath.string());
        }

        // Preserve the private copy. The GD path is only a best-effort mirror;
        // failure here must never make the Ranked resource gate reappear.
        if (privateReady && !gdPath.empty() && !gdReady) {
            ec.clear();
            if (!gdPath.parent_path().empty()) {
                std::filesystem::create_directories(gdPath.parent_path(), ec);
            }
            if (!ec) {
                std::filesystem::copy_file(
                    privatePath,
                    gdPath,
                    std::filesystem::copy_options::overwrite_existing,
                    ec
                );
            }
            if (ec || !usableFile(gdPath)) {
                log::warn("Ranked audio: GD mirror unavailable for song {}; persistent cache remains ready", songId);
            } else {
                log::info("Ranked audio: mirrored song {} into GD song storage", songId);
            }
        }
    }
}

void RankedAudioManager::beginOwnedFadeOut(double seconds) {
    // Silent uses the same DSP retirement as a replacement. Changing only the
    // base volume during an incomplete DSP fade would multiply two envelopes.
    retireOwnedChannel(seconds);
}

void RankedAudioManager::requestAudioSwitch() {
    auto const* desired = resourceForMode(m_desiredMode);
    auto const desiredKey = desired ? desired->key : std::string();
    if (desired && m_ownsMusicChannel && desiredKey == m_playingKey && ownedChannelPlaying()) {
        cancelPendingAudioStart();
        m_switchPending = false;
        return;
    }

    auto const fadeOut = std::max(0.0, m_config.audio.fadeOutSeconds);
    m_playbackVerifyPending = false;
    m_playbackRetryCount = 0;
    m_audioStartBeganAt = std::chrono::steady_clock::now();

    // Opening and seeking compressed streams can take seconds on affected
    // Android storage. Keep the current channel alive while FMOD prepares the
    // replacement asynchronously, then swap only after OPENSTATE_READY.
    if (desired) {
        if (!m_pendingKey.empty() && m_pendingKey != desiredKey) {
            cancelPendingAudioStart();
        }
        m_switchPending = true;
        startDesiredAudio();
        return;
    }

    cancelPendingAudioStart();
    if (m_rankedChannel) {
        m_switchPending = true;
        beginOwnedFadeOut(fadeOut);
        if (!m_ownedFadeOutPending) m_switchPending = false;
        return;
    }

    // No Ranked-owned channel exists, so this is normally the vanilla GD menu
    // track. Let GD fade it, then hard-stop it immediately before starting the
    // independent Ranked FMOD stream.
    m_switchPending = false;
}

RankedAudioMode RankedAudioManager::availableMode(RankedAudioMode requested) const {
    while (auto const* resource = resourceForMode(requested)) {
        if (!m_failedPlaybackKeys.contains(resource->key)) return requested;
        requested = pendingTimeoutFallback(requested);
    }
    return RankedAudioMode::Silent;
}

bool RankedAudioManager::audioStartTimedOut() const {
    return m_audioStartBeganAt != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() - m_audioStartBeganAt >= kPendingAudioStartTimeout;
}

void RankedAudioManager::failDesiredAudio(char const* phase, int result) {
    if (auto const* resource = resourceForMode(m_desiredMode)) {
        m_failedPlaybackKeys.insert(resource->key);
        log::warn("Ranked audio: transition failed key='{}' song={} phase={} result={}; holding fallback until mode changes",
            resource->key, resource->songId, phase, result);
    }
    cancelPendingAudioStart();
    m_switchPending = false;
    m_playbackVerifyPending = false;
    m_desiredMode = availableMode(m_requestedMode);
    // Do not release an asynchronously-opening Sound here: release itself can
    // wait for the decoder. Keep the current audible channel while fallback
    // prepares, and prevent the repeated UI mode from restarting this failure.
    if (resourceForMode(m_desiredMode)) {
        TransitionTrace::get().mark("audio-pending-fallback");
        requestAudioSwitch();
    }
}

void RankedAudioManager::retireOwnedChannel(double seconds) {
    auto* outgoing = m_rankedChannel;
    auto* outgoingSound = m_rankedSound;
    auto const outgoingKey = m_playingKey;
    auto const fadingIn = m_ownedDspFadeIn;
    auto const fadeStart = m_ownedFadeDspStart;
    auto const fadeEnd = m_ownedFadeDspEnd;
    m_rankedChannel = nullptr;
    stopOwnedAudio(); // clears previous retirement, not the detached outgoing
    if (!outgoing) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    unsigned long long clock = 0;
    int rate = 0;
    if (seconds > 0 && engine && engine->m_system &&
        engine->m_system->getSoftwareFormat(&rate, nullptr, nullptr) == FMOD_OK && rate > 0 &&
        outgoing->getDSPClock(nullptr, &clock) == FMOD_OK) {
        // Fade points multiply setVolume; getVolume excludes this envelope.
        // Interrupt a still-running fade at its actual DSP-clock gain, rather
        // than jumping to full volume or keeping its future fade-in endpoint.
        auto const gain = fadingIn && fadeEnd > fadeStart
            ? static_cast<float>(std::clamp(
                (static_cast<double>(clock) - static_cast<double>(fadeStart)) /
                    static_cast<double>(fadeEnd - fadeStart), 0.0, 1.0))
            : 1.0f;
        auto const end = clock + static_cast<unsigned long long>(seconds * rate);
        if (outgoing->removeFadePoints(clock, std::numeric_limits<unsigned long long>::max()) == FMOD_OK &&
            outgoing->addFadePoint(clock, gain) == FMOD_OK &&
            outgoing->addFadePoint(end, 0) == FMOD_OK &&
            outgoing->setDelay(0, end, true) == FMOD_OK) {
            m_retiringChannel = outgoing;
            m_retiringKey = outgoingKey;
            m_retiringSound = outgoingSound;
            m_retiringUntil = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(static_cast<int>(seconds * 1000));
            return;
        }
    }
    outgoing->stop();
}

void RankedAudioManager::startDesiredAudio(bool retry) {
    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || !songReady(resource->songId)) {
        cancelPendingAudioStart();
        m_switchPending = false;
        failDesiredAudio("resource");
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        cancelPendingAudioStart();
        m_switchPending = false;
        failDesiredAudio("engine");
        return;
    }

    auto const path = readySongPath(resource->songId);
    if (path.empty()) {
        cancelPendingAudioStart();
        m_switchPending = false;
        failDesiredAudio("path");
        return;
    }

    if (m_pendingChannel) {
        if (m_pendingKey != resource->key) cancelPendingAudioStart();
        else {
            updatePendingAudioStart();
            return;
        }
    }
    if (m_audioStartBeganAt == std::chrono::steady_clock::time_point{})
        m_audioStartBeganAt = std::chrono::steady_clock::now();

    // NONBLOCKING prewarming includes the exact initial seek and seek table.
    // Never call playSound until that prepared buffer is ready.
    auto* sound = preparedSound(*resource);
    FMOD_RESULT openResult = FMOD_OK;
    auto const openState = preparedStreamState(sound, &openResult);
    if (openState == PreparedStreamState::Pending) {
        m_switchPending = true;
        if (audioStartTimedOut()) failDesiredAudio("open-timeout");
        return;
    }
    if (openState == PreparedStreamState::Error) {
        log::error(
            "Ranked audio: prepared stream failed key='{}' song={} result={} path='{}'",
            resource->key,
            resource->songId,
            static_cast<int>(openResult),
            path
        );
        failDesiredAudio("open", static_cast<int>(openResult));
        return;
    }

    // Prewarming advances a paused channel as well as its Sound. Initial
    // position mismatches are repaired there, not treated as immediate failure.
    auto const playStarted = std::chrono::steady_clock::now();
    int prepareResult = FMOD_OK;
    auto* channel = preparedChannel(*resource, sound, prepareResult);
    auto const playElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - playStarted).count();
    if (!channel) {
        m_switchPending = true;
        auto const prepared = m_preparedChannels.find(resource->key);
        if (prepareResult != FMOD_ERR_NOTREADY ||
            (prepared != m_preparedChannels.end() && prepared->second.failed))
            failDesiredAudio("channel-prepare", prepareResult);
        else if (audioStartTimedOut()) failDesiredAudio("channel-prepare-timeout", prepareResult);
        return;
    }
    m_preparedChannels.erase(resource->key); // transfer the single-use channel

    // Keep the candidate paused/muted until its prepared offset is verified.
    // The predecessor stays audible unless its own stream is starving.
    m_consumedPreparedKeys.insert(resource->key);
    channel->setVolume(0.0f);
    channel->setMute(true);
    m_pendingSound = sound;
    m_pendingChannel = channel;
    m_pendingKey = resource->key;
    m_pendingPath = path;
    m_pendingStartMs = static_cast<unsigned int>(
        std::max(0.0, resource->startSeconds) * 1000.0
    );
    m_pendingStartedAt = std::chrono::steady_clock::now();
    m_pendingPlaybackRetry = retry;
    m_switchPending = true;

    log::debug(
        "Ranked audio transition playSound key='{}' song={} duration={}us prepared=true nonblocking=true",
        resource->key,
        resource->songId,
        playElapsedUs
    );
    updatePendingAudioStart();
}

void RankedAudioManager::updatePendingAudioStart() {
    if (!m_pendingSound || !m_pendingChannel) return;

    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || resource->key != m_pendingKey) {
        cancelPendingAudioStart();
        m_switchPending = false;
        return;
    }

    FMOD_RESULT openResult = FMOD_OK;
    auto const state = preparedStreamState(m_pendingSound, &openResult);
    if (state == PreparedStreamState::Pending) {
        if (audioStartTimedOut()) {
            TransitionTrace::get().mark("audio-pending-timeout");
            failDesiredAudio("buffer-seek-timeout", static_cast<int>(openResult));
        }
        return;
    }
    if (state == PreparedStreamState::Error) {
        log::error(
            "Ranked audio: asynchronous seek failed key='{}' song={} result={}",
            resource->key,
            resource->songId,
            static_cast<int>(openResult)
        );
        failDesiredAudio("async-seek", static_cast<int>(openResult));
        return;
    }

    // Revalidate the prepared channel before allowing audible data. Initial
    // offset mismatch/async seek was handled while warming, never by discarding
    // a successful playSound simply because its first getPosition was stale.
    unsigned int position = 0;
    auto const positionResult = m_pendingChannel->getPosition(&position, FMOD_TIMEUNIT_MS);
    if (positionResult == FMOD_ERR_NOTREADY) {
        if (audioStartTimedOut()) failDesiredAudio("initial-position-timeout", static_cast<int>(positionResult));
        return;
    }
    if (positionResult != FMOD_OK || std::abs(static_cast<double>(position) - m_pendingStartMs) > 250.0) {
        log::warn("[RankedAudio] prepared position changed key='{}' expected={}ms observed={}ms result={}",
            resource->key, m_pendingStartMs, position, static_cast<int>(positionResult));
        failDesiredAudio("prepared-position", static_cast<int>(positionResult));
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        failDesiredAudio("engine-after-seek");
        return;
    }

    auto* sound = m_pendingSound;
    auto* channel = m_pendingChannel;
    auto const key = m_pendingKey;
    auto const path = m_pendingPath;
    auto const startMs = m_pendingStartMs;
    auto const retry = m_pendingPlaybackRetry;
    auto const seekElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - m_pendingStartedAt
    ).count();

    // Detach the ready candidate before retiring the old active channel.
    m_pendingSound = nullptr;
    m_pendingChannel = nullptr;
    m_pendingKey.clear();
    m_pendingPath.clear();
    m_pendingStartMs = 0;
    m_pendingStartedAt = {};
    m_pendingPlaybackRetry = false;

    ensureFmodOutputActive();

    auto const fadeIn = std::clamp(m_config.audio.fadeInSeconds, 0.0, 10.0);
    unsigned long long clock = 0;
    int rate = 0;
    auto const dspFade = fadeIn > 0 &&
        engine->m_system->getSoftwareFormat(&rate, nullptr, nullptr) == FMOD_OK && rate > 0 &&
        channel->getDSPClock(nullptr, &clock) == FMOD_OK &&
        channel->addFadePoint(clock, 0) == FMOD_OK &&
        channel->addFadePoint(clock + static_cast<unsigned long long>(fadeIn * rate), 1) == FMOD_OK;
    if (!dspFade) channel->removeFadePoints(0, std::numeric_limits<unsigned long long>::max());
    // Base music volume and the DSP fade envelope are separate multipliers.
    // A zero base volume would leave the stream silent even after its DSP fade.
    channel->setVolume(dspFade || fadeIn <= 0 ? targetRankedVolume(key) : 0);
    channel->setMute(false);
    auto const unpauseResult = channel->setPaused(false);
    if (unpauseResult != FMOD_OK) {
        log::error(
            "Ranked audio: prepared channel start failed key='{}' song={} result={}",
            key,
            resource->songId,
            static_cast<int>(unpauseResult)
        );
        channel->stop();
        failDesiredAudio("unpause", static_cast<int>(unpauseResult));
        return;
    }

    // Accept the new channel before retiring the audible one. FMOD DSP ramps
    // run independently of the Ranked UI's 200ms polling/render heartbeat.
    if (!m_ownsMusicChannel) engine->stopAllMusic(true);
    retireOwnedChannel(std::clamp(m_config.audio.fadeOutSeconds, 0.0, 10.0));

    m_rankedSound = sound;
    m_rankedChannel = channel;
    m_ownedStreamMuted = false;
    m_streamStarvedAt = {};
    m_playbackVerifyStartPositionMs = startMs;
    m_playingKey = key;
    m_ownsMusicChannel = true;
    m_switchPending = false;
    m_playbackVerifyPending = true;
    m_playbackVerifyAt = std::chrono::steady_clock::now() + kPlaybackVerifyDelay;
    m_audioStartBeganAt = {};
    m_ownedFadeInPending = fadeIn > 0;
    m_ownedFadeStartedAt = std::chrono::steady_clock::now();
    m_ownedFadeDurationSeconds = fadeIn;
    m_ownedDspFadeIn = dspFade;
    m_ownedFadeDspStart = dspFade ? clock : 0;
    m_ownedFadeDspEnd = dspFade ? clock + static_cast<unsigned long long>(fadeIn * rate) : 0;

    TransitionTrace::get().mark(fmt::format("audio-prebuffer-ready:{}us", seekElapsedUs));
    TransitionTrace::get().mark("audio-channel-started");
    log::info(
        "Ranked audio: direct FMOD play key='{}' song={} path='{}' loop={} relativeVolume={} retry={} seek={}us",
        key,
        resource->songId,
        path,
        resource->loop,
        targetRankedVolume(),
        retry,
        seekElapsedUs
    );
}

void RankedAudioManager::cancelPendingAudioStart() {
    if (m_pendingChannel) m_pendingChannel->stop();
    m_pendingSound = nullptr;
    m_pendingChannel = nullptr;
    m_pendingKey.clear();
    m_pendingPath.clear();
    m_pendingStartMs = 0;
    m_pendingStartedAt = {};
    m_pendingPlaybackRetry = false;
}

void RankedAudioManager::verifyDesiredAudio() {
    if (!m_playbackVerifyPending) return;
    if (m_ownedStreamMuted) {
        m_playbackVerifyAt = std::chrono::steady_clock::now() + kPlaybackVerifyDelay;
        return;
    }
    m_playbackVerifyPending = false;

    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || resource->key != m_playingKey) return;
    bool progressed = false;
    unsigned int positionMs = 0;
    if (ownedChannelPlaying() && m_rankedChannel &&
        m_rankedChannel->getPosition(&positionMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
        // A loop can wrap below the configured start offset. Either a meaningful
        // forward delta or a wrap proves that the stream clock is actually moving.
        progressed = positionMs < m_playbackVerifyStartPositionMs ||
            positionMs >= m_playbackVerifyStartPositionMs + 20;
    }
    if (ownedChannelPlaying() && progressed) {
        log::info(
            "Ranked audio: direct playback verified for key='{}' song={} position={}ms",
            resource->key,
            resource->songId,
            positionMs
        );
        m_playbackRetryCount = 0;
        return;
    }

    if (m_playbackRetryCount < kMaxPlaybackRetries) {
        ++m_playbackRetryCount;
        log::warn(
            "Ranked audio: direct playback inactive for key='{}' song={}; retry {}/{}",
            resource->key,
            resource->songId,
            m_playbackRetryCount,
            kMaxPlaybackRetries
        );
        stopOwnedAudio();
        m_switchPending = true;
        m_audioStartBeganAt = std::chrono::steady_clock::now();
        startDesiredAudio(true);
        return;
    }

    log::error(
        "Ranked audio: direct playback failed after {} retries for key='{}' song={}",
        kMaxPlaybackRetries,
        resource->key,
        resource->songId
    );
    failDesiredAudio("verification-retries-exhausted");
}

void RankedAudioManager::updateOwnedFades(std::chrono::steady_clock::time_point now) {
    // The retiring track has its own gain even while the incoming track plays.
    // setVolume changes the base multiplier; FMOD keeps its DSP fade points.
    if (m_retiringChannel) m_retiringChannel->setVolume(targetRankedVolume(m_retiringKey));
    if (m_retiringChannel && now >= m_retiringUntil) {
        m_retiringChannel->stop();
        m_retiringChannel = nullptr;
        m_retiringSound = nullptr;
        m_retiringStreamMuted = false;
        m_retiringUntil = {};
        m_retiringKey.clear();
    }
    if (m_ownedFadeOutPending && m_rankedChannel) {
        auto const elapsed = std::chrono::duration<double>(now - m_ownedFadeStartedAt).count();
        auto const t = m_ownedFadeDurationSeconds <= 0.0
            ? 1.0
            : std::clamp(elapsed / m_ownedFadeDurationSeconds, 0.0, 1.0);
        m_rankedChannel->setVolume(m_ownedFadeStartVolume * static_cast<float>(1.0 - t));
        if (t >= 1.0) {
            m_ownedFadeOutPending = false;
            stopOwnedAudio();
            m_switchPending = false;
            if (resourceForMode(m_desiredMode)) startDesiredAudio();
        }
        return;
    }

    if (m_ownedFadeInPending && m_rankedChannel) {
        auto const elapsed = std::chrono::duration<double>(now - m_ownedFadeStartedAt).count();
        auto const t = m_ownedFadeDurationSeconds <= 0.0
            ? 1.0
            : std::clamp(elapsed / m_ownedFadeDurationSeconds, 0.0, 1.0);
        // DSP points already provide the ramp. Keep the per-song base gain
        // live during that ramp; only the software fallback multiplies by t.
        m_rankedChannel->setVolume(targetRankedVolume() * (m_ownedDspFadeIn ? 1.0f : static_cast<float>(t)));
        if (t >= 1.0) {
            m_ownedFadeInPending = false;
            m_ownedDspFadeIn = false;
        }
    }
}

void RankedAudioManager::fadeOutForGameplay() {
    // This is called only after vanilla successfully initializes PlayLayer. The
    // Ranked layer no longer ticks in gameplay, so stop the independently-owned
    // channel deterministically and hand audio ownership back to Geometry Dash.
    m_desiredMode = RankedAudioMode::Silent;
    m_requestedMode = RankedAudioMode::Silent;
    m_failedPlaybackKeys.clear();
    m_audioStartBeganAt = {};
    m_switchPending = false;
    cancelPendingAudioStart();
    stopOwnedAudio();
    // No paused standby channels may survive into native gameplay. Re-arm
    // their first-play streams for the next retained Ranked scene instead.
    stopPreparedChannels();
    releaseMusicGroup();
    prewarmReadySounds();
}

void RankedAudioManager::onApplicationPause() {
    // Ranked owns this FMOD channel outside Geometry Dash's normal music-slot
    // registry, so pause it explicitly when the app audio session is suspended.
    // This prevents the direct stream from escaping GD's normal background-audio
    // lifecycle on platforms where pauseSound() only visits registered channels.
    if (m_rankedChannel) m_rankedChannel->setPaused(true);
    if (m_retiringChannel) m_retiringChannel->setPaused(true);
}

void RankedAudioManager::onApplicationResume() {
    // iOS/iPadOS may leave an FMOD channel paused after returning from the
    // background even though the Sound itself is still valid. Defer recovery to
    // the next Ranked tick, after Geometry Dash has completed resumeSound().
    m_resumeRecoveryRequested = true;
}

void RankedAudioManager::restoreMenuMusic() {
    m_desiredMode = RankedAudioMode::Silent;
    m_requestedMode = RankedAudioMode::Silent;
    m_failedPlaybackKeys.clear();
    m_audioStartBeganAt = {};
    m_switchPending = false;
    m_playbackRetryCount = 0;
    cancelPendingAudioStart();
    stopOwnedAudio();
    stopPreparedChannels();
    releaseMusicGroup();
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->stopAllMusic(true);
    if (auto* game = GameManager::sharedState()) game->playMenuMusic();
}

void RankedAudioManager::tick() {
    refreshVolume();
    auto const now = std::chrono::steady_clock::now();
    updateOwnedFades(now);
    reapRetainedSounds();
    updateStreamHealth();

    if (m_activeDownloadSongId > 0 &&
        m_downloadStartedAt != std::chrono::steady_clock::time_point{} &&
        now - m_downloadStartedAt >= kDownloadGuardTimeout) {
        auto const songId = m_activeDownloadSongId;
        m_songInfoRequest.cancel();
        m_songFileRequest.cancel();
        finishDownloadFailure(songId, "download timed out");
    } else if (m_activeDownloadSongId >= kMusicLibraryFirstSongId) {
        updateLibraryDownload();
    } else if (m_activeDownloadSongId == 0 && m_downloadState == RankedResourceDownloadState::Downloading) {
        startNextDownload();
    }

    if (resourcesReady() && m_downloadState != RankedResourceDownloadState::Ready) {
        m_downloadState = RankedResourceDownloadState::Ready;
        m_failedSongIds.clear();
        prewarmReadySounds();
    }

    // Warm future Round/R3/Result channels during the visible Ranked scene;
    // the gameplay handoff stops them and the covered scene does not tick.
    prewarmReadyChannels();

    if (m_resumeRecoveryRequested) {
        m_resumeRecoveryRequested = false;
        if (m_desiredMode != RankedAudioMode::Silent && resourceForMode(m_desiredMode)) {
            ensureFmodOutputActive();
            if (m_rankedChannel) {
                m_rankedChannel->setPaused(false);
                m_rankedChannel->setMute(m_ownedStreamMuted);
            }
            if (!ownedChannelPlaying()) {
                log::warn("Ranked audio: recovering channel after application resume");
                startDesiredAudio(true);
            }
        }
    }

    // Respect the player's live Geometry Dash music-volume setting. Ranked's
    // own mix is intentionally 80% of that value. Do not fight an active fade.
    if (m_rankedChannel && !m_ownedFadeInPending && !m_ownedFadeOutPending) {
        ensureFmodOutputActive();
        m_rankedChannel->setPaused(false);
        m_rankedChannel->setMute(m_ownedStreamMuted);
        m_rankedChannel->setVolume(targetRankedVolume());
    }

    if (m_switchPending && !m_ownedFadeOutPending) {
        if (m_pendingChannel) updatePendingAudioStart();
        else startDesiredAudio();
    }
    if (m_playbackVerifyPending && now >= m_playbackVerifyAt) verifyDesiredAudio();

    // If another scene/mod unexpectedly kills the direct Ranked channel, recover
    // looped menu/match/result BGM instead of remaining silently "owned" forever.
    if (m_ownsMusicChannel && !m_switchPending && !m_ownedFadeOutPending && !m_playbackVerifyPending) {
        if (auto const* resource = resourceForMode(m_desiredMode); resource && resource->loop && !ownedChannelPlaying()) {
            log::warn("Ranked audio: owned channel stopped unexpectedly; restarting key='{}' song={}", resource->key, resource->songId);
            if (m_playbackRetryCount < kMaxPlaybackRetries) {
                ++m_playbackRetryCount;
                m_audioStartBeganAt = now;
                startDesiredAudio(true);
            } else failDesiredAudio("channel-retries-exhausted");
        }
    }
}

} // namespace corum::ranked
