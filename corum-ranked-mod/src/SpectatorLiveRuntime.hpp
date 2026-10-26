#pragma once
#include "domain/SpectatorPublishQueue.hpp"

#include "domain/SpectatorRemotePolicy.hpp"
#include "domain/SpectatorInterpolationBuffer.hpp"
#include "domain/SpectatorRestoreTimeline.hpp"

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
    Buffering,
    Fallback,
    Syncing,
    Reconnecting,
    PartialSync,
    Cleared,
    Disconnected,
};

class SpectatorLiveRuntime final {
public:
    static SpectatorLiveRuntime& get();

    bool tickPublisher(SpectatorGhostPose const& pose, bool practice);
    void publisherPaused(bool paused);
    void publishTerminal(SpectatorGhostPose const& pose, bool practice);
    void tickTerminal();
    void localAttemptStarted() { m_deferredTerminalPose.reset(); }
    void tickViewer();
    void reset();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    // Server-approved scoped context/HTTP route only; no replay frames accepted.
    void startDebugViewer(std::string contextId, std::string token, std::string path,bool restoreHistory=false);
    void stopDebugViewer();
#endif

    [[nodiscard]] SpectatorLiveStatus status() const;
    [[nodiscard]] std::string const& reason() const;
    [[nodiscard]] std::string const& opponentName() const;
    [[nodiscard]] std::optional<SpectatorGhostPose> interpolatedPose() const;
    [[nodiscard]] std::optional<SpectatorRemoteFrame> presentationFrame() const;
    void requestSceneRestore();
    std::vector<SpectatorRemoteFrame> takeRestorationFrames();
    void restorationApplied(SpectatorRemoteFrame const& frame,bool clockAdvanced,double workMs=0);
    void noteWorldPartial(std::string reason);

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
    int m_publishRound = 0, m_publishDeathmatch = 0, m_publishLevel=0;
    SpectatorPublishQueue m_publishQueue;
    std::chrono::steady_clock::time_point m_publisherPausedAt {};
    std::int64_t m_publisherPausedMs = 0;
    bool m_publisherPaused = false;
    std::int64_t publisherElapsedMs() const;
    matjson::Value m_publishBatchBody;
    std::int64_t m_publishBatchLastSequence = -1;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    std::string m_debugContextId, m_debugToken, m_debugPath;
    bool m_debugRestoreHistory=false;
#endif
    std::string m_publishAttemptId;
    std::uint64_t m_publishGeneration = 0;
    std::uint64_t m_publishSequence = 0;
    std::uint64_t m_publishCosmeticRevision=0;
    std::chrono::steady_clock::time_point m_publishStartedAt {};
    std::chrono::steady_clock::time_point m_nextPublishAt {};
    std::uint64_t m_publishRequestGeneration = 0;
    bool m_publishBusy = false;
    std::string m_publishBudgetAttemptId;
    unsigned m_attemptPublishFailures=0;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    std::chrono::steady_clock::time_point m_nextCameraMetricAt{};
    unsigned m_cameraMetricCount=0;
#endif
    matjson::Value m_terminalBody;
    std::string m_terminalSessionToken, m_terminalMatchToken, m_terminalPublishAttemptId;
    std::chrono::steady_clock::time_point m_terminalDeadline {}, m_nextTerminalAt {};
    std::uint64_t m_terminalRequestGeneration = 0;
    bool m_terminalPending = false, m_terminalBusy = false;
    std::optional<SpectatorGhostPose> m_deferredTerminalPose;
    std::string m_deferredMatchId;
    int m_deferredRound = 0, m_deferredDeathmatch = 0;

    std::string m_viewMatchId;
    bool m_viewOpponentPaused = false;
    std::string m_viewContextKey;
    std::string m_viewAttemptId;
    std::string m_viewTerminalAttemptId;
    std::uint64_t m_viewGeneration = 0;
    std::uint64_t m_viewPresentationEpoch = 0;
    std::int64_t m_lastViewSequence = -1;
    std::string m_opponentName;
    std::shared_ptr<SpectatorCosmetics const> m_viewCosmetics;
    std::uint64_t m_viewCosmeticRevision=0;
    std::string m_reason;
    SpectatorLiveStatus m_status = SpectatorLiveStatus::Idle;
    // Latest accepted pose is validation state, never the rendering timeline.
    std::optional<SpectatorGhostPose> m_lastAcceptedPose;
    std::int64_t m_lastPublisherElapsedMs = -1;
    mutable SpectatorInterpolationBuffer m_sampleBuffer;
    std::chrono::steady_clock::time_point m_nextBufferMetricAt {};
    bool m_serverPlaybackComplete = false;
    std::chrono::steady_clock::time_point m_lastPacketAt {};
    std::chrono::steady_clock::time_point m_nextViewerPollAt {};
    std::uint64_t m_viewRequestGeneration = 0;
    std::int64_t m_lastViewerRttMs = 0;
    int m_viewerPollGapMs = 20;
    int m_consecutiveViewerErrors = 0;
    bool m_viewBusy = false;
    SpectatorRestoreTimeline m_restoreTimeline;
    bool m_forceRestoreRequest=false,m_partialSync=false,m_disconnected=false;
    std::uint64_t m_reconnections=0;
    unsigned m_restoreFailures=0;
    double m_restoreStepCostMs=2;
    std::string m_worldRestoreReason;
    mutable std::optional<SpectatorRemoteFrame> m_heldPresentation;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_publishRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_terminalRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_viewRequest;
};

} // namespace corum::ranked
