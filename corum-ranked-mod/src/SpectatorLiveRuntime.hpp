#pragma once

#include "domain/SpectatorRemotePolicy.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {

enum class SpectatorLiveStatus {
    Idle,
    Waiting,
    Live,
    Fallback,
};

class SpectatorLiveRuntime final {
public:
    static SpectatorLiveRuntime& get();

    void tickPublisher(SpectatorGhostPose const& pose, bool practice);
    void publishTerminal(SpectatorGhostPose const& pose, bool practice);
    void tickTerminal();
    void localAttemptStarted() { m_deferredTerminalPose.reset(); }
    void tickViewer();
    void reset();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    // Server-approved scoped context/HTTP route only; no replay frames accepted.
    void startDebugViewer(std::string contextId, std::string token, std::string path);
    void stopDebugViewer();
#endif

    [[nodiscard]] SpectatorLiveStatus status() const;
    [[nodiscard]] std::string const& reason() const;
    [[nodiscard]] std::string const& opponentName() const;
    [[nodiscard]] std::optional<SpectatorGhostPose> interpolatedPose() const;
    [[nodiscard]] std::optional<SpectatorRemoteFrame> presentationFrame() const;

private:
    SpectatorLiveRuntime() = default;

    void resetPublisher();
    void resetViewer();
    void startPublisherGeneration(std::string const& matchId, std::string const& attemptId);
    void publish(SpectatorGhostPose const& pose);
    void pollViewer();
    void applyViewerResponse(matjson::Value const& root);
    void setFallback(std::string reason);
    [[nodiscard]] std::string endpoint(std::string const& path) const;

    std::string m_publishMatchId;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    std::string m_debugContextId, m_debugToken, m_debugPath;
#endif
    std::string m_publishAttemptId;
    std::uint64_t m_publishGeneration = 0;
    std::uint64_t m_publishSequence = 0;
    std::chrono::steady_clock::time_point m_publishStartedAt {};
    std::chrono::steady_clock::time_point m_nextPublishAt {};
    std::uint64_t m_publishRequestGeneration = 0;
    bool m_publishBusy = false;
    matjson::Value m_terminalBody;
    std::string m_terminalSessionToken, m_terminalMatchToken, m_terminalPublishAttemptId;
    std::chrono::steady_clock::time_point m_terminalDeadline {}, m_nextTerminalAt {};
    std::uint64_t m_terminalRequestGeneration = 0;
    bool m_terminalPending = false, m_terminalBusy = false;
    std::optional<SpectatorGhostPose> m_deferredTerminalPose;
    std::string m_deferredMatchId;
    int m_deferredRound = 0, m_deferredDeathmatch = 0;

    std::string m_viewMatchId;
    std::string m_viewContextKey;
    std::string m_viewAttemptId;
    std::string m_viewTerminalAttemptId;
    std::uint64_t m_viewGeneration = 0;
    std::uint64_t m_viewPresentationEpoch = 0;
    std::int64_t m_lastViewSequence = -1;
    std::string m_opponentName;
    std::string m_reason;
    SpectatorLiveStatus m_status = SpectatorLiveStatus::Idle;
    std::optional<SpectatorGhostPose> m_previousPose;
    std::optional<SpectatorGhostPose> m_currentPose;
    std::chrono::steady_clock::time_point m_poseReceivedAt {};
    std::chrono::steady_clock::time_point m_lastPacketAt {};
    std::chrono::steady_clock::time_point m_nextViewerPollAt {};
    std::uint64_t m_viewRequestGeneration = 0;
    std::int64_t m_lastViewerRttMs = 0;
    int m_consecutiveViewerErrors = 0;
    bool m_viewBusy = false;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_publishRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_terminalRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_viewRequest;
};

} // namespace corum::ranked
