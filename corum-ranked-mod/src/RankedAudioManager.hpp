#pragma once

#include "RankedRuntime.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace FMOD {
class Sound;
class Channel;
}

namespace corum::ranked {

enum class RankedAudioMode {
    Silent,
    Menu,
    Match,
    ResultWin,
    ResultLose,
};

enum class RankedResourceDownloadState {
    Idle,
    Downloading,
    Ready,
    Failed,
};

struct RankedResourceDownloadView {
    RankedResourceDownloadState state = RankedResourceDownloadState::Idle;
    int total = 0;
    int ready = 0;
    int failed = 0;
    int activeSongId = 0;
    int activeProgress = 0;
};

class RankedAudioManager final {
public:
    static RankedAudioManager& get();

    void configure(RankedClientPresentationView const& config);
    void tick();

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool resourcesReady() const;
    [[nodiscard]] bool requiresResourceDownload() const;
    [[nodiscard]] RankedResourceDownloadView downloadView() const;
    void downloadAll();
    void retryFailed();

    void setMode(RankedAudioMode mode);
    void fadeOutForGameplay();
    void restoreMenuMusic();
    void onApplicationPause();
    void onApplicationResume();

private:
    RankedAudioManager() = default;

    [[nodiscard]] std::string configSignature(RankedClientPresentationView const& config) const;
    [[nodiscard]] bool songReady(int songId) const;
    [[nodiscard]] bool gdSongReady(int songId) const;
    [[nodiscard]] std::filesystem::path cachedSongPath(int songId) const;
    [[nodiscard]] std::filesystem::path preferredDownloadPath(int songId) const;
    [[nodiscard]] std::string readySongPath(int songId) const;
    [[nodiscard]] std::vector<int> uniqueSongIds() const;
    [[nodiscard]] RankedAudioResourceView const* findResource(std::string const& key) const;
    [[nodiscard]] RankedAudioResourceView const* resourceForMode(RankedAudioMode mode) const;
    void startNextDownload();
    void fetchSongInfo(int songId);
    void downloadSongFile(int songId, std::string url);
    void finishDownloadSuccess(int songId);
    void finishDownloadFailure(int songId, std::string const& reason);
    void cancelActiveDownload();
    void requestAudioSwitch();
    void startDesiredAudio(bool retry = false);
    void verifyDesiredAudio();
    void clearPlaybackState();
    void migratePrivateCacheToGdPath();
    void stopOwnedAudio();
    [[nodiscard]] bool ownedChannelPlaying() const;
    [[nodiscard]] float targetRankedVolume() const;
    void ensureFmodOutputActive() const;
    void beginOwnedFadeOut(double seconds);
    void updateOwnedFades(std::chrono::steady_clock::time_point now);

    RankedClientPresentationView m_config;
    std::string m_configSignature;
    std::deque<int> m_downloadQueue;
    std::set<int> m_failedSongIds;
    int m_activeDownloadSongId = 0;
    std::chrono::steady_clock::time_point m_downloadStartedAt {};
    int m_activeDownloadProgress = 0;
    std::uint64_t m_downloadGeneration = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_songInfoRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_songFileRequest;
    RankedResourceDownloadState m_downloadState = RankedResourceDownloadState::Idle;

    RankedAudioMode m_desiredMode = RankedAudioMode::Silent;
    std::string m_playingKey;
    bool m_ownsMusicChannel = false;
    bool m_switchPending = false;
    std::chrono::steady_clock::time_point m_switchAt {};
    bool m_playbackVerifyPending = false;
    std::chrono::steady_clock::time_point m_playbackVerifyAt {};
    int m_playbackRetryCount = 0;
    unsigned int m_pendingStartMs = 0;
    unsigned int m_playbackVerifyStartPositionMs = 0;

    FMOD::Sound* m_rankedSound = nullptr;
    FMOD::Channel* m_rankedChannel = nullptr;
    bool m_ownedFadeInPending = false;
    bool m_ownedFadeOutPending = false;
    std::chrono::steady_clock::time_point m_ownedFadeStartedAt {};
    double m_ownedFadeDurationSeconds = 0.0;
    float m_ownedFadeStartVolume = 0.8f;
    bool m_resumeRecoveryRequested = false;
};

} // namespace corum::ranked
