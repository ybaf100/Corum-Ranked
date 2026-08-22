#include "RankedAudioManager.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <system_error>

using namespace geode::prelude;

namespace corum::ranked {
namespace {
constexpr auto kSongInfoTimeout = std::chrono::seconds(20);
constexpr auto kSongFileTimeout = std::chrono::seconds(90);
constexpr auto kDownloadGuardTimeout = std::chrono::seconds(105);
constexpr std::uintmax_t kMinimumCachedSongBytes = 1024;
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

bool usableFile(std::filesystem::path const& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec &&
        std::filesystem::file_size(path, ec) >= kMinimumCachedSongBytes && !ec;
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

void RankedAudioManager::requestAudioSwitch() {
    auto const* desired = resourceForMode(m_desiredMode);
    auto const desiredKey = desired ? desired->key : std::string();
    if (desired && m_ownsMusicChannel && desiredKey == m_playingKey) {
        m_switchPending = false;
        return;
    }

    auto* engine = FMODAudioEngine::sharedEngine();
    auto const fadeOut = static_cast<float>(std::max(0.0, m_config.audio.fadeOutSeconds));
    if (engine && (desired || m_ownsMusicChannel)) engine->fadeOutMusic(fadeOut, 0);

    m_ownsMusicChannel = false;
    m_playingKey.clear();
    if (!desired) {
        m_switchPending = false;
        return;
    }

    m_switchPending = true;
    m_switchAt = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(std::round(std::max(0.0, m_config.audio.fadeOutSeconds) * 1000.0)));
    if (m_config.audio.fadeOutSeconds <= 0.0) startDesiredAudio();
}

void RankedAudioManager::startDesiredAudio() {
    m_switchPending = false;
    auto const* resource = resourceForMode(m_desiredMode);
    if (!resource || !songReady(resource->songId)) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    auto const path = readySongPath(resource->songId);
    if (path.empty()) return;
    engine->playMusic(
        path,
        resource->loop,
        static_cast<float>(std::max(0.0, m_config.audio.fadeInSeconds)),
        0
    );
    auto const startMs = static_cast<unsigned int>(std::max(0.0, resource->startSeconds) * 1000.0);
    if (startMs > 0) engine->setMusicTimeMS(startMs, true, 0);
    m_playingKey = resource->key;
    m_ownsMusicChannel = true;
}

void RankedAudioManager::fadeOutForGameplay() {
    m_desiredMode = RankedAudioMode::Silent;
    requestAudioSwitch();
}

void RankedAudioManager::restoreMenuMusic() {
    if (auto* engine = FMODAudioEngine::sharedEngine()) {
        if (m_ownsMusicChannel || m_switchPending) {
            engine->fadeOutMusic(static_cast<float>(std::max(0.0, m_config.audio.fadeOutSeconds)), 0);
        }
    }
    m_desiredMode = RankedAudioMode::Silent;
    m_switchPending = false;
    m_ownsMusicChannel = false;
    m_playingKey.clear();
    if (auto* game = GameManager::sharedState()) game->fadeInMenuMusic();
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
}

} // namespace corum::ranked
