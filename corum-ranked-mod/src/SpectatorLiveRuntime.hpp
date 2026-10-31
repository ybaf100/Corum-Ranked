#pragma once
#include "domain/SpectatorPublishQueue.hpp"

#include "domain/SpectatorRemotePolicy.hpp"
#include "domain/SpectatorInterpolationBuffer.hpp"
#include "domain/SpectatorRestoreTimeline.hpp"
#include "domain/SpectatorFailureFingerprint.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <set>

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
    RecaptureRequired,
};

// Mode-specific authority is supplied by SurvivalSpectatorRuntime, never by a fake Solo Match.
struct SurvivalSpectatorTransportContext {
    std::string matchId,key,session,roundKey,targetId,attemptId;
    int round=0,level=0,mapGeneration=0;
    bool viewer=false,publisher=false;
};
class SpectatorLiveRuntime final {
public:
    static SpectatorLiveRuntime& get();
    static SpectatorLiveRuntime& survival();
    void configureSurvival(SurvivalSpectatorTransportContext context);
    bool survivalViewerAuthorized()const{return m_external && m_external->viewer;}
    void stopSurvival();
    void pumpSurvivalPublisher(){if(m_external && m_external->publisher && std::chrono::steady_clock::now()>=m_nextPublishAt)publish({});}

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
    /** P2-D Debug HUD line: no token, pointer or user identity. */
    [[nodiscard]] std::string diagnosticsLine() const {
        auto [applied,total]=restoreProgress();
        return fmt::format("delay {:.0f}ms p95 {:.0f}ms starve {} rtt {:.0f}ms | {} {} | seq {} ep {} | restore {}/{} | ledger {} suppressed {}",
            m_sampleBuffer.delayMs(),m_sampleBuffer.p95ArrivalMs(),m_sampleBuffer.starvations(),m_sampleBuffer.averageRttMs(),
            spectatorFailureClassName(spectatorFailureClass(m_reason)),m_reason,m_lastViewSequence,m_viewPresentationEpoch,applied,total,m_failureLedger.size(),m_failureLedger.suppressed());
    }
    /** P1-B restore progress: {applied packets, applied + queued}. */
    [[nodiscard]] std::pair<std::uint64_t,std::uint64_t> restoreProgress() const{auto applied=m_restoreTimeline.appliedCount();return {applied,applied+m_restoreTimeline.queued()};}
    [[nodiscard]] std::string const& opponentName() const;
    [[nodiscard]] std::optional<SpectatorGhostPose> interpolatedPose() const;
    [[nodiscard]] std::optional<SpectatorRemoteFrame> presentationFrame() const;
    [[nodiscard]] std::optional<SpectatorRemoteFrame> worldFrame() const;
    bool restoringWorld()const{return m_forceRestoreRequest||m_restoreTimeline.active();}
    void audioPrepared(bool ready){m_presentationAudioReady=ready;}
    bool presentationPending()const{return m_debugBarrierPending;}
    char const* restorationPhase()const{
        if(m_forceRestoreRequest||m_viewAttemptId.empty())return "PREPARING";
        if(m_restoreTimeline.active())return m_restoreTimeline.queued()?"RESTORING WORLD":"CATCHING UP";
        if(m_debugBarrierPending)return "READY TO PRESENT";
        return "LIVE";
    }
    void requestSceneRestore();
    std::vector<SpectatorRemoteFrame> takeRestorationFrames();
    void restorationApplied(SpectatorRemoteFrame const& frame,bool clockAdvanced,double workMs=0);
    void noteWorldPartial(std::string reason);
    void worldCauseRecovered(std::string const& reason);

private:
    SpectatorLiveRuntime() = default;
    std::optional<SurvivalSpectatorTransportContext> m_external;
    std::string publishPath()const{return m_external?"/api/ranked/survival/online/matches/"+m_publishMatchId+"/spectator-live/publish":
        "/api/ranked/matches/"+m_publishMatchId+"/spectator-live/publish";}

    void resetPublisher();
    void resetViewer();
    void startPublisherGeneration(std::string const& matchId, std::string const& attemptId);
    void publish(SpectatorGhostPose const& pose);
    void pollViewer();
    void applyViewerResponse(matjson::Value const& root);
    void setFallback(std::string reason);
    [[nodiscard]] std::string endpoint(std::string const& path) const;

    std::string m_publishMatchId;
    unsigned m_sourceEvidenceLogs=0;
    int m_publishRound = 0, m_publishDeathmatch = 0, m_publishLevel=0;
    SpectatorPublishQueue m_publishQueue;
    std::chrono::steady_clock::time_point m_publisherPausedAt {};
    std::int64_t m_publisherPausedMs = 0;
    bool m_publisherPaused = false;
    std::int64_t publisherElapsedMs() const;
    matjson::Value m_publishBatchBody=nullptr;
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
    std::uint64_t m_publishRetries=0;
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
    std::string m_rejectedWorldContext;
    // P0-B: survives resetViewer()/requestSceneRestore(); only a new Attempt or
    // stream generation (a different key) may retry a rejected proof.
    SpectatorFailureLedger m_failureLedger;
    SpectatorRandomTimeline m_randomEvents;
    bool m_presentationAudioReady=false,m_debugBarrierPending=false;
    bool m_viewOpponentPaused = false;
    std::string m_viewContextKey;
    std::string m_viewAttemptId;
    std::string m_viewTerminalAttemptId;
    std::uint64_t m_viewGeneration = 0;
    std::uint64_t m_viewPresentationEpoch = 0;
    unsigned m_viewEvidenceLogs=0;
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
    std::set<std::string> m_worldCauses;
    mutable std::optional<SpectatorRemoteFrame> m_heldPresentation;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_publishRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_terminalRequest;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_viewRequest;
};

} // namespace corum::ranked
