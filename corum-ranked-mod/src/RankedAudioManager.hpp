#pragma once

#include "RankedRuntime.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace FMOD {
class Sound;
class Channel;
class ChannelGroup;
class System;
}

namespace corum::ranked {

enum class RankedAudioMode {
    Silent,
    Lobby,
    LobbyDemotionWarning,
    LobbyPromotion,
    Round12,
    Round3DeathmatchWait,
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
    void refreshVolume();

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
    void updateLibraryDownload();
    void downloadSongFile(int songId, std::string url);
    void finishDownloadSuccess(int songId);
    void finishDownloadFailure(int songId, std::string const& reason);
    void cancelActiveDownload();
    void requestAudioSwitch();
    [[nodiscard]] RankedAudioMode availableMode(RankedAudioMode requested) const;
    [[nodiscard]] bool audioStartTimedOut() const;
    void failDesiredAudio(char const* phase, int result = 0);
    void retireOwnedChannel(double seconds);
    void startDesiredAudio(bool retry = false);
    void updatePendingAudioStart();
    void cancelPendingAudioStart();
    void verifyDesiredAudio();
    void clearPlaybackState();
    void synchronizeSongCaches();
    void prewarmReadySounds();
    void prewarmReadyChannels();
    void stopPreparedChannels();
    [[nodiscard]] FMOD::Channel* preparedChannel(RankedAudioResourceView const& resource,
        FMOD::Sound* sound, int& result);
    void releasePreparedSounds();
    [[nodiscard]] FMOD::Sound* preparedSound(RankedAudioResourceView const& resource);
    void reapRetainedSounds();
    void updateStreamHealth();
    void stopOwnedAudio();
    [[nodiscard]] bool ownedChannelPlaying() const;
    [[nodiscard]] float targetRankedVolume() const;
    [[nodiscard]] FMOD::ChannelGroup* ownedMusicGroup();
    void releaseMusicGroup();
    void ensureFmodOutputActive() const;
    void beginOwnedFadeOut(double seconds);
    void updateOwnedFades(std::chrono::steady_clock::time_point now);

    RankedClientPresentationView m_config;
    FMOD::System* m_musicSystem = nullptr;
    FMOD::ChannelGroup* m_musicGroup = nullptr;
    float m_appliedMenuVolume = -1;
    std::string m_configSignature;
    std::deque<int> m_downloadQueue;
    std::set<int> m_failedSongIds;
    int m_activeDownloadSongId = 0;
    std::chrono::steady_clock::time_point m_downloadStartedAt {};
    int m_activeDownloadProgress = 0;
    std::uint64_t m_downloadGeneration = 0;
    // Observe the native downloader without replacing its delegates or
    // cancelling requests owned by GD/another mod when our config changes.
    bool m_libraryUrlRequested = false;
    bool m_librarySongRequested = false;
    std::chrono::steady_clock::time_point m_libraryQuietSince {};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_songInfoRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_songFileRequest;
    RankedResourceDownloadState m_downloadState = RankedResourceDownloadState::Idle;

    RankedAudioMode m_desiredMode = RankedAudioMode::Silent;
    // The UI repeats its requested mode every refresh. Keep failures scoped to
    // that request so fallback cannot be undone by the next identical refresh.
    RankedAudioMode m_requestedMode = RankedAudioMode::Silent;
    std::set<std::string> m_failedPlaybackKeys;
    std::chrono::steady_clock::time_point m_audioStartBeganAt {};
    std::string m_playingKey;
    bool m_ownsMusicChannel = false;
    bool m_switchPending = false;
    bool m_pendingPlaybackRetry = false;
    std::string m_pendingKey;
    std::string m_pendingPath;
    FMOD::Sound* m_pendingSound = nullptr;
    FMOD::Channel* m_pendingChannel = nullptr;
    std::chrono::steady_clock::time_point m_pendingStartedAt {};
    unsigned int m_pendingStartMs = 0;
    bool m_playbackVerifyPending = false;
    std::chrono::steady_clock::time_point m_playbackVerifyAt {};
    int m_playbackRetryCount = 0;
    unsigned int m_playbackVerifyStartPositionMs = 0;

    // Paths are validated once during resource preparation. Competitive mode
    // changes must not reopen every audio file through Android storage.
    mutable std::unordered_map<int, std::string> m_readySongPaths;

    // Prepared streaming Sounds are opened asynchronously while resources are
    // prepared, never synchronously in Round/Deathmatch/Result callbacks.
    // Each resource has its own initially-seeked, single-use stream. Replaying
    // the same streaming Sound resets it and discards its initial seek buffer.
    std::unordered_map<std::string, FMOD::Sound*> m_preparedSounds;
    struct PreparedChannel {
        FMOD::Channel* channel = nullptr;
        std::chrono::steady_clock::time_point beganAt {};
        bool seekSubmitted = false;
        bool failed = false;
        int result = 0;
    };
    // A Sound's initial seek is not a certificate of the paused Channel's
    // position. Prepare/repair that channel asynchronously while it is muted;
    // consume it once only, after its actual offset/buffer is ready.
    std::unordered_map<std::string, PreparedChannel> m_preparedChannels;
    std::set<std::string> m_consumedPreparedKeys;
    std::vector<FMOD::Sound*> m_retainedSounds;
    FMOD::Sound* m_rankedSound = nullptr;
    FMOD::Sound* m_retiringSound = nullptr;
    bool m_ownedStreamMuted = false;
    bool m_retiringStreamMuted = false;
    unsigned int m_streamDiagnostics = 0;
    std::chrono::steady_clock::time_point m_streamStarvedAt {};
    FMOD::Channel* m_rankedChannel = nullptr;
    FMOD::Channel* m_retiringChannel = nullptr;
    std::chrono::steady_clock::time_point m_retiringUntil {};
    bool m_ownedDspFadeIn = false;
    unsigned long long m_ownedFadeDspStart = 0;
    unsigned long long m_ownedFadeDspEnd = 0;
    bool m_ownedFadeInPending = false;
    bool m_ownedFadeOutPending = false;
    std::chrono::steady_clock::time_point m_ownedFadeStartedAt {};
    double m_ownedFadeDurationSeconds = 0.0;
    float m_ownedFadeStartVolume = 0.8f;
    bool m_resumeRecoveryRequested = false;
};

} // namespace corum::ranked
