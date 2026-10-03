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
    void tickViewer();
    void reset();

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
    std::string m_publishAttemptId;
    std::uint64_t m_publishGeneration = 0;
    std::uint64_t m_publishSequence = 0;
    std::chrono::steady_clock::time_point m_publishStartedAt {};
    std::chrono::steady_clock::time_point m_nextPublishAt {};
    std::uint64_t m_publishRequestGeneration = 0;
    bool m_publishBusy = false;

    std::string m_viewMatchId;
    std::string m_viewContextKey;
    std::string m_viewAttemptId;
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
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_viewRequest;
};

} // namespace corum::ranked
