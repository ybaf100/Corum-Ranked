#include "RankedAudioManager.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <initializer_list>
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
constexpr std::uintmax_t kMinimumCachedSongBytes = 1024;
constexpr auto kPlaybackVerifyDelay = std::chrono::milliseconds(700);
constexpr int kMaxPlaybackRetries = 2;
constexpr char kSongInfoEndpoint[] = "https://www.boomlings.com/database/getGJSongInfo.php";
constexpr char kGdSecret[] = "Wmfd2893gb7";

int clampProgress(int value) {
    return std::clamp(value, 0, 100);
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
    if (signature == m_configSignature) return;

    auto const wasOwningMusic = m_ownsMusicChannel;
    cancelActiveDownload();
    m_config = config;
    m_configSignature = signature;
    m_downloadQueue.clear();
    m_failedSongIds.clear();
    m_downloadState = resourcesReady() ? RankedResourceDownloadState::Ready : RankedResourceDownloadState::Idle;

    if (wasOwningMusic) {
        if (auto* engine = FMODAudioEngine::sharedEngine()) {
            engine->fadeOutMusic(static_cast<float>(std::max(0.0, m_config.audio.fadeOutSeconds)), 0);
        }
        m_ownsMusicChannel = false;
        m_playingKey.clear();
    }
    m_switchPending = false;
    m_playbackVerifyPending = false;
    m_playbackVerifyAt = {};
    m_playbackRetryCount = 0;
    m_pendingStartMs = 0;
    m_desiredMode = RankedAudioMode::Silent;
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

std::string RankedAudioManager::readySongPath(int songId) const {
    if (songId <= 0) return {};
    if (gdSongReady(songId)) {
        if (auto* manager = MusicDownloadManager::sharedState()) {
            auto const path = manager->pathForSong(songId);
            if (!path.empty()) return path;
        }
    }
    auto const cached = cachedSongPath(songId);
    if (usableFile(cached)) return cached.string();
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
        return;
    }

    m_activeDownloadSongId = m_downloadQueue.front();
    m_downloadQueue.pop_front();
    m_activeDownloadProgress = 0;
    m_downloadStartedAt = std::chrono::steady_clock::now();
    fetchSongInfo(m_activeDownloadSongId);
}

void RankedAudioManager::fetchSongInfo(int songId) {
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

    log::info("Ranked audio: downloading GD song {} to private cache", songId);
    m_songFileRequest.spawn(
        request.get(std::move(url)),
        [this, generation, songId](web::WebResponse response) {
            if (generation != m_downloadGeneration || songId != m_activeDownloadSongId) return;
            if (!response.ok()) {
                finishDownloadFailure(songId, fmt::format("audio HTTP {}", response.code()));
                return;
            }

            auto const target = cachedSongPath(songId);
            if (target.empty()) {
                finishDownloadFailure(songId, "cache path unavailable");
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                finishDownloadFailure(songId, "failed to create audio cache directory");
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
    log::info("Ranked audio: GD song {} cached successfully", songId);
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
        case RankedAudioMode::Menu: return first({"menu"});
        case RankedAudioMode::Match: return first({"match", "menu"});
        case RankedAudioMode::ResultWin: return first({"result_win", "match", "menu"});
        case RankedAudioMode::ResultLose: return first({"result_lose", "match", "menu"});
        case RankedAudioMode::Silent: break;
    }
    return nullptr;
}

void RankedAudioManager::setMode(RankedAudioMode mode) {
    if (!enabled() || !resourcesReady()) mode = RankedAudioMode::Silent;
    auto const* desired = resourceForMode(mode);
    auto const desiredKey = desired ? desired->key : std::string();
    if (mode == m_desiredMode && ((!desired && !m_ownsMusicChannel) || (desired && desiredKey == m_playingKey))) {
        return;
    }
    m_desiredMode = mode;
    requestAudioSwitch();
}

void RankedAudioManager::clearPlaybackState() {
    m_ownsMusicChannel = false;
    m_playingKey.clear();
    m_playbackVerifyPending = false;
    m_playbackVerifyAt = {};
    m_pendingStartMs = 0;
}

void RankedAudioManager::requestAudioSwitch() {
    auto const* desired = resourceForMode(m_desiredMode);
    auto const desiredKey = desired ? desired->key : std::string();
    if (desired && m_ownsMusicChannel && desiredKey == m_playingKey) {
        m_switchPending = false;
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    auto const fadeOut = static_cast<float>(std::max(0.0, m_config.audio.fadeOutSeconds));

    // fadeOutMusic only fades the currently registered music channel; it does not
    // guarantee that the channel has been removed by the time playMusic is called.
    // Keep the visual fade here, then startDesiredAudio() hard-stops the old music
    // before installing the new Ranked stream. This mirrors the stable pattern used
    // by GD/Geode custom-music screens and avoids a permanently silent channel.
    if (engine && (desired || m_ownsMusicChannel)) engine->fadeOutMusic(fadeOut, 0);

    clearPlaybackState();
    m_playbackRetryCount = 0;
    if (!desired) {
        m_switchPending = false;
        return;
    }

    m_switchPending = true;
    m_switchAt = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(std::round(std::max(0.0, m_config.audio.fadeOutSeconds) * 1000.0)));
    if (m_config.audio.fadeOutSeconds <= 0.0) startDesiredAudio();
}

void RankedAudioManager::startDesiredAudio(bool retry) {
    m_switchPending = false;
    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || !songReady(resource->songId)) {
        clearPlaybackState();
        return;
    }
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) {
        clearPlaybackState();
        return;
    }

    auto const path = readySongPath(resource->songId);
    if (path.empty()) {
        clearPlaybackState();
        return;
    }

    // IMPORTANT: playMusic can fail to take ownership while the previous GD menu
    // track is still registered/fading. Fully remove it first. The downloaded
    // Ranked file itself remains on disk and is not affected by stopAllMusic().
    engine->stopAllMusic(true);
    if (engine->m_backgroundMusicChannel) {
        // Scene transitions and third-party audio hooks can leave the main music
        // group paused. A successful playMusic call on a paused group is silent.
        engine->m_backgroundMusicChannel->setPaused(false);
        // Respect the user's Geometry Dash music-volume setting while also
        // recovering from a transition that left the group itself at volume 0.
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
    }

    auto const fadeIn = retry
        ? 0.0f
        : static_cast<float>(std::max(0.0, m_config.audio.fadeInSeconds));
    log::info(
        "Ranked audio: play key='{}' song={} path='{}' loop={} fadeIn={} retry={}",
        resource->key,
        resource->songId,
        path,
        resource->loop,
        fadeIn,
        retry
    );
    engine->playMusic(path, resource->loop, fadeIn, 0);

    // playMusic may create/load the stream asynchronously. Seek only after the
    // main music channel is confirmed active; doing it immediately can target the
    // previous/nonexistent channel on some platforms.
    m_pendingStartMs = static_cast<unsigned int>(std::max(0.0, resource->startSeconds) * 1000.0);

    m_playingKey = resource->key;
    m_ownsMusicChannel = true;
    m_playbackVerifyPending = true;
    m_playbackVerifyAt = std::chrono::steady_clock::now() + kPlaybackVerifyDelay;
}

void RankedAudioManager::verifyDesiredAudio() {
    if (!m_playbackVerifyPending) return;
    m_playbackVerifyPending = false;

    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || resource->key != m_playingKey) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) {
        clearPlaybackState();
        return;
    }
    auto const path = readySongPath(resource->songId);
    if (path.empty()) {
        clearPlaybackState();
        return;
    }

    bool isPlaying = false;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
        channel->isPlaying(&isPlaying);
    }
    if (isPlaying) {
        if (m_pendingStartMs > 0) {
            engine->setMusicTimeMS(m_pendingStartMs, true, 0);
            m_pendingStartMs = 0;
        }
        log::info("Ranked audio: playback verified for key='{}' song={}", resource->key, resource->songId);
        m_playbackRetryCount = 0;
        return;
    }

    if (m_playbackRetryCount < kMaxPlaybackRetries) {
        ++m_playbackRetryCount;
        log::warn(
            "Ranked audio: playback did not become active for key='{}' song={}; retry {}/{}",
            resource->key,
            resource->songId,
            m_playbackRetryCount,
            kMaxPlaybackRetries
        );
        startDesiredAudio(true);
        return;
    }

    log::error(
        "Ranked audio: playback failed after {} retries for key='{}' song={} path='{}'",
        kMaxPlaybackRetries,
        resource->key,
        resource->songId,
        path
    );
    // Do not pretend that audio is playing. Clearing this state allows a later
    // mode sync (or scene transition) to attempt playback again instead of being
    // permanently stuck in the silent 'already playing' state.
    clearPlaybackState();
}

void RankedAudioManager::fadeOutForGameplay() {
    m_desiredMode = RankedAudioMode::Silent;
    requestAudioSwitch();
}

void RankedAudioManager::restoreMenuMusic() {
    if (auto* engine = FMODAudioEngine::sharedEngine()) {
        // Ranked can leave the main music slot in a faded/stopped transitional
        // state. Tear it down before asking GameManager to recreate menu music.
        engine->stopAllMusic(true);
    }
    m_desiredMode = RankedAudioMode::Silent;
    m_switchPending = false;
    m_playbackRetryCount = 0;
    clearPlaybackState();
    if (auto* game = GameManager::sharedState()) game->playMenuMusic();
}

void RankedAudioManager::tick() {
    auto const now = std::chrono::steady_clock::now();

    if (m_activeDownloadSongId > 0 &&
        m_downloadStartedAt != std::chrono::steady_clock::time_point{} &&
        now - m_downloadStartedAt >= kDownloadGuardTimeout) {
        auto const songId = m_activeDownloadSongId;
        m_songInfoRequest.cancel();
        m_songFileRequest.cancel();
        finishDownloadFailure(songId, "download timed out");
    } else if (m_activeDownloadSongId == 0 && m_downloadState == RankedResourceDownloadState::Downloading) {
        startNextDownload();
    }

    if (resourcesReady() && m_downloadState != RankedResourceDownloadState::Ready) {
        m_downloadState = RankedResourceDownloadState::Ready;
        m_failedSongIds.clear();
    }

    if (m_switchPending && now >= m_switchAt) startDesiredAudio();
    if (m_playbackVerifyPending && now >= m_playbackVerifyAt) verifyDesiredAudio();
}

} // namespace corum::ranked
