#include "SpectatorLiveRuntime.hpp"
#include "domain/SpectatorViewerPollPolicy.hpp"
#include "RankedRuntime.hpp"
#include "domain/SpectatorProtocol.hpp"
#include "spectator/SpectatorPoseCodec.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#endif

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace {

constexpr int kProtocolVersion = corum::ranked::kSpectatorProtocolVersion;
constexpr auto kPublishInterval = std::chrono::milliseconds(corum::ranked::kSpectatorPublishIntervalMs);
constexpr auto kPacketTimeout = std::chrono::milliseconds(1500);
constexpr float kPoseDiscontinuityDistance = 400.0f;
constexpr std::string_view kDefaultRankedServerURL = "https://api.52-74-37-130.sslip.io";

bool successful(web::WebResponse const& response) {
    return response.code() >= 200 && response.code() < 300;
}

web::WebRequest spectatorRequest(std::string const& sessionToken, std::string const& matchToken) {
    web::WebRequest request;
    request.header("User-Agent", fmt::format(
        "{}/{}; Geometry Dash/{}; Geode/{}; {}",
        Mod::get()->getID(),
        Mod::get()->getVersion().toVString(),
        GEODE_GD_VERSION_STRING,
        Loader::get()->getVersion().toVString(),
        GEODE_PLATFORM_NAME
    ));
    if (!sessionToken.empty()) request.header("Authorization", "Bearer " + sessionToken);
    if (!matchToken.empty()) request.header("x-match-token", matchToken);
    request.followRedirects(false);
    request.timeout(std::chrono::seconds(3));
    return request;
}

bool finitePose(corum::ranked::SpectatorGhostPose const& pose) {
    return corum::ranked::finiteSpectatorPose(pose);
}

float poseDistance(
    corum::ranked::SpectatorGhostPose const& left,
    corum::ranked::SpectatorGhostPose const& right
) {
    auto const dx = right.x - left.x;
    auto const dy = right.y - left.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string viewerContextKey(corum::ranked::MatchView const& match) {
    return fmt::format(
        "{}:R{}:D{}:L{}",
        match.matchId,
        match.roundNumber,
        match.deathmatchSequence,
        match.currentMap ? match.currentMap->levelId : 0
    );
}

} // namespace

namespace corum::ranked {

SpectatorLiveRuntime& SpectatorLiveRuntime::get() {
    static SpectatorLiveRuntime runtime;
    return runtime;
}

SpectatorLiveRuntime& SpectatorLiveRuntime::survival(){static auto* r=new SpectatorLiveRuntime;return *r;}
void SpectatorLiveRuntime::configureSurvival(SurvivalSpectatorTransportContext context){
    if(!m_external || m_external->key!=context.key || m_external->session!=context.session)reset();
    m_external=std::move(context);
}
void SpectatorLiveRuntime::stopSurvival(){reset();m_external.reset();}

SpectatorLiveStatus SpectatorLiveRuntime::status() const {
    if(m_disconnected)return SpectatorLiveStatus::Disconnected;
    if(m_consecutiveViewerErrors>0)return SpectatorLiveStatus::Reconnecting;
    if(!m_rejectedWorldContext.empty())return SpectatorLiveStatus::RecaptureRequired;
    if(m_forceRestoreRequest || m_restoreTimeline.active())return SpectatorLiveStatus::Syncing;
    if(m_sampleBuffer.terminalRendered() && m_lastAcceptedPose && m_lastAcceptedPose->finished)return SpectatorLiveStatus::Cleared;
    if(m_status==SpectatorLiveStatus::Waiting)return SpectatorLiveStatus::Waiting;
    if(m_status==SpectatorLiveStatus::Live && m_sampleBuffer.buffering())return SpectatorLiveStatus::Buffering;
    if(m_partialSync && m_lastAcceptedPose)return SpectatorLiveStatus::PartialSync;
    return m_status;
}

std::string const& SpectatorLiveRuntime::reason() const {
    return m_reason;
}

std::string const& SpectatorLiveRuntime::opponentName() const {
    return m_opponentName;
}

std::string SpectatorLiveRuntime::endpoint(std::string const& path) const {
    return std::string(kDefaultRankedServerURL) + path;
}

void SpectatorLiveRuntime::resetPublisher() {
    m_sourceEvidenceLogs=0;
    ++m_publishRequestGeneration;
    m_publishBusy = false;
    m_publishMatchId.clear();
    m_publishAttemptId.clear();
    m_publishSequence = 0; m_publishCosmeticRevision=0;
    m_publishQueue = {}; m_publishBatchBody = nullptr; m_publishBatchLastSequence = -1;
    m_publisherPaused = false; m_publisherPausedAt = {}; m_publisherPausedMs = 0;
    m_publishStartedAt = {};
    m_nextPublishAt = {};
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    m_nextCameraMetricAt={};m_cameraMetricCount=0;
#endif
}

void SpectatorLiveRuntime::resetViewer() {
    m_viewOpponentPaused = false;
    m_worldCauses.clear();m_restoreTimeline.reset();m_randomEvents.reset();m_rejectedWorldContext.clear();m_presentationAudioReady=m_debugBarrierPending=false;m_forceRestoreRequest=m_partialSync=m_disconnected=false;m_heldPresentation.reset();m_reconnections=m_restoreFailures=0;
    m_worldRestoreReason.clear();m_worldCauses.clear();m_partialSync=false;m_restoreStepCostMs=2;
    m_viewerPollGapMs = 20;
    ++m_viewRequestGeneration;
    ++m_viewPresentationEpoch;m_viewEvidenceLogs=0;
    if (m_viewPresentationEpoch == 0) ++m_viewPresentationEpoch;
    m_viewBusy = false;
    m_viewMatchId.clear();
    m_viewContextKey.clear();
    m_viewAttemptId.clear();m_viewCosmetics.reset();m_viewCosmeticRevision=0;
    m_viewTerminalAttemptId.clear();
    m_viewGeneration = 0;
    m_lastViewSequence = -1;
    m_opponentName.clear();
    m_reason.clear();
    m_status = SpectatorLiveStatus::Idle;
    m_lastAcceptedPose.reset();
    m_lastPublisherElapsedMs = -1;
    m_sampleBuffer.reset();
    m_nextBufferMetricAt = {}; m_serverPlaybackComplete = false;
    m_lastPacketAt = {};
    m_nextViewerPollAt = {};
    m_lastViewerRttMs = 0;
    m_consecutiveViewerErrors = 0;
}

void SpectatorLiveRuntime::reset() {
    m_publishBudgetAttemptId.clear();m_attemptPublishFailures=0;
    ++m_terminalRequestGeneration;
    m_terminalPending = m_terminalBusy = false;
    m_deferredTerminalPose.reset(); m_deferredMatchId.clear();
    m_terminalBody = matjson::Value();
    m_terminalSessionToken.clear(); m_terminalMatchToken.clear(); m_terminalPublishAttemptId.clear();
    resetPublisher();
    resetViewer();
}
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
void SpectatorLiveRuntime::startDebugViewer(std::string contextId, std::string token, std::string path,bool restoreHistory) {
    m_debugRestoreHistory=restoreHistory;
    reset();m_debugBarrierPending=true;m_presentationAudioReady=false;
    m_debugContextId = std::move(contextId); m_debugToken = std::move(token); m_debugPath = std::move(path);
    m_status = SpectatorLiveStatus::Waiting;
}
void SpectatorLiveRuntime::stopDebugViewer() {
    m_debugContextId.clear(); m_debugToken.clear(); m_debugPath.clear();m_debugRestoreHistory=false; reset();
}
#endif

void SpectatorLiveRuntime::startPublisherGeneration(
    std::string const& matchId,
    std::string const& attemptId
) {
    m_publishMatchId = matchId;
    m_publishAttemptId = attemptId;
    auto const budget=matchId+":"+attemptId;
    if(budget!=m_publishBudgetAttemptId){m_publishBudgetAttemptId=budget;m_attemptPublishFailures=0;}
    auto const& match = RankedRuntime::get().view().match;
    m_publishRound = m_external?m_external->round:match.roundNumber;
    m_publishLevel=m_external?m_external->level:match.currentMap?match.currentMap->levelId:0;
    // A server start certificate can release this Attempt before the next state
    // poll changes DEATHMATCH_ENTERING to PLAYING. Context is an exact identity,
    // not a presentation phase; retain it across that delayed state projection.
    m_publishDeathmatch = m_external?0:match.deathmatchSequence > 0 ? match.deathmatchSequence : 0;
    ++m_publishGeneration;
    if (m_publishGeneration == 0) m_publishGeneration = 1;
    m_publishSequence = 0; m_publishCosmeticRevision=0;
    m_publishStartedAt = std::chrono::steady_clock::now();
    m_nextPublishAt = m_publishStartedAt;
}

void SpectatorLiveRuntime::publisherPaused(bool paused) {
    auto const now = std::chrono::steady_clock::now();
    if (paused && !m_publisherPaused) m_publisherPausedAt = now;
    if (!paused && m_publisherPaused) m_publisherPausedMs +=
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_publisherPausedAt).count();
    m_publisherPaused = paused;
}
std::int64_t SpectatorLiveRuntime::publisherElapsedMs() const {
    auto const now = std::chrono::steady_clock::now();
    auto const activePause = m_publisherPaused ?
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_publisherPausedAt).count() : 0;
    return std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(now - m_publishStartedAt).count()
        - m_publisherPausedMs - activePause);
}
bool SpectatorLiveRuntime::tickPublisher(SpectatorGhostPose const& pose, bool practice) {
    tickTerminal();
    auto& ranked = RankedRuntime::get();
    auto const& view = ranked.view();
    auto const& match = view.match;
    auto const attemptId = m_external?m_external->attemptId:ranked.spectatorLiveAttemptId();
    auto const eligible = m_external ? (m_external->publisher && !practice && !attemptId.empty() && finitePose(pose)) :
        view.stage == RuntimeStage::Matched &&
        !ranked.isPlacement() &&
        !ranked.isSpectating() && match.spectatorLiveEnabled &&
        !practice &&
        !match.matchId.empty() &&
        !attemptId.empty() &&
        finitePose(pose);
    if (!eligible) {
        if(!match.spectatorLiveEnabled){m_publishBudgetAttemptId.clear();m_attemptPublishFailures=0;}
        if (!m_terminalPending) resetPublisher();
        return false;
    }
    auto const publisherMatch=m_external?m_external->matchId:match.matchId;
    if (m_publishMatchId != publisherMatch || m_publishAttemptId != attemptId) {
        resetPublisher();
        startPublisherGeneration(publisherMatch, attemptId);
    }
    if (attemptId == m_terminalPublishAttemptId) return false;
    if(m_attemptPublishFailures>=12)return false;
    auto const now = std::chrono::steady_clock::now();
    if (m_publishQueue.full()) {
        if(!m_publishBusy && now>=m_nextPublishAt)publish(pose);
        return false; // bounded backpressure; never create an overlapping POST or rewind this Attempt clock
    }
    auto const elapsed=pose.nativeElapsedMs && std::isfinite(*pose.nativeElapsedMs) && *pose.nativeElapsedMs>=0 && *pose.nativeElapsedMs<=86400000.0
        ? static_cast<std::int64_t>(*pose.nativeElapsedMs):publisherElapsedMs();
    auto const captured = !m_publisherPaused && m_publishQueue.capture(elapsed, pose);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(captured && pose.camera && pose.camera->transform && m_cameraMetricCount<120 && now>=m_nextCameraMetricAt){
        m_nextCameraMetricAt=now+std::chrono::seconds(1);++m_cameraMetricCount;auto const& c=*pose.camera;auto const& t=*c.transform;
        log::info("[SpectatorCameraPublisher] sequence={} publisherMs={} cameraX={} cameraY={} zoom={} angle={} scaleX={} scaleY={} layerRotation={}",
            m_publishQueue.nextSequence()-1,elapsed,c.x,c.y,t.zoom,t.angle,t.layers[0].scaleX,t.layers[0].scaleY,t.layers[0].rotation);
    }
#endif
    if(captured&&(m_publishLevel==143308401)&&elapsed>=70000&&elapsed<=73000&&m_sourceEvidenceLogs++<128){
        auto wire=encodeSpectatorPose(pose);
        log::info("[SpectatorEvidence] stage=QUEUED_ENCODED attempt={} generation={} sequence={} elapsedMs={} p1={} p2={}",m_publishAttemptId,m_publishGeneration,m_publishQueue.nextSequence()-1,elapsed,spectatorEvidenceDiscontinuity(wire),spectatorEvidenceDiscontinuity(wire,true));
    }
    m_publishSequence = static_cast<std::uint64_t>(m_publishQueue.nextSequence());
    if (!m_publishBusy && now >= m_nextPublishAt) publish(pose);
    // Consumed native evidence is tied to capture, never to HTTP request/ACK.
    return captured;
}

void SpectatorLiveRuntime::publishTerminal(SpectatorGhostPose const& pose, bool practice) {
    if(m_external){
        if(practice || !m_external->publisher || m_external->attemptId.empty() || !finitePose(pose) || !(pose.dead||pose.finished))return;
        if(m_terminalPublishAttemptId==m_external->attemptId)return;
        if(m_publishAttemptId!=m_external->attemptId){resetPublisher();startPublisherGeneration(m_external->matchId,m_external->attemptId);}
        auto elapsed=pose.nativeElapsedMs?static_cast<std::int64_t>(*pose.nativeElapsedMs):publisherElapsedMs();
        if(m_publishQueue.capture(std::max(elapsed,m_publishQueue.lastElapsedMs()),pose)){
            m_terminalPublishAttemptId=m_external->attemptId;
            publish(pose);
        }
        return;
    }
    auto& ranked = RankedRuntime::get();
    auto const& match = ranked.view().match;
    auto const attempt = ranked.spectatorLiveAttemptId();
    if (practice || !match.spectatorLiveEnabled || ranked.isPlacement() || ranked.isSpectating() ||
        ranked.view().stage != RuntimeStage::Matched || match.matchId.empty() ||
        (!attempt.empty() && attempt == m_terminalPublishAttemptId) || !finitePose(pose) ||
        !(pose.dead || pose.finished)) return;
    if (attempt.empty()) {
        // A real early death/Clear can precede Start ACK. Capture it without
        // inventing an Attempt ID; RankedRuntime drains it at the actual ACK.
        m_deferredTerminalPose = pose; m_deferredMatchId = match.matchId;
        m_deferredRound = match.roundNumber; m_deferredDeathmatch = match.deathmatchSequence;
        m_terminalDeadline = std::chrono::steady_clock::now() + kPacketTimeout;
        return;
    }
    if (m_publishMatchId != match.matchId || m_publishAttemptId != attempt) {
        resetPublisher(); startPublisherGeneration(match.matchId, attempt);
    }
    ++m_terminalRequestGeneration;
    m_terminalBody = matjson::Value();
    m_terminalBody["protocolVersion"] = kProtocolVersion;
    m_terminalBody["matchId"] = match.matchId;
    m_terminalBody["roundNumber"] = match.roundNumber;
    if (match.deathmatchSequence > 0) m_terminalBody["deathmatchSequence"] = match.deathmatchSequence;
    m_terminalBody["attemptId"] = attempt;
    m_terminalBody["generation"] = static_cast<std::int64_t>(m_publishGeneration);
    // Capture the terminal immediately, but wait for the preceding live ACK to
    // preserve contiguous sequence delivery. It bypasses the normal 50ms delay.
    m_terminalBody["sequence"] = static_cast<std::int64_t>(m_publishQueue.nextSequence());
    m_terminalBody["elapsedMs"] = static_cast<std::int64_t>(std::max<std::int64_t>(0,
        pose.nativeElapsedMs && std::isfinite(*pose.nativeElapsedMs) && *pose.nativeElapsedMs>=0 && *pose.nativeElapsedMs<=86400000.0
            ? static_cast<std::int64_t>(*pose.nativeElapsedMs):publisherElapsedMs()));
    m_terminalBody["elapsedMs"] = std::max<std::int64_t>(
        m_terminalBody["elapsedMs"].asInt().unwrapOr(0), m_publishQueue.lastElapsedMs());
    m_terminalBody["keyframe"] = encodeSpectatorPose(pose);
    if(pose.cosmetics && validSpectatorCosmetics(*pose.cosmetics) && m_publishCosmeticRevision!=pose.cosmetics->revision){
        m_terminalBody["cosmetics"]=encodeSpectatorCosmetics(*pose.cosmetics);
    }
    m_terminalBody["levelId"]=m_publishLevel;
    m_terminalSessionToken = ranked.spectatorLiveSessionToken(); m_terminalMatchToken = ranked.spectatorLiveMatchToken();
    m_terminalPublishAttemptId = attempt;
    m_terminalDeadline = std::chrono::steady_clock::now() + kPacketTimeout;
    m_nextTerminalAt = {}; m_terminalPending = true; m_terminalBusy = false;
    tickTerminal();
}

void SpectatorLiveRuntime::tickTerminal() {
    if (m_deferredTerminalPose) {
        auto& ranked = RankedRuntime::get();
        auto const& match = ranked.view().match;
        if (std::chrono::steady_clock::now() >= m_terminalDeadline || match.matchId != m_deferredMatchId ||
            match.roundNumber != m_deferredRound || match.deathmatchSequence != m_deferredDeathmatch) {
            m_deferredTerminalPose.reset();
        } else if (!ranked.spectatorLiveAttemptId().empty()) {
            auto const pose = *m_deferredTerminalPose; m_deferredTerminalPose.reset();
            publishTerminal(pose, false); return;
        }
    }
    if (!m_terminalPending) return;
    auto const now = std::chrono::steady_clock::now();
    if (now >= m_terminalDeadline) { m_terminalPending = false; return; }
    if (m_terminalBusy || now < m_nextTerminalAt ||
        (m_publishBusy && m_publishAttemptId == m_terminalPublishAttemptId)) return;
    if (!m_publishQueue.empty() || m_publishBatchBody.isObject()) {
        publish({}); return; // drain captured frames in sequence before terminal
    }
    auto const generation = m_terminalRequestGeneration;
    auto request = spectatorRequest(m_terminalSessionToken, m_terminalMatchToken);
    request.bodyJSON(m_terminalBody);
    m_terminalBusy = true;
    auto const matchId = m_terminalBody["matchId"].asString().unwrapOr("");
    m_terminalRequest.spawn(request.post(endpoint(publishPath())),
        [this, generation](web::WebResponse response) {
            if (generation != m_terminalRequestGeneration) return;
            m_terminalBusy = false;
            if (successful(response) || response.code() == 400 || response.code() == 401 || response.code() == 403) {
                m_terminalPending = false;
            } else {
                m_nextTerminalAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            }
        });
}

void SpectatorLiveRuntime::publish(SpectatorGhostPose const&) {
    auto& ranked = RankedRuntime::get();
    if (m_publishBusy || m_publishMatchId.empty() || m_publishAttemptId.empty()) return;
    auto const session = m_external?m_external->session:m_terminalPending ? m_terminalSessionToken : ranked.spectatorLiveSessionToken();
    auto const token = m_terminalPending ? m_terminalMatchToken : ranked.spectatorLiveMatchToken();
    if (session.empty() || (!m_external && token.empty())) return;
    if (!m_publishBatchBody.isObject()) {
        auto const batch = m_publishQueue.batch();
        if (batch.empty()) return;
        auto packets = matjson::Value::array();
        for (auto const& sample : batch) {
            matjson::Value packet;
            packet["protocolVersion"] = kProtocolVersion;
            packet["matchId"] = m_publishMatchId; packet["roundNumber"] = m_publishRound;
            if (m_publishDeathmatch > 0) packet["deathmatchSequence"] = m_publishDeathmatch;
            packet["attemptId"] = m_publishAttemptId;
            packet["generation"] = static_cast<std::int64_t>(m_publishGeneration);
            packet["sequence"] = sample.sequence; packet["elapsedMs"] = sample.elapsedMs;
            packet["keyframe"] = encodeSpectatorPose(sample.pose);
            packet["levelId"]=m_publishLevel;
            if(sample.pose.cosmetics && validSpectatorCosmetics(*sample.pose.cosmetics) && m_publishCosmeticRevision!=sample.pose.cosmetics->revision){
                packet["cosmetics"]=encodeSpectatorCosmetics(*sample.pose.cosmetics);m_publishCosmeticRevision=sample.pose.cosmetics->revision;
            }
            if(m_publishLevel==143308401&&sample.elapsedMs>=70000&&sample.elapsedMs<=73000&&m_sourceEvidenceLogs++<128)
                log::info("[SpectatorEvidence] stage=PUBLISH attempt={} generation={} sequence={} elapsedMs={} p1={} p2={}",m_publishAttemptId,m_publishGeneration,sample.sequence,sample.elapsedMs,spectatorEvidenceDiscontinuity(packet["keyframe"]),spectatorEvidenceDiscontinuity(packet["keyframe"],true));
            packets.push(packet);
        }
        m_publishBatchBody = matjson::Value(); m_publishBatchBody["packets"] = packets;
        m_publishBatchLastSequence = batch.back().sequence;
    }
    if(m_external){m_publishBatchBody["roundKey"]=m_external->roundKey;m_publishBatchBody["mapGeneration"]=m_external->mapGeneration;m_publishBatchBody["levelId"]=m_external->level;}
    auto request = spectatorRequest(session, token); request.bodyJSON(m_publishBatchBody);
    auto const requestGeneration = ++m_publishRequestGeneration;
    auto const matchId = m_publishMatchId, attemptId = m_publishAttemptId;
    auto const lastSequence = m_publishBatchLastSequence;
    m_publishBusy = true;
    m_publishRequest.spawn(
        request.post(endpoint(publishPath())),
        [this, requestGeneration, matchId, attemptId, lastSequence](web::WebResponse response) {
            if (requestGeneration != m_publishRequestGeneration || matchId != m_publishMatchId ||
                attemptId != m_publishAttemptId) return;
            m_publishBusy = false;
            if (successful(response)) {
                m_attemptPublishFailures=0;
                m_publishQueue.acknowledge(lastSequence);
                m_publishBatchBody = nullptr; m_publishBatchLastSequence = -1;
                m_nextPublishAt = std::chrono::steady_clock::now(); // no extra ACK-bound 50ms wait
                return;
            }
            ++m_attemptPublishFailures;
            if(response.code()==401 || response.code()==403)m_attemptPublishFailures=12;
            m_nextPublishAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(
                std::min(1000,100*(1<<std::min(3,static_cast<int>(m_attemptPublishFailures)-1))));
            if (response.code() == 409) {
                ++m_publishGeneration; if (!m_publishGeneration) m_publishGeneration = 1;
                m_publishQueue = {}; m_publishBatchBody = nullptr; m_publishBatchLastSequence = -1;
                m_publishSequence = 0; m_publishCosmeticRevision=0; // The Attempt's publisher clock never rewinds on reconnect.
                if (m_terminalPending) {
                    m_terminalBody["generation"] = static_cast<std::int64_t>(m_publishGeneration);
                    m_terminalBody["sequence"] = 0;
                }
                log::debug("Restarting spectator relay generation after HTTP 409");
            }
            // Network failures retry the exact batch, including native snap
            // evidence and capture timestamps. Capture can continue meanwhile.
        }
    );
}

void SpectatorLiveRuntime::tickViewer() {
    auto& ranked = RankedRuntime::get();
    auto const& view = ranked.view();
    auto const& match = view.match;
    bool eligible =
        view.stage == RuntimeStage::Matched && !ranked.isPlacement() &&
        ranked.isSpectating() && !ranked.hasLocalAttemptInFlight() && match.spectatorLiveEnabled && !match.matchId.empty() &&
        !ranked.spectatorLiveSessionToken().empty() && !ranked.spectatorLiveMatchToken().empty();
    auto matchId = match.matchId;
    auto contextKey = viewerContextKey(match);
    if(m_external){eligible=m_external->viewer;matchId=m_external->matchId;contextKey=m_external->key;}
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) {
        eligible = debug::DebugSpectateRuntime::get().playbackLevel(debug::DebugSpectateRuntime::get().levelId());
        matchId = m_debugContextId;
        contextKey = fmt::format("{}:R1:D0:L{}", matchId, debug::DebugSpectateRuntime::get().levelId());
    }
#endif
    if (!eligible) {
        resetViewer(); return;
    }
    // Debug admission uses a separate server-issued scope, not a fake Ranked Match.
    if (m_viewMatchId != matchId || m_viewContextKey != contextKey) {
        resetViewer();
        m_viewMatchId = matchId;
        m_viewContextKey = contextKey;
        m_status = SpectatorLiveStatus::Waiting;
    }
    auto const now = std::chrono::steady_clock::now();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty() && m_serverPlaybackComplete && m_sampleBuffer.terminalRendered())
        debug::DebugSpectateRuntime::get().viewerResponse(true, false);
#endif
    bool terminalHeld = false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    terminalHeld = !m_debugContextId.empty() && m_lastAcceptedPose && (m_lastAcceptedPose->dead || m_lastAcceptedPose->finished);
#endif
    if (!terminalHeld && !m_viewOpponentPaused && (
        m_status == SpectatorLiveStatus::Live && m_lastPacketAt.time_since_epoch().count() != 0 &&
        now - m_lastPacketAt > kPacketTimeout
    )) {
        setFallback("STREAM_TIMEOUT");
    }
    if (m_disconnected || m_viewBusy || now < m_nextViewerPollAt) return;
    pollViewer();
}

void SpectatorLiveRuntime::pollViewer() {
    if (m_viewBusy) return;
    auto& ranked = RankedRuntime::get();
    auto matchId = m_external?m_external->matchId:ranked.view().match.matchId;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) matchId = m_debugContextId;
#endif
    if (matchId.empty()) return;
    auto request = spectatorRequest(
        ranked.spectatorLiveSessionToken(),
        ranked.spectatorLiveMatchToken()
    );
    auto path = fmt::format(
        "/api/ranked/matches/{}/spectator-live?protocolVersion={}",
        matchId,
        kProtocolVersion
    );
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) {
        request = spectatorRequest({}, {});
        request.header("x-corum-debug-spectate", "1");
        request.header("x-debug-spectate-token", m_debugToken);
        path = m_debugPath + fmt::format("?protocolVersion={}", kProtocolVersion);
    }
#endif
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(m_debugContextId.empty() || m_debugRestoreHistory)
#endif
        path+="&worldHistory=1";
    if(m_external){
        request=spectatorRequest(m_external->session,{});
        path="/api/ranked/survival/online/matches/"+matchId+"/spectator-live?protocolVersion=2&targetId="+m_external->targetId+
            "&roundKey="+m_external->roundKey+"&mapGeneration="+std::to_string(m_external->mapGeneration)+"&levelId="+std::to_string(m_external->level);
    }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(!m_debugContextId.empty()){
        path+="&restoreBarrier=1";
        if(!restoringWorld()&&m_presentationAudioReady&&m_lastViewSequence>=0)path+=fmt::format("&readySequence={}",m_lastViewSequence);
    }
#endif
    path+=fmt::format("&cosmeticRevision={}",m_viewCosmeticRevision);
    if (!m_forceRestoreRequest && !m_viewAttemptId.empty()) path += "&attemptId=" + m_viewAttemptId;
    if (!m_forceRestoreRequest && m_viewGeneration > 0) path += fmt::format("&generation={}", m_viewGeneration);
    if (!m_forceRestoreRequest && m_lastViewSequence >= 0) path += fmt::format("&afterSequence={}", m_lastViewSequence);

    auto const requestGeneration = ++m_viewRequestGeneration;
    auto const contextKey = m_viewContextKey;
    auto const requestStartedAt = std::chrono::steady_clock::now();
    m_viewBusy = true;
    m_viewRequest.spawn(
        request.get(endpoint(path)),
        [this, requestGeneration, matchId, contextKey, requestStartedAt](web::WebResponse response) {
            auto currentContextKey = m_external?m_external->key:viewerContextKey(RankedRuntime::get().view().match);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (!m_debugContextId.empty()) currentContextKey = fmt::format("{}:R1:D0:L{}", m_debugContextId, debug::DebugSpectateRuntime::get().levelId());
#endif
            if (
                requestGeneration != m_viewRequestGeneration ||
                matchId != m_viewMatchId || contextKey != m_viewContextKey ||
                contextKey != currentContextKey
            ) return;
            m_viewBusy = false;
            auto& authority=RankedRuntime::get();
            bool authorized=authority.view().stage==RuntimeStage::Matched && !authority.isPlacement() &&
                authority.isSpectating() && !authority.hasLocalAttemptInFlight() && authority.view().match.spectatorLiveEnabled;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if(!m_debugContextId.empty())authorized=debug::DebugSpectateRuntime::get().playbackLevel(debug::DebugSpectateRuntime::get().levelId());
#endif
            if(m_external)authorized=m_external->viewer;
            if(!authorized){resetViewer();return;}
            if(m_external && (response.code()==401||response.code()==403||response.code()==409)){
                m_external->viewer=false;resetViewer();return;
            }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (!m_debugContextId.empty() && (response.code() == 401 || response.code() == 403 || response.code() == 404)) {
                debug::DebugSpectateRuntime::get().viewerResponse(false, true); return;
            }
#endif
            if (response.code() == 403) {
                resetViewer();
                return;
            }
            if (!successful(response)) {
                ++m_consecutiveViewerErrors;
                m_reason="NETWORK_ERROR";
                if(m_consecutiveViewerErrors>=12){m_disconnected=true;m_reason="NETWORK_RETRY_EXHAUSTED";return;}
                m_viewerPollGapMs = spectatorViewerPollGapMs(false, true, false, false, 0, 0, 0, m_consecutiveViewerErrors);
                if(m_external)m_viewerPollGapMs=std::max(20,m_viewerPollGapMs);
            m_nextViewerPollAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_viewerPollGapMs);
                return;
            }
            m_consecutiveViewerErrors = 0;
            m_lastViewerRttMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - requestStartedAt
            ).count();
            auto const root = response.json().unwrapOr(matjson::Value());
            applyViewerResponse(root);
            m_viewerPollGapMs = spectatorViewerPollGapMs(
                m_status == SpectatorLiveStatus::Live, m_status == SpectatorLiveStatus::Fallback,
                m_sampleBuffer.starving(), m_sampleBuffer.terminalRendered() || m_viewOpponentPaused,
                m_sampleBuffer.latestTimeMs() - m_sampleBuffer.renderTimeMs(),
                m_sampleBuffer.delayMs(), m_lastViewerRttMs, 0);
            if(m_external)m_viewerPollGapMs=std::max(20,m_viewerPollGapMs);
            if(!m_rejectedWorldContext.empty())m_viewerPollGapMs=std::max(1000,m_viewerPollGapMs);
            m_nextViewerPollAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_viewerPollGapMs);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (!m_debugContextId.empty() && m_lastAcceptedPose &&
                (m_lastAcceptedPose->dead || m_lastAcceptedPose->finished))
                m_serverPlaybackComplete = root["playbackComplete"].asBool().unwrapOr(false);
#endif
        }
    );
}

void SpectatorLiveRuntime::applyViewerResponse(matjson::Value const& root) {
    if (!root.isObject()) {
        setFallback("INVALID_RESPONSE");
        return;
    }
    auto const protocolVersion = root["protocolVersion"].asInt().unwrapOr(0);
    if (protocolVersion != kProtocolVersion) {
        setFallback("UNSUPPORTED_PROTOCOL_VERSION");
        return;
    }
    if(m_external && !root["fallback"].asBool().unwrapOr(false)){
        if(root["targetId"].asString().unwrapOr("")!=m_external->targetId ||
           root["context"]["roundKey"].asString().unwrapOr("")!=m_external->roundKey ||
           root["context"]["mapGeneration"].asInt().unwrapOr(0)!=m_external->mapGeneration ||
           root["context"]["levelId"].asInt().unwrapOr(0)!=m_external->level){setFallback("SURVIVAL_STREAM_CONTEXT_MISMATCH");return;}
    }
    m_debugBarrierPending=root["awaitingWorldReady"].asBool().unwrapOr(false);
    auto const paused = root["paused"].asBool().unwrapOr(false);
    if (m_viewOpponentPaused && !paused) m_lastPacketAt = std::chrono::steady_clock::now();
    m_viewOpponentPaused = paused;
    m_sampleBuffer.setSourcePaused(paused, std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (root["fallback"].asBool().unwrapOr(false)) {
        setFallback(root["reason"].asString().unwrapOr("SERVER_FALLBACK"));
        return;
    }
    if (root["waiting"].asBool().unwrapOr(false)) {
        auto const reason = root["reason"].asString().unwrapOr("WAITING");
        if (m_status != SpectatorLiveStatus::Waiting || m_reason != reason) {
            log::debug(
                "[SpectatorLive] waiting match={} context={} reason={} rttMs={}",
                m_viewMatchId,
                m_viewContextKey,
                reason,
                m_lastViewerRttMs
            );
        }
        if(m_restoreTimeline.active()){
            m_heldPresentation=m_restoreTimeline.frame();m_restoreTimeline.reset();noteWorldPartial("RESTORE_HISTORY_GAP");m_forceRestoreRequest=true;
        }
        m_status = SpectatorLiveStatus::Waiting;
        m_reason = reason;
        // A transient empty relay response hides presentation without allocating
        // a new Remote Player. Only an explicit stream/context reset changes epoch.
        return;
    }

    auto const stream = root["stream"];
    if (!stream.isObject()) {
        setFallback("MISSING_STREAM");
        return;
    }
    auto const matchId = stream["matchId"].asString().unwrapOr("");
    auto const attemptId = stream["attemptId"].asString().unwrapOr("");
    auto const generation = static_cast<std::uint64_t>(stream["generation"].asInt().unwrapOr(0));
    auto const& authoritativeMatch = RankedRuntime::get().view().match;
    auto expectedRound = m_external?m_external->round:authoritativeMatch.roundNumber;
    auto expectedDeathmatch = m_external?0:authoritativeMatch.deathmatchSequence;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) { expectedRound = 1; expectedDeathmatch = 0; }
#endif
    auto const streamRound = static_cast<int>(stream["roundNumber"].asInt().unwrapOr(-1));
    auto const streamDeathmatch = static_cast<int>(stream["deathmatchSequence"].asInt().unwrapOr(0));
    if (
        matchId != m_viewMatchId || attemptId.empty() || generation == 0 ||
        streamRound != expectedRound || streamDeathmatch != expectedDeathmatch
    ) {
        setFallback("STREAM_CONTEXT_MISMATCH");
        return;
    }
    auto const restoration=root["restoration"];
    auto const hasRestoration=restoration.isObject();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(m_debugContextId.empty() && !hasRestoration)noteWorldPartial("WORLD_HISTORY_UNAVAILABLE");
#else
    if(!hasRestoration)noteWorldPartial("WORLD_HISTORY_UNAVAILABLE");
#endif
    auto expectedLevel=m_external?m_external->level:authoritativeMatch.currentMap?authoritativeMatch.currentMap->levelId:0;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if(!m_debugContextId.empty())expectedLevel=debug::DebugSpectateRuntime::get().levelId();
#endif
    auto const historyLevel=restoration["levelId"].asInt().unwrapOr(0);
    if(hasRestoration && historyLevel>0 && historyLevel!=expectedLevel){setFallback("HISTORY_LEVEL_MISMATCH");return;}
    if(hasRestoration){
        auto const head=restoration["headSequence"].asInt().unwrapOr(-1),start=restoration["startSequence"].asInt().unwrapOr(-1);
        if(restoration["available"].asBool().isErr() || restoration["caughtUp"].asBool().isErr() || start<0 || head<start ||
           (restoration["available"].asBool().unwrapOr(false) && (start!=0 || historyLevel<=0))){setFallback("INVALID_RESTORE_DESCRIPTOR");return;}
    }
    auto const resetStream = root["reset"].asBool().unwrapOr(false);
    auto const streamChanged = attemptId != m_viewAttemptId || generation != m_viewGeneration;
    auto const worldIdentity=attemptId+":"+std::to_string(generation);
    if(!streamChanged&&m_rejectedWorldContext==worldIdentity)return;
    if(!streamChanged&&hasRestoration&&restoration["reason"].asString().unwrapOr("")=="ATTEMPT_ENDED_DURING_RESTORE"){
        auto packets=root["packets"].asArray();
        if(!packets.isErr()&&!packets.unwrap().empty()){
            auto const& packet=packets.unwrap().back();auto terminal=decodeSpectatorPose(packet["keyframe"]);
            if(terminal&&(terminal->dead||terminal->finished)&&packet["protocolVersion"].asInt().unwrapOr(0)==kProtocolVersion&&packet["sequence"].asInt().unwrapOr(-1)==restoration["headSequence"].asInt().unwrapOr(-2)&&packet["deathmatchSequence"].asInt().unwrapOr(0)==expectedDeathmatch&&packet["matchId"].asString().unwrapOr("")==m_viewMatchId&&packet["roundNumber"].asInt().unwrapOr(0)==expectedRound&&packet["levelId"].asInt().unwrapOr(expectedLevel)==expectedLevel&&packet["attemptId"].asString().unwrapOr("")==attemptId&&packet["generation"].asInt().unwrapOr(0)==static_cast<std::int64_t>(generation)){
                m_restoreTimeline.reset();m_randomEvents.reset();m_sampleBuffer.clear();m_heldPresentation.reset();m_lastAcceptedPose=*terminal;
                m_viewTerminalAttemptId=attemptId;m_lastViewSequence=restoration["headSequence"].asInt().unwrapOr(m_lastViewSequence);m_forceRestoreRequest=false;noteWorldPartial("ATTEMPT_ENDED_DURING_RESTORE");
                m_status=SpectatorLiveStatus::Waiting;m_reason=terminal->finished?"CLEAR_DURING_WORLD_RESTORE":"DEATH_DURING_WORLD_RESTORE";return;
            }
        }
    }
    if (attemptId == m_viewAttemptId && generation < m_viewGeneration) {
        setFallback("STALE_GENERATION"); return;
    }
    // A timeout/network gap does not permanently poison this generation.
    // Fresh contiguous packets may recover it, through ALL the same validators.
    // Replayed sequences/empty replies cannot revive a timed-out pose.
    if (streamChanged || resetStream) {
        m_rejectedWorldContext.clear();
        m_viewCosmetics.reset();m_viewCosmeticRevision=0;
        ++m_viewPresentationEpoch;m_viewEvidenceLogs=0;
        if (m_viewPresentationEpoch == 0) ++m_viewPresentationEpoch;
        m_viewAttemptId = attemptId;
        m_viewGeneration = generation;
        m_lastViewSequence = -1;
        m_restoreTimeline.reset();m_randomEvents.reset();m_presentationAudioReady=false;m_heldPresentation.reset();
        m_worldRestoreReason.clear();m_worldCauses.clear();m_partialSync=false;
        if(hasRestoration){
            auto const available=restoration["available"].asBool().unwrapOr(false) && historyLevel==expectedLevel && expectedLevel>0;
            m_restoreTimeline.begin({m_viewMatchId,expectedRound,expectedDeathmatch,expectedLevel,attemptId,generation,m_viewPresentationEpoch},available);
            // Native replay reconstructs supported position/time-driven paths,
            // but does not prove pointer-bearing engine state/input/RNG parity.
            if(!available)noteWorldPartial(restoration["reason"].asString().unwrapOr("WORLD_HISTORY_UNAVAILABLE"));
            else if(restoration["headSequence"].asInt().unwrapOr(0)>0)noteWorldPartial("NATIVE_HISTORY_REPLAY_LIMITED");
        }
        m_lastPublisherElapsedMs = -1;
        m_sampleBuffer.clear();
        m_lastAcceptedPose.reset();
        m_lastPacketAt = {};
        log::info(
            "[SpectatorLive] stream reset match={} context={} attempt={} generation={} rttMs={}",
            m_viewMatchId,
            m_viewContextKey,
            attemptId,
            generation,
            m_lastViewerRttMs
        );
    }
    m_forceRestoreRequest=false;
    m_opponentName = stream["opponentName"].asString().unwrapOr("Opponent");
    if(stream.contains("cosmetics")){
        auto descriptor=decodeSpectatorCosmetics(stream["cosmetics"]);
        if(!descriptor){setFallback("INVALID_COSMETIC_DESCRIPTOR");return;}
        m_viewCosmeticRevision=descriptor->revision;
        if(!m_viewCosmetics)m_viewCosmetics=std::move(descriptor);
    }

    auto const packets = root["packets"].asArray();
    if (packets.isErr()) {
        setFallback("INVALID_PACKET_ARRAY");
        return;
    }
    if(hasRestoration && restoration["available"].asBool().unwrapOr(false) && packets.unwrap().size()>64){setFallback("RESTORE_PAGE_TOO_LARGE");return;}
    auto const responseNow = std::chrono::steady_clock::now();
    auto const presentationNowMs = std::chrono::duration<double, std::milli>(responseNow.time_since_epoch()).count();
    // A reset clears buffer-local state, including its pause flag.
    m_sampleBuffer.setSourcePaused(paused, presentationNowMs);
    m_sampleBuffer.noteResponse(presentationNowMs, m_lastViewerRttMs, m_viewerPollGapMs, packets.unwrap().size());
    int appliedPackets = 0;
    std::int64_t publisherElapsedMs = 0;
    float largestCorrection = 0.0f;
    for (auto const& packet : packets.unwrap()) {
        if (!packet.isObject()) {
            setFallback("INVALID_PACKET");
            return;
        }
        if (packet["protocolVersion"].asInt().unwrapOr(0) != kProtocolVersion) {
            setFallback("UNSUPPORTED_PROTOCOL_VERSION");
            return;
        }
        if (packet["matchId"].asString().unwrapOr("") != m_viewMatchId) {
            setFallback("PACKET_MATCH_MISMATCH");
            return;
        }
        auto const packetRound = static_cast<int>(packet["roundNumber"].asInt().unwrapOr(-1));
        auto const packetDeathmatch = static_cast<int>(packet["deathmatchSequence"].asInt().unwrapOr(0));
        if (packetRound != expectedRound || packetDeathmatch != expectedDeathmatch) {
            setFallback("PACKET_CONTEXT_MISMATCH");
            return;
        }
        if (packet["attemptId"].asString().unwrapOr("") != m_viewAttemptId) {
            setFallback("PACKET_ATTEMPT_MISMATCH");
            return;
        }
        if (static_cast<std::uint64_t>(packet["generation"].asInt().unwrapOr(0)) != m_viewGeneration) {
            setFallback("STALE_GENERATION");
            return;
        }
        auto const packetLevel=packet["levelId"].asInt().unwrapOr(0);
        if(packetLevel>0 && packetLevel!=expectedLevel){setFallback("PACKET_LEVEL_MISMATCH");return;}
        auto const sequence = packet["sequence"].asInt().unwrapOr(-1);
        if (sequence <= m_lastViewSequence) continue;
        if (m_viewTerminalAttemptId == attemptId) {
            m_reason="TERMINAL_STREAM_CANNOT_RESUME"; return;
        }
        if (m_lastViewSequence >= 0 && sequence != m_lastViewSequence + 1) {
            setFallback("SEQUENCE_GAP");
            return;
        }
        auto const elapsedMs = packet["elapsedMs"].asInt().unwrapOr(-1);
        if (elapsedMs < 0 || elapsedMs < m_lastPublisherElapsedMs) {
            setFallback("INVALID_PUBLISHER_TIMELINE"); return;
        }
        auto const keyframe = packet["keyframe"];
        if (!keyframe.isObject()) {
            setFallback("MISSING_KEYFRAME");
            return;
        }
        if(keyframe["dual"].asBool().unwrapOr(false) && !keyframe["player2"].isObject()){
            setFallback("DUAL_UNSUPPORTED");return; // legacy stream lacks a complete second body
        }
        auto const decoded = decodeSpectatorPose(keyframe);
        if (!decoded) { log::warn("[SpectatorEvidence] stage=DECODE_REJECT attempt={} generation={} sequence={} elapsedMs={} reason=INVALID_POSE_OR_NATIVE_CHAIN",attemptId,generation,sequence,elapsedMs);setFallback("INVALID_POSE"); return; }
        if(expectedLevel==143308401&&elapsedMs>=70000&&elapsedMs<=73000&&m_viewEvidenceLogs++<128)
            log::info("[SpectatorEvidence] stage=RECEIVED_DECODED attempt={} generation={} epoch={} sequence={} elapsedMs={} p1={} p2={}",attemptId,generation,m_viewPresentationEpoch,sequence,elapsedMs,spectatorEvidenceDiscontinuity(keyframe),spectatorEvidenceDiscontinuity(keyframe,true));
        auto pose = *decoded;
        if(pose.randomEvents&&(!pose.simulation||!m_randomEvents.push(pose.randomEvents,pose.simulation->elapsedMs,elapsedMs))){noteWorldPartial("RANDOM_EVENT_HISTORY_INCOMPLETE");}
        if(packet.contains("cosmetics") || keyframe.contains("cosmetics")){
            auto descriptor=decodeSpectatorCosmetics(packet.contains("cosmetics")?packet["cosmetics"]:keyframe["cosmetics"]);
            if(!descriptor){setFallback("INVALID_COSMETIC_DESCRIPTOR");return;}
            m_viewCosmetics=std::move(descriptor);
        }
        pose.cosmetics=m_viewCosmetics;
        if(hasRestoration && (!pose.camera || !pose.camera->transform))noteWorldPartial("CAMERA_TRANSFORM_UNAVAILABLE");
        if(pose.dual)noteWorldPartial("DUAL_PLAYER2_TRIGGERS_UNVERIFIED");
        if (m_lastAcceptedPose && (m_lastAcceptedPose->dead || m_lastAcceptedPose->finished)) {
            setFallback("TERMINAL_STREAM_CANNOT_RESUME"); return;
        }
        bool snap = false;
        if(pose.player2)pose.player2->segment=1;
        if (m_lastAcceptedPose) {
            auto const correction = poseDistance(*m_lastAcceptedPose, pose);
            largestCorrection = std::max(largestCorrection, correction);
            snap = pose.discontinuity ? verifiedSpectatorSnap(*m_lastAcceptedPose, pose) :
                correction > kPoseDiscontinuityDistance && spectatorPoseSnapAllowed(*m_lastAcceptedPose, pose);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (!m_debugContextId.empty() && sequence >= 18 && sequence <= 22) {
                auto const& previous = *m_lastAcceptedPose;
                log::info("[DebugSpectate] playback forensic sequence={} elapsedMs={} x={} y={} deltaX={} deltaY={} distance={} previousMode={} mode={} mini={} upsideDown={} flipped={} dual={} cameraX={} cameraY={} discontinuity={}",
                    sequence, packet["elapsedMs"].asInt().unwrapOr(-1), pose.x, pose.y,
                    pose.x - previous.x, pose.y - previous.y, correction, spectatorModeName(previous.mode),
                    spectatorModeName(pose.mode), pose.mini, pose.upsideDown, pose.flipped, pose.dual,
                    pose.camera ? pose.camera->x : 0, pose.camera ? pose.camera->y : 0,
                    pose.discontinuity ? spectatorDiscontinuityName(pose.discontinuity->type) : "NONE");
            }
#endif
            if ((pose.discontinuity && !snap) || (correction > kPoseDiscontinuityDistance && !snap)) {
                log::warn(
                    "[SpectatorLive] correction rejected match={} attempt={} generation={} sequence={} distance={}",
                    m_viewMatchId,
                    m_viewAttemptId,
                    m_viewGeneration,
                    sequence,
                    correction
                );
                log::warn("[SpectatorNativeEvent] rejected sequence={} previousMode={} nextMode={} previousGravity={} nextGravity={} type={} fromX={} fromY={} toX={} toY={} nativeEvents={} recaptureRequired=true",sequence,spectatorModeName(m_lastAcceptedPose->mode),spectatorModeName(pose.mode),m_lastAcceptedPose->upsideDown,pose.upsideDown,pose.discontinuity?spectatorDiscontinuityName(pose.discontinuity->type):"NONE",pose.discontinuity?pose.discontinuity->fromX:0,pose.discontinuity?pose.discontinuity->fromY:0,pose.discontinuity?pose.discontinuity->toX:0,pose.discontinuity?pose.discontinuity->toY:0,pose.discontinuity?pose.discontinuity->nativeEvents.size():0);
                log::warn("[SpectatorNativeEvent] context={} epoch={} previousSequence={} nextSequence={} previousPublisherMs={} nextPublisherMs={} previousX={} previousY={} nextX={} nextY={} nativeTick={} sourceMs={} randomEvents={}",worldIdentity,m_viewPresentationEpoch,m_lastViewSequence,sequence,m_lastPublisherElapsedMs,elapsedMs,m_lastAcceptedPose->x,m_lastAcceptedPose->y,pose.x,pose.y,pose.simulation?pose.simulation->tick:-1,pose.simulation?pose.simulation->elapsedMs:-1,pose.randomEvents?pose.randomEvents->size():0);
                setFallback("POSE_DISCONTINUITY");
                return;
            }
            if(pose.dual && m_lastAcceptedPose->dual && pose.player2 && m_lastAcceptedPose->player2){
                auto const previous=spectatorSecondaryAsPose(*m_lastAcceptedPose->player2),next=spectatorSecondaryAsPose(*pose.player2);
                auto const distance=poseDistance(previous,next);
                auto const secondarySnap=next.discontinuity?verifiedSpectatorSnap(previous,next):distance>kPoseDiscontinuityDistance&&spectatorPoseSnapAllowed(previous,next);
                if((next.discontinuity&&!secondarySnap)||(distance>kPoseDiscontinuityDistance&&!secondarySnap)){
                    setFallback("PLAYER2_POSE_DISCONTINUITY");return;
                }
                // P2 has its own visual boundary. Never reset P1 native trigger
                // history merely because the other avatar snapped.
                pose.player2->segment=m_lastAcceptedPose->player2->segment+(secondarySnap?1:0);
            }
            if (snap) {
                log::info("[SpectatorLive] verified discontinuity type={}",
                    pose.discontinuity ? spectatorDiscontinuityName(pose.discontinuity->type) : "BOUNDED_STATE_TRANSITION");
                log::debug("[SpectatorLive] timeline snap boundary retained sequence={} elapsedMs={}", sequence, elapsedMs);
            }
        }
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (!m_debugContextId.empty() && !m_lastAcceptedPose && m_lastViewSequence < 0)
            log::info("[SpectatorLive] first normalized packet sequence={} elapsedMs={}",
                sequence, packet["elapsedMs"].asInt().unwrapOr(-1));
#endif
        m_lastAcceptedPose = pose;
        if (pose.dead || pose.finished) m_viewTerminalAttemptId = attemptId;
        auto const stored=m_restoreTimeline.active()
            ? m_restoreTimeline.push(sequence,static_cast<double>(elapsedMs),pose,snap)
            : m_sampleBuffer.push({sequence, static_cast<double>(elapsedMs), pose}, snap, presentationNowMs);
        if (!stored) {
            setFallback("PRESENTATION_BUFFER_OVERFLOW"); return;
        }
        m_lastPublisherElapsedMs = elapsedMs;
        m_lastPacketAt = std::chrono::steady_clock::now();
        m_lastViewSequence = sequence;
        publisherElapsedMs = packet["elapsedMs"].asInt().unwrapOr(publisherElapsedMs);
        ++appliedPackets;
    }
    if(hasRestoration){
        auto const head=restoration["headSequence"].asInt().unwrapOr(-1);
        if(m_lastViewSequence>head || restoration["caughtUp"].asBool().unwrapOr(false)!=(m_lastViewSequence>=head)){
            setFallback("RESTORE_CURSOR_MISMATCH");return;
        }
        if(m_restoreTimeline.active()){
            m_restoreTimeline.networkCaughtUp(restoration["caughtUp"].asBool().unwrapOr(false));
            if(!m_restoreTimeline.active())if(auto frame=m_restoreTimeline.frame();frame&&frame->pose){m_sampleBuffer.clear();m_sampleBuffer.push({frame->sequence,frame->publisherElapsedMs,*frame->pose,frame->presentationSegment},false,presentationNowMs);m_heldPresentation=frame;}
        }
    }
    if(appliedPackets>0)m_restoreFailures=0;
    if (appliedPackets > 0 && (streamChanged || resetStream || m_status != SpectatorLiveStatus::Live)) {
        log::debug(
            "[SpectatorLive] applied match={} context={} attempt={} generation={} packets={} lastSequence={} publisherElapsedMs={} rttMs={} maxCorrection={}",
            m_viewMatchId,
            m_viewContextKey,
            m_viewAttemptId,
            m_viewGeneration,
            appliedPackets,
            m_lastViewSequence,
            publisherElapsedMs,
            m_lastViewerRttMs,
            largestCorrection
        );
    }
    if (appliedPackets==0 && m_status == SpectatorLiveStatus::Fallback && !streamChanged && !resetStream) return;
    m_status = m_lastAcceptedPose ? SpectatorLiveStatus::Live : SpectatorLiveStatus::Waiting;
    m_reason = m_partialSync ? (!m_worldRestoreReason.empty()?m_worldRestoreReason:restoration["reason"].asString().unwrapOr("NATIVE_HISTORY_REPLAY_LIMITED")) :
        m_lastAcceptedPose ? std::string() : "STREAM_BUFFER_EMPTY";
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (responseNow >= m_nextBufferMetricAt) {
        m_nextBufferMetricAt = responseNow + std::chrono::seconds(1);
        log::info("[DebugSpectate] buffer samples={} span={}ms delay={}ms rtt={}ms arrival={}ms packetsPerResponse={} renderPublisherElapsedMs={} latestPublisherElapsedMs={} starvation={} pollGapMs={} responseBytes={} poseBytes={}",
            m_sampleBuffer.size(), m_sampleBuffer.spanMs(), m_sampleBuffer.delayMs(), m_sampleBuffer.averageRttMs(),
            m_sampleBuffer.averageArrivalMs(), m_sampleBuffer.packetsPerResponse(), m_sampleBuffer.renderTimeMs(),
            m_sampleBuffer.latestTimeMs(), m_sampleBuffer.starvations(), m_viewerPollGapMs,root.dump().size(),sizeof(SpectatorGhostPose));
        log::info("[SpectatorRestore] attempt={} generation={} epoch={} sequence={} queued={} applied={} syncing={} partial={} reconnects={} reason={}",
            m_viewAttemptId,m_viewGeneration,m_viewPresentationEpoch,m_lastViewSequence,m_restoreTimeline.queued(),m_restoreTimeline.appliedCount(),m_restoreTimeline.active(),m_partialSync,m_reconnections,m_reason);
    }
#endif
}

void SpectatorLiveRuntime::setFallback(std::string reason) {
    if((reason=="POSE_DISCONTINUITY"||reason=="PLAYER2_POSE_DISCONTINUITY"||reason=="INVALID_POSE")&&!m_viewAttemptId.empty()){
        // Re-fetching an immutable invalid event cannot repair its proof. Hold
        // safely and keep checking for an authoritative NEW Attempt, without
        // repeatedly resetting the same world/epoch back to 0%.
        m_rejectedWorldContext=m_viewAttemptId+":"+std::to_string(m_viewGeneration);m_restoreTimeline.reset();m_randomEvents.reset();
        m_status=SpectatorLiveStatus::Fallback;m_reason=std::move(reason);m_forceRestoreRequest=false;return;
    }
    if (m_status != SpectatorLiveStatus::Fallback || m_reason != reason) {
        log::warn(
            "[SpectatorLive] fallback match={} context={} attempt={} generation={} lastSequence={} reason={} rttMs={}",
            m_viewMatchId,
            m_viewContextKey,
            m_viewAttemptId,
            m_viewGeneration,
            m_lastViewSequence,
            reason,
            m_lastViewerRttMs
        );
    }
    m_status = SpectatorLiveStatus::Fallback;
    m_reason = std::move(reason);
    // Hold the last validated presentation. No extrapolation or origin reset.
    if(!m_forceRestoreRequest)++m_reconnections;
    m_forceRestoreRequest=true;
    if(++m_restoreFailures>=12 || m_reason=="DUAL_UNSUPPORTED" || m_reason=="UNSUPPORTED_PROTOCOL_VERSION"){
        m_rejectedWorldContext=m_viewAttemptId+":"+std::to_string(m_viewGeneration);m_forceRestoreRequest=false;m_restoreTimeline.reset();
    }
}

void SpectatorLiveRuntime::requestSceneRestore(){
    m_disconnected=false;m_consecutiveViewerErrors=0;m_restoreFailures=0;
    m_reason="SCENE_RESTORE_REQUESTED";
    m_forceRestoreRequest=true;m_nextViewerPollAt={};
}
std::vector<SpectatorRemoteFrame> SpectatorLiveRuntime::takeRestorationFrames(){
    if(m_forceRestoreRequest || !presentationFrame())return {};
    return m_restoreTimeline.takeBatch(static_cast<std::size_t>(std::clamp(2.0/std::max(.05,m_restoreStepCostMs),1.0,8.0)));
}
void SpectatorLiveRuntime::restorationApplied(SpectatorRemoteFrame const& frame,bool clockAdvanced,double workMs){
    if(std::isfinite(workMs) && workMs>0)m_restoreStepCostMs=m_restoreStepCostMs*.75+workMs*.25;
    auto const wasActive=m_restoreTimeline.active();
    if(!m_restoreTimeline.applied(frame))return;
    if(!clockAdvanced)noteWorldPartial("NATIVE_WORLD_CLOCK_HELD");
    if(wasActive && !m_restoreTimeline.active()){
        auto const now=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        m_sampleBuffer.clear();
        if(frame.pose)m_sampleBuffer.push({frame.sequence,frame.publisherElapsedMs,*frame.pose,frame.presentationSegment},false,now);
    }
}

void SpectatorLiveRuntime::noteWorldPartial(std::string reason){
    if(m_worldCauses.insert(reason).second)log::warn("[SpectatorWorldState] attemptId={} generation={} epoch={} cause={} transition=DEGRADED",m_viewAttemptId,m_viewGeneration,m_viewPresentationEpoch,reason);
    m_partialSync=true;m_worldRestoreReason=std::move(reason);m_reason=m_worldRestoreReason;
}

void SpectatorLiveRuntime::worldCauseRecovered(std::string const& reason){
    if(!m_worldCauses.erase(reason))return;
    log::info("[SpectatorWorldState] attemptId={} generation={} epoch={} cause={} transition=VERIFIED",m_viewAttemptId,m_viewGeneration,m_viewPresentationEpoch,reason);
    // History/RNG/Follow parity cannot be proved by a later valid sample.
    // Only remove the explicit transient cause; retain preexisting history debt.
    if(m_worldRestoreReason==reason){m_worldRestoreReason=m_worldCauses.empty()?"":*m_worldCauses.begin();m_reason=m_worldRestoreReason;}
    m_partialSync=!m_worldCauses.empty();
}
std::optional<SpectatorGhostPose> SpectatorLiveRuntime::interpolatedPose() const {
    auto const nowMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto const sample = m_sampleBuffer.render(nowMs);
    return sample ? std::optional(sample->pose) : std::nullopt;
}

std::optional<SpectatorRemoteFrame> SpectatorLiveRuntime::presentationFrame() const {
    auto frame=worldFrame();
    if(frame&&restoringWorld())frame->pose.reset();
    return frame;
}
std::optional<SpectatorRemoteFrame> SpectatorLiveRuntime::worldFrame() const {
    if(!m_rejectedWorldContext.empty())return m_heldPresentation;
    auto const nowMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto const sample = m_sampleBuffer.render(nowMs);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) {
        if (!debug::DebugSpectateRuntime::get().playbackLevel(debug::DebugSpectateRuntime::get().levelId()) ||
            m_viewMatchId != m_debugContextId ||
            (m_status != SpectatorLiveStatus::Waiting && m_status != SpectatorLiveStatus::Live &&
             m_status != SpectatorLiveStatus::Fallback)) return std::nullopt;
        if(m_forceRestoreRequest)return m_heldPresentation;
        if(m_restoreTimeline.active())return m_restoreTimeline.frame();
        auto frame=SpectatorRemoteFrame {
            .context = { .matchId = m_viewMatchId, .roundNumber = 1, .deathmatchSequence = 0,
                .levelId = debug::DebugSpectateRuntime::get().levelId(), .attemptId = m_viewAttemptId,
                .generation = m_viewGeneration, .epoch = m_viewPresentationEpoch },
            .sequence = sample ? sample->sequence : m_lastViewSequence,
            .pose = sample ? std::optional(sample->pose) : std::nullopt,
            .publisherElapsedMs = sample ? sample->publisherElapsedMs : 0,
            .presentationSegment = sample ? sample->segment : 0,
        };
        if(frame.pose){frame.pose->randomEvents=m_randomEvents.through(frame.publisherElapsedMs);m_heldPresentation=frame;}
        return frame;
    }
#endif
    if(m_external){
        if(!m_external->viewer || m_viewContextKey!=m_external->key)return std::nullopt;
        if(m_forceRestoreRequest)return m_heldPresentation;
        if(m_restoreTimeline.active())return m_restoreTimeline.frame();
        if(!sample)return m_heldPresentation;
        auto frame=SpectatorRemoteFrame{.context={m_viewMatchId,m_external->round,0,m_external->level,m_viewAttemptId,m_viewGeneration,m_viewPresentationEpoch},
            .sequence=sample->sequence,.pose=sample->pose,.publisherElapsedMs=sample->publisherElapsedMs,.presentationSegment=sample->segment};
        m_heldPresentation=frame;return frame;
    }
    auto& ranked = RankedRuntime::get();
    auto const& match = ranked.view().match;
    if (ranked.view().stage != RuntimeStage::Matched || ranked.isPlacement() ||
        !ranked.isSpectating() || !match.spectatorLiveEnabled || ranked.hasLocalAttemptInFlight() || !match.currentMap || match.matchId.empty() ||
        m_viewMatchId != match.matchId || m_viewContextKey != viewerContextKey(match) ||
        (m_status != SpectatorLiveStatus::Waiting && m_status != SpectatorLiveStatus::Live &&
         m_status != SpectatorLiveStatus::Fallback)) {
        return std::nullopt;
    }
    if(m_forceRestoreRequest)return m_heldPresentation;
    if(m_restoreTimeline.active())return m_restoreTimeline.frame();
    if(!sample && m_heldPresentation && m_heldPresentation->context.attemptId==m_viewAttemptId &&
       m_heldPresentation->context.generation==m_viewGeneration && m_heldPresentation->context.epoch==m_viewPresentationEpoch)return m_heldPresentation;
    auto frame=SpectatorRemoteFrame {
        .context = {
            .matchId = m_viewMatchId,
            .roundNumber = match.roundNumber,
            .deathmatchSequence = match.deathmatchSequence,
            .levelId = match.currentMap->levelId,
            .attemptId = m_viewAttemptId,
            .generation = m_viewGeneration,
            .epoch = m_viewPresentationEpoch,
        },
        .sequence = sample ? sample->sequence : m_lastViewSequence,
        .pose = sample ? std::optional(sample->pose) : std::nullopt,
        .publisherElapsedMs = sample ? sample->publisherElapsedMs : 0,
        .presentationSegment = sample ? sample->segment : 0,
    };
    if(frame.pose)m_heldPresentation=frame;
    return frame;
}

} // namespace corum::ranked
