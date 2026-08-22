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
constexpr auto kPlaybackVerifyDelay = std::chrono::milliseconds(1000);
constexpr int kMaxPlaybackRetries = 2;
constexpr float kRankedRelativeVolume = 0.80f;
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

    cancelActiveDownload();
    stopOwnedAudio();
    m_config = config;
    m_configSignature = signature;
    migratePrivateCacheToGdPath();
    m_downloadQueue.clear();
    m_failedSongIds.clear();
    m_downloadState = resourcesReady() ? RankedResourceDownloadState::Ready : RankedResourceDownloadState::Idle;

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

std::filesystem::path RankedAudioManager::preferredDownloadPath(int songId) const {
    if (songId > 0) {
        if (auto* manager = MusicDownloadManager::sharedState()) {
            auto const gdPath = manager->pathForSong(songId);
            if (!gdPath.empty()) return std::filesystem::path(gdPath.c_str());
        }
    }
    return cachedSongPath(songId);
}

std::string RankedAudioManager::readySongPath(int songId) const {
    if (songId <= 0) return {};
    if (auto* manager = MusicDownloadManager::sharedState()) {
        auto const path = manager->pathForSong(songId);
        if (!path.empty()) {
            auto const fsPath = std::filesystem::path(path.c_str());
            // Trust built-in resource songs, and also accept a valid on-disk song
            // even if GD's in-memory "downloaded" state has not refreshed yet.
            if (manager->isResourceSong(songId) || manager->isSongDownloaded(songId) || usableFile(fsPath)) {
                return path;
            }
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

    if (mode == m_desiredMode) {
        if (m_switchPending || m_ownedFadeOutPending || m_playbackVerifyPending) return;
        if (!desired && !m_ownsMusicChannel) return;
        if (desired && desiredKey == m_playingKey && ownedChannelPlaying()) return;
    }

    m_desiredMode = mode;
    requestAudioSwitch();
}

bool RankedAudioManager::ownedChannelPlaying() const {
    if (!m_rankedChannel) return false;
    bool playing = false;
    bool paused = false;
    bool muted = false;
    if (m_rankedChannel->isPlaying(&playing) != FMOD_OK || !playing) return false;
    if (m_rankedChannel->getPaused(&paused) != FMOD_OK || paused) return false;
    if (m_rankedChannel->getMute(&muted) != FMOD_OK || muted) return false;
    return true;
}

float RankedAudioManager::targetRankedVolume() const {
    auto* engine = FMODAudioEngine::sharedEngine();
    auto const gdMusicVolume = engine ? std::clamp(engine->m_musicVolume, 0.0f, 1.0f) : 1.0f;
    return gdMusicVolume * kRankedRelativeVolume;
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
    if (m_rankedChannel) {
        m_rankedChannel->stop();
        m_rankedChannel = nullptr;
    }
    if (m_rankedSound) {
        m_rankedSound->release();
        m_rankedSound = nullptr;
    }
    m_ownsMusicChannel = false;
    m_playingKey.clear();
    m_playbackVerifyPending = false;
    m_playbackVerifyAt = {};
    m_pendingStartMs = 0;
    m_playbackVerifyStartPositionMs = 0;
    m_ownedFadeInPending = false;
    m_ownedFadeOutPending = false;
    m_ownedFadeStartedAt = {};
    m_ownedFadeDurationSeconds = 0.0;
    // Avoid touching the Geometry Dash audio singleton during teardown.
    m_ownedFadeStartVolume = kRankedRelativeVolume;
}

void RankedAudioManager::clearPlaybackState() {
    stopOwnedAudio();
}

void RankedAudioManager::migratePrivateCacheToGdPath() {
    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) return;

    for (auto const songId : uniqueSongIds()) {
        auto const privatePath = cachedSongPath(songId);
        if (!usableFile(privatePath)) continue;

        auto const gdRawPath = manager->pathForSong(songId);
        if (gdRawPath.empty()) continue;
        auto const gdPath = std::filesystem::path(gdRawPath.c_str());
        if (usableFile(gdPath)) continue;

        std::error_code ec;
        if (!gdPath.parent_path().empty()) {
            std::filesystem::create_directories(gdPath.parent_path(), ec);
            if (ec) {
                log::warn("Ranked audio: failed to create GD song directory for {}: {}", songId, ec.message());
                continue;
            }
        }
        std::filesystem::copy_file(privatePath, gdPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec || !usableFile(gdPath)) {
            log::warn("Ranked audio: failed to migrate private song {} into GD song storage", songId);
            continue;
        }
        std::filesystem::remove(privatePath, ec);
        log::info("Ranked audio: migrated song {} into GD song storage", songId);
    }
}

void RankedAudioManager::beginOwnedFadeOut(double seconds) {
    if (!m_rankedChannel || !ownedChannelPlaying() || seconds <= 0.0) {
        stopOwnedAudio();
        return;
    }
    float currentVolume = targetRankedVolume();
    if (m_rankedChannel->getVolume(&currentVolume) != FMOD_OK) currentVolume = targetRankedVolume();
    m_ownedFadeStartVolume = std::clamp(currentVolume, 0.0f, 1.0f);
    m_ownedFadeDurationSeconds = seconds;
    m_ownedFadeStartedAt = std::chrono::steady_clock::now();
    m_ownedFadeOutPending = true;
    m_ownedFadeInPending = false;
}

void RankedAudioManager::requestAudioSwitch() {
    auto const* desired = resourceForMode(m_desiredMode);
    auto const desiredKey = desired ? desired->key : std::string();
    if (desired && m_ownsMusicChannel && desiredKey == m_playingKey && ownedChannelPlaying()) {
        m_switchPending = false;
        return;
    }

    auto const fadeOut = std::max(0.0, m_config.audio.fadeOutSeconds);
    m_playbackVerifyPending = false;
    m_playbackRetryCount = 0;

    if (m_rankedChannel) {
        m_switchPending = true;
        beginOwnedFadeOut(fadeOut);
        if (!m_ownedFadeOutPending) {
            m_switchPending = false;
            if (desired) startDesiredAudio();
        }
        return;
    }

    // No Ranked-owned channel exists, so this is normally the vanilla GD menu
    // track. Let GD fade it, then hard-stop it immediately before starting the
    // independent Ranked FMOD stream.
    if (auto* engine = FMODAudioEngine::sharedEngine(); engine && desired && fadeOut > 0.0) {
        engine->fadeOutMusic(static_cast<float>(fadeOut), 0);
        m_switchPending = true;
        m_switchAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(
            static_cast<int>(std::round(fadeOut * 1000.0))
        );
        return;
    }

    m_switchPending = false;
    if (desired) startDesiredAudio();
}

void RankedAudioManager::startDesiredAudio(bool retry) {
    m_switchPending = false;
    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || !songReady(resource->songId)) {
        stopOwnedAudio();
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        stopOwnedAudio();
        return;
    }

    auto const path = readySongPath(resource->songId);
    if (path.empty()) {
        stopOwnedAudio();
        return;
    }

    // Ranked owns its FMOD Sound/Channel directly. This deliberately bypasses
    // FMODAudioEngine::playMusic(), whose internal music-slot/queue state can
    // occasionally reject or immediately silence an otherwise valid file.
    stopOwnedAudio();
    engine->stopAllMusic(true);
    ensureFmodOutputActive();

    FMOD_MODE mode = FMOD_CREATESTREAM | FMOD_2D;
    mode |= resource->loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;

    FMOD::Sound* sound = nullptr;
    auto result = engine->m_system->createSound(path.c_str(), mode, nullptr, &sound);
    if (result != FMOD_OK || !sound) {
        log::error(
            "Ranked audio: createSound failed key='{}' song={} result={} path='{}'",
            resource->key,
            resource->songId,
            static_cast<int>(result),
            path
        );
        stopOwnedAudio();
        return;
    }
    if (resource->loop) sound->setLoopCount(-1);

    FMOD::Channel* channel = nullptr;
    result = engine->m_system->playSound(sound, nullptr, true, &channel);
    if (result != FMOD_OK || !channel) {
        log::error(
            "Ranked audio: playSound failed key='{}' song={} result={} path='{}'",
            resource->key,
            resource->songId,
            static_cast<int>(result),
            path
        );
        sound->release();
        return;
    }

    auto const startMs = static_cast<unsigned int>(std::max(0.0, resource->startSeconds) * 1000.0);
    if (startMs > 0) {
        auto const seekResult = channel->setPosition(startMs, FMOD_TIMEUNIT_MS);
        if (seekResult != FMOD_OK) {
            log::warn("Ranked audio: seek failed song={} result={}", resource->songId, static_cast<int>(seekResult));
        }
    }

    auto const fadeIn = retry ? 0.0 : std::max(0.0, m_config.audio.fadeInSeconds);
    channel->setVolume(fadeIn > 0.0 ? 0.0f : targetRankedVolume());
    channel->setMute(false);
    channel->setPaused(false);
    m_playbackVerifyStartPositionMs = startMs;
    engine->m_system->update();

    m_rankedSound = sound;
    m_rankedChannel = channel;
    m_playingKey = resource->key;
    m_ownsMusicChannel = true;
    m_playbackVerifyPending = true;
    m_playbackVerifyAt = std::chrono::steady_clock::now() + kPlaybackVerifyDelay;
    m_ownedFadeInPending = fadeIn > 0.0;
    if (m_ownedFadeInPending) {
        m_ownedFadeStartedAt = std::chrono::steady_clock::now();
        m_ownedFadeDurationSeconds = fadeIn;
    }

    log::info(
        "Ranked audio: direct FMOD play key='{}' song={} path='{}' loop={} relativeVolume={} retry={}",
        resource->key,
        resource->songId,
        path,
        resource->loop,
        targetRankedVolume(),
        retry
    );
}

void RankedAudioManager::verifyDesiredAudio() {
    if (!m_playbackVerifyPending) return;
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
        startDesiredAudio(true);
        return;
    }

    log::error(
        "Ranked audio: direct playback failed after {} retries for key='{}' song={}",
        kMaxPlaybackRetries,
        resource->key,
        resource->songId
    );
    stopOwnedAudio();
}

void RankedAudioManager::updateOwnedFades(std::chrono::steady_clock::time_point now) {
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
        m_rankedChannel->setVolume(targetRankedVolume() * static_cast<float>(t));
        if (t >= 1.0) m_ownedFadeInPending = false;
    }
}

void RankedAudioManager::fadeOutForGameplay() {
    // The Ranked layer stops ticking as soon as GD pushes the LevelInfo/Play
    // scene, so a tick-driven fade could otherwise strand the custom channel in
    // the gameplay scene. Stop it deterministically before handing audio back to GD.
    m_desiredMode = RankedAudioMode::Silent;
    m_switchPending = false;
    stopOwnedAudio();
}

void RankedAudioManager::onApplicationPause() {
    // Ranked owns this FMOD channel outside Geometry Dash's normal music-slot
    // registry, so pause it explicitly when the app audio session is suspended.
    // This prevents the direct stream from escaping GD's normal background-audio
    // lifecycle on platforms where pauseSound() only visits registered channels.
    if (m_rankedChannel) m_rankedChannel->setPaused(true);
}

void RankedAudioManager::onApplicationResume() {
    // iOS/iPadOS may leave an FMOD channel paused after returning from the
    // background even though the Sound itself is still valid. Defer recovery to
    // the next Ranked tick, after Geometry Dash has completed resumeSound().
    m_resumeRecoveryRequested = true;
}

void RankedAudioManager::restoreMenuMusic() {
    m_desiredMode = RankedAudioMode::Silent;
    m_switchPending = false;
    m_playbackRetryCount = 0;
    stopOwnedAudio();
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->stopAllMusic(true);
    if (auto* game = GameManager::sharedState()) game->playMenuMusic();
}

void RankedAudioManager::tick() {
    auto const now = std::chrono::steady_clock::now();
    updateOwnedFades(now);

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

    if (m_resumeRecoveryRequested) {
        m_resumeRecoveryRequested = false;
        if (m_desiredMode != RankedAudioMode::Silent && resourceForMode(m_desiredMode)) {
            ensureFmodOutputActive();
            if (m_rankedChannel) {
                m_rankedChannel->setPaused(false);
                m_rankedChannel->setMute(false);
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
        m_rankedChannel->setMute(false);
        m_rankedChannel->setVolume(targetRankedVolume());
    }

    if (m_switchPending && !m_ownedFadeOutPending && now >= m_switchAt) startDesiredAudio();
    if (m_playbackVerifyPending && now >= m_playbackVerifyAt) verifyDesiredAudio();

    // If another scene/mod unexpectedly kills the direct Ranked channel, recover
    // looped menu/match/result BGM instead of remaining silently "owned" forever.
    if (m_ownsMusicChannel && !m_ownedFadeOutPending && !m_playbackVerifyPending) {
        if (auto const* resource = resourceForMode(m_desiredMode); resource && resource->loop && !ownedChannelPlaying()) {
            log::warn("Ranked audio: owned channel stopped unexpectedly; restarting key='{}' song={}", resource->key, resource->songId);
            startDesiredAudio(true);
        }
    }
}

} // namespace corum::ranked
