#pragma once

#include "RankedRuntime.hpp"

#include <chrono>
#include <deque>
#include <set>
#include <string>
#include <vector>

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

private:
    RankedAudioManager() = default;

    [[nodiscard]] std::string configSignature(RankedClientPresentationView const& config) const;
    [[nodiscard]] bool songReady(int songId) const;
    [[nodiscard]] std::vector<int> uniqueSongIds() const;
    [[nodiscard]] RankedAudioResourceView const* findResource(std::string const& key) const;
    [[nodiscard]] RankedAudioResourceView const* resourceForMode(RankedAudioMode mode) const;
    void startNextDownload();
    void requestAudioSwitch();
    void startDesiredAudio();

    RankedClientPresentationView m_config;
    std::string m_configSignature;
    std::deque<int> m_downloadQueue;
    std::set<int> m_failedSongIds;
    int m_activeDownloadSongId = 0;
    std::chrono::steady_clock::time_point m_downloadStartedAt {};
    RankedResourceDownloadState m_downloadState = RankedResourceDownloadState::Idle;

    RankedAudioMode m_desiredMode = RankedAudioMode::Silent;
    std::string m_playingKey;
    bool m_ownsMusicChannel = false;
    bool m_switchPending = false;
    std::chrono::steady_clock::time_point m_switchAt {};
};

} // namespace corum::ranked
