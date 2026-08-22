#include "RankedAudioManager.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>

using namespace geode::prelude;

namespace corum::ranked {
namespace {
constexpr auto kDownloadTimeout = std::chrono::seconds(60);

int clampProgress(int value) {
    return std::clamp(value, 0, 100);
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
    m_config = config;
    m_configSignature = signature;
    m_downloadQueue.clear();
    m_failedSongIds.clear();
    m_activeDownloadSongId = 0;
    m_downloadStartedAt = {};
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

bool RankedAudioManager::songReady(int songId) const {
    if (songId <= 0) return true;
    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) return false;
    return manager->isResourceSong(songId) || manager->isSongDownloaded(songId);
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
    if (m_activeDownloadSongId > 0) {
        if (auto* manager = MusicDownloadManager::sharedState()) {
            view.activeProgress = clampProgress(manager->getDownloadProgress(m_activeDownloadSongId));
        }
    }
    return view;
}

void RankedAudioManager::downloadAll() {
    if (!enabled()) {
        m_downloadState = RankedResourceDownloadState::Ready;
        return;
    }

    m_downloadQueue.clear();
    m_failedSongIds.clear();
    m_activeDownloadSongId = 0;
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

    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) {
        while (!m_downloadQueue.empty()) {
            m_failedSongIds.insert(m_downloadQueue.front());
            m_downloadQueue.pop_front();
        }
        m_downloadState = RankedResourceDownloadState::Failed;
        return;
    }

    m_activeDownloadSongId = m_downloadQueue.front();
    m_downloadQueue.pop_front();
    m_downloadStartedAt = std::chrono::steady_clock::now();
    manager->downloadSong(m_activeDownloadSongId);
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
    // First Ranked playback fades Geometry Dash's current menu track. Later
    // switches fade the Ranked-owned track on the same main music channel.
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
    auto* downloads = MusicDownloadManager::sharedState();
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!downloads || !engine) return;

    auto const path = downloads->pathForSong(resource->songId);
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

    if (m_activeDownloadSongId > 0) {
        if (songReady(m_activeDownloadSongId)) {
            m_activeDownloadSongId = 0;
            m_downloadStartedAt = {};
            startNextDownload();
        } else if (m_downloadStartedAt != std::chrono::steady_clock::time_point{} &&
                   now - m_downloadStartedAt >= kDownloadTimeout) {
            m_failedSongIds.insert(m_activeDownloadSongId);
            m_activeDownloadSongId = 0;
            m_downloadStartedAt = {};
            startNextDownload();
        }
    } else if (m_downloadState == RankedResourceDownloadState::Downloading) {
        startNextDownload();
    }

    if (resourcesReady() && m_downloadState != RankedResourceDownloadState::Ready) {
        m_downloadState = RankedResourceDownloadState::Ready;
        m_failedSongIds.clear();
    }

    if (m_switchPending && now >= m_switchAt) startDesiredAudio();
}

} // namespace corum::ranked
