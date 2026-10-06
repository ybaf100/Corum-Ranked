#pragma once
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "DebugSpectatePolicy.hpp"
#include "../domain/LevelPreparationPolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>

namespace corum::ranked::debug {
class DebugSpectateRuntime final {
public:
    static DebugSpectateRuntime& get();
    void start(bool playback, std::string password);
    void refreshLiveSetting();
    void toggleLiveSetting(std::string password);
    std::optional<bool> liveEnabled() const { return m_liveEnabled; }
    bool liveSettingBusy() const { return m_settingBusy; }
    void tick();
    void sceneEntered(int levelId);
    bool recordPose(SpectatorGhostPose const& pose, bool terminal = false);
    void cancelRecording();
    void exitScene();
    void cancelPreparation(char const* reason = "explicit cancel/back");
    void cancelPopupPreparation();
    bool recordLevel(int levelId) const;
    bool playbackLevel(int levelId) const;
    bool preparedLevel(int levelId) const;
    LevelPreparationContext preparationContext() const;
    std::string const& contextId() const { return m_contextId; }
    std::string const& status() const { return m_status; }
    DebugSpectateMode mode() const { return m_mode; }
    bool playbackComplete() const { return m_playbackComplete; }
    void viewerResponse(bool complete, bool expired);
private:
    DebugSpectateRuntime() = default;
    void pumpRecording();
    void closePlayback();
    void fail(std::string message);
    DebugSpectateMode m_mode = DebugSpectateMode::None;
    std::string m_id, m_token, m_contextId, m_status;
    DebugSpectateCapture m_capture;
    std::optional<SpectatorGhostPose> m_previousCapturedPose;
    std::chrono::steady_clock::time_point m_recordStartedAt {}, m_nextUploadAt {}, m_expireAt {}, m_prepareStartedAt {};
    std::uint64_t m_generation = 0;
    int m_errors = 0;
    bool m_busy = false, m_launched = false, m_playbackComplete = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_cleanupRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_settingRequest;
    std::optional<bool> m_liveEnabled;
    bool m_settingBusy = false;
};
void showDebugSpectatePopup();
} // namespace corum::ranked::debug
#endif
