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
constexpr std::string_view kDefaultRankedServerURL = "https://corum-ranked.onrender.com";

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

SpectatorLiveStatus SpectatorLiveRuntime::status() const {
    return m_status == SpectatorLiveStatus::Live && m_sampleBuffer.buffering()
        ? SpectatorLiveStatus::Buffering : m_status;
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
    ++m_publishRequestGeneration;
    m_publishBusy = false;
    m_publishMatchId.clear();
    m_publishAttemptId.clear();
    m_publishSequence = 0;
    m_publishQueue = {}; m_publishBatchBody = {}; m_publishBatchLastSequence = -1;
    m_publisherPaused = false; m_publisherPausedAt = {}; m_publisherPausedMs = 0;
    m_publishStartedAt = {};
    m_nextPublishAt = {};
}

void SpectatorLiveRuntime::resetViewer() {
    m_viewOpponentPaused = false;
    m_viewerPollGapMs = 20;
    ++m_viewRequestGeneration;
    ++m_viewPresentationEpoch;
    if (m_viewPresentationEpoch == 0) ++m_viewPresentationEpoch;
    m_viewBusy = false;
    m_viewMatchId.clear();
    m_viewContextKey.clear();
    m_viewAttemptId.clear();
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
    ++m_terminalRequestGeneration;
    m_terminalPending = m_terminalBusy = false;
    m_deferredTerminalPose.reset(); m_deferredMatchId.clear();
    m_terminalBody = matjson::Value();
    m_terminalSessionToken.clear(); m_terminalMatchToken.clear(); m_terminalPublishAttemptId.clear();
    resetPublisher();
    resetViewer();
}
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
void SpectatorLiveRuntime::startDebugViewer(std::string contextId, std::string token, std::string path) {
    reset();
    m_debugContextId = std::move(contextId); m_debugToken = std::move(token); m_debugPath = std::move(path);
    m_status = SpectatorLiveStatus::Waiting;
}
void SpectatorLiveRuntime::stopDebugViewer() {
    m_debugContextId.clear(); m_debugToken.clear(); m_debugPath.clear(); reset();
}
#endif

void SpectatorLiveRuntime::startPublisherGeneration(
    std::string const& matchId,
    std::string const& attemptId
) {
    m_publishMatchId = matchId;
    m_publishAttemptId = attemptId;
    auto const& match = RankedRuntime::get().view().match;
    m_publishRound = match.roundNumber;
    m_publishDeathmatch = match.state == "DEATHMATCH_PLAYING" ? match.deathmatchSequence : 0;
    ++m_publishGeneration;
    if (m_publishGeneration == 0) m_publishGeneration = 1;
    m_publishSequence = 0;
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
    auto const attemptId = ranked.spectatorLiveAttemptId();
    auto const eligible =
        view.stage == RuntimeStage::Matched &&
        !ranked.isPlacement() &&
        !ranked.isSpectating() && match.spectatorLiveEnabled &&
        !practice &&
        !match.matchId.empty() &&
        !attemptId.empty() &&
        finitePose(pose);
    if (!eligible) {
        if (!m_terminalPending) resetPublisher();
        return false;
    }
    if (m_publishMatchId != match.matchId || m_publishAttemptId != attemptId) {
        resetPublisher();
        startPublisherGeneration(match.matchId, attemptId);
    }
    if (attemptId == m_terminalPublishAttemptId) return false;
    auto const now = std::chrono::steady_clock::now();
    if (m_publishQueue.full()) {
        resetPublisher(); startPublisherGeneration(match.matchId, attemptId);
    }
    auto const captured = !m_publisherPaused && m_publishQueue.capture(publisherElapsedMs(), pose);
    m_publishSequence = static_cast<std::uint64_t>(m_publishQueue.nextSequence());
    if (!m_publishBusy && now >= m_nextPublishAt) publish(pose);
    // Consumed native evidence is tied to capture, never to HTTP request/ACK.
    return captured;
}

void SpectatorLiveRuntime::publishTerminal(SpectatorGhostPose const& pose, bool practice) {
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
    if (match.state == "DEATHMATCH_PLAYING") m_terminalBody["deathmatchSequence"] = match.deathmatchSequence;
    m_terminalBody["attemptId"] = attempt;
    m_terminalBody["generation"] = static_cast<std::int64_t>(m_publishGeneration);
    // Capture the terminal immediately, but wait for the preceding live ACK to
    // preserve contiguous sequence delivery. It bypasses the normal 50ms delay.
    m_terminalBody["sequence"] = static_cast<std::int64_t>(m_publishQueue.nextSequence());
    m_terminalBody["elapsedMs"] = static_cast<std::int64_t>(std::max<std::int64_t>(0,
        publisherElapsedMs()));
    m_terminalBody["elapsedMs"] = std::max<std::int64_t>(
        m_terminalBody["elapsedMs"].asInt().unwrapOr(0), m_publishQueue.lastElapsedMs() + 1);
    m_terminalBody["keyframe"] = encodeSpectatorPose(pose);
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
    m_terminalRequest.spawn(request.post(endpoint("/api/ranked/matches/" + matchId + "/spectator-live/publish")),
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
    auto const session = m_terminalPending ? m_terminalSessionToken : ranked.spectatorLiveSessionToken();
    auto const token = m_terminalPending ? m_terminalMatchToken : ranked.spectatorLiveMatchToken();
    if (session.empty() || token.empty()) return;
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
            packets.push(packet);
        }
        m_publishBatchBody = matjson::Value(); m_publishBatchBody["packets"] = packets;
        m_publishBatchLastSequence = batch.back().sequence;
    }
    auto request = spectatorRequest(session, token); request.bodyJSON(m_publishBatchBody);
    auto const requestGeneration = ++m_publishRequestGeneration;
    auto const matchId = m_publishMatchId, attemptId = m_publishAttemptId;
    auto const lastSequence = m_publishBatchLastSequence;
    m_publishBusy = true;
    m_publishRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + matchId + "/spectator-live/publish")),
        [this, requestGeneration, matchId, attemptId, lastSequence](web::WebResponse response) {
            if (requestGeneration != m_publishRequestGeneration || matchId != m_publishMatchId ||
                attemptId != m_publishAttemptId) return;
            m_publishBusy = false;
            if (successful(response)) {
                m_publishQueue.acknowledge(lastSequence);
                m_publishBatchBody = {}; m_publishBatchLastSequence = -1;
                m_nextPublishAt = std::chrono::steady_clock::now(); // no extra ACK-bound 50ms wait
                return;
            }
            m_nextPublishAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            if (response.code() == 409) {
                ++m_publishGeneration; if (!m_publishGeneration) m_publishGeneration = 1;
                m_publishQueue = {}; m_publishBatchBody = {}; m_publishBatchLastSequence = -1;
                m_publishSequence = 0; m_publishStartedAt = std::chrono::steady_clock::now();
                m_publisherPausedMs = 0;
                if (m_publisherPaused) m_publisherPausedAt = m_publishStartedAt;
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
        ranked.isSpectating() && match.spectatorLiveEnabled && !match.matchId.empty() &&
        !ranked.spectatorLiveSessionToken().empty() && !ranked.spectatorLiveMatchToken().empty();
    auto matchId = match.matchId;
    auto contextKey = viewerContextKey(match);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) {
        eligible = debug::DebugSpectateRuntime::get().playbackLevel(debug::kDebugSpectateLevelId);
        matchId = m_debugContextId;
        contextKey = fmt::format("{}:R1:D0:L{}", matchId, debug::kDebugSpectateLevelId);
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
    if (m_viewBusy || now < m_nextViewerPollAt) return;
    pollViewer();
}

void SpectatorLiveRuntime::pollViewer() {
    if (m_viewBusy) return;
    auto& ranked = RankedRuntime::get();
    auto matchId = ranked.view().match.matchId;
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
    if (!m_viewAttemptId.empty()) path += "&attemptId=" + m_viewAttemptId;
    if (m_viewGeneration > 0) path += fmt::format("&generation={}", m_viewGeneration);
    if (m_lastViewSequence >= 0) path += fmt::format("&afterSequence={}", m_lastViewSequence);

    auto const requestGeneration = ++m_viewRequestGeneration;
    auto const contextKey = m_viewContextKey;
    auto const requestStartedAt = std::chrono::steady_clock::now();
    m_viewBusy = true;
    m_viewRequest.spawn(
        request.get(endpoint(path)),
        [this, requestGeneration, matchId, contextKey, requestStartedAt](web::WebResponse response) {
            auto currentContextKey = viewerContextKey(RankedRuntime::get().view().match);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (!m_debugContextId.empty()) currentContextKey = fmt::format("{}:R1:D0:L{}", m_debugContextId, debug::kDebugSpectateLevelId);
#endif
            if (
                requestGeneration != m_viewRequestGeneration ||
                matchId != m_viewMatchId || contextKey != m_viewContextKey ||
                contextKey != currentContextKey
            ) return;
            m_viewBusy = false;
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
                if (m_consecutiveViewerErrors >= 3) setFallback("NETWORK_ERROR");
                m_viewerPollGapMs = spectatorViewerPollGapMs(false, true, false, false, 0, 0, 0, m_consecutiveViewerErrors);
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
                m_sampleBuffer.starving(), m_sampleBuffer.terminalRendered(),
                m_sampleBuffer.latestTimeMs() - m_sampleBuffer.renderTimeMs(),
                m_sampleBuffer.delayMs(), m_lastViewerRttMs, 0);
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
        m_status = SpectatorLiveStatus::Waiting;
        m_reason = reason;
        // A transient empty relay response hides presentation without allocating
        // a new Remote Player. Only an explicit stream/context reset changes epoch.
        m_sampleBuffer.clear();
        m_lastAcceptedPose.reset();
        m_lastPacketAt = {};
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
    auto expectedRound = authoritativeMatch.roundNumber;
    auto expectedDeathmatch = authoritativeMatch.deathmatchSequence;
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
    auto const resetStream = root["reset"].asBool().unwrapOr(false);
    auto const streamChanged = attemptId != m_viewAttemptId || generation != m_viewGeneration;
    if (attemptId == m_viewAttemptId && generation < m_viewGeneration) {
        setFallback("STALE_GENERATION"); return;
    }
    // A timeout/network gap does not permanently poison this generation.
    // Fresh contiguous packets may recover it, through ALL the same validators.
    // Replayed sequences/empty replies cannot revive a timed-out pose.
    if (streamChanged || resetStream) {
        ++m_viewPresentationEpoch;
        if (m_viewPresentationEpoch == 0) ++m_viewPresentationEpoch;
        m_viewAttemptId = attemptId;
        m_viewGeneration = generation;
        m_lastViewSequence = -1;
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
    m_opponentName = stream["opponentName"].asString().unwrapOr("Opponent");

    auto const packets = root["packets"].asArray();
    if (packets.isErr()) {
        setFallback("INVALID_PACKET_ARRAY");
        return;
    }
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
        auto const sequence = packet["sequence"].asInt().unwrapOr(-1);
        if (sequence <= m_lastViewSequence) continue;
        if (m_viewTerminalAttemptId == attemptId) {
            setFallback("TERMINAL_STREAM_CANNOT_RESUME"); return;
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
        auto const decoded = decodeSpectatorPose(keyframe);
        if (!decoded) { setFallback("INVALID_POSE"); return; }
        auto const pose = *decoded;
        if (pose.dual) { setFallback("DUAL_UNSUPPORTED"); return; }
        if (m_lastAcceptedPose && (m_lastAcceptedPose->dead || m_lastAcceptedPose->finished)) {
            setFallback("TERMINAL_STREAM_CANNOT_RESUME"); return;
        }
        bool snap = false;
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
                setFallback("POSE_DISCONTINUITY");
                return;
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
        if (!m_sampleBuffer.push({sequence, static_cast<double>(elapsedMs), pose}, snap, presentationNowMs)) {
            setFallback("PRESENTATION_BUFFER_OVERFLOW"); return;
        }
        m_lastPublisherElapsedMs = elapsedMs;
        m_lastPacketAt = std::chrono::steady_clock::now();
        m_lastViewSequence = sequence;
        publisherElapsedMs = packet["elapsedMs"].asInt().unwrapOr(publisherElapsedMs);
        ++appliedPackets;
    }
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
    if (!m_lastAcceptedPose && m_status == SpectatorLiveStatus::Fallback && !streamChanged && !resetStream) return;
    m_status = m_lastAcceptedPose ? SpectatorLiveStatus::Live : SpectatorLiveStatus::Waiting;
    m_reason = m_lastAcceptedPose ? std::string() : "STREAM_BUFFER_EMPTY";
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty() && responseNow >= m_nextBufferMetricAt) {
        m_nextBufferMetricAt = responseNow + std::chrono::seconds(1);
        log::info("[DebugSpectate] buffer samples={} span={}ms delay={}ms rtt={}ms arrival={}ms packetsPerResponse={} renderPublisherElapsedMs={} latestPublisherElapsedMs={} starvation={} pollGapMs={}",
            m_sampleBuffer.size(), m_sampleBuffer.spanMs(), m_sampleBuffer.delayMs(), m_sampleBuffer.averageRttMs(),
            m_sampleBuffer.averageArrivalMs(), m_sampleBuffer.packetsPerResponse(), m_sampleBuffer.renderTimeMs(),
            m_sampleBuffer.latestTimeMs(), m_sampleBuffer.starvations(), m_viewerPollGapMs);
    }
#endif
}

void SpectatorLiveRuntime::setFallback(std::string reason) {
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
    m_sampleBuffer.clear();
    m_lastAcceptedPose.reset();
}

std::optional<SpectatorGhostPose> SpectatorLiveRuntime::interpolatedPose() const {
    auto const nowMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto const sample = m_sampleBuffer.render(nowMs);
    return sample ? std::optional(sample->pose) : std::nullopt;
}

std::optional<SpectatorRemoteFrame> SpectatorLiveRuntime::presentationFrame() const {
    auto const nowMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto const sample = m_sampleBuffer.render(nowMs);
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    if (!m_debugContextId.empty()) {
        if (!debug::DebugSpectateRuntime::get().playbackLevel(debug::kDebugSpectateLevelId) ||
            m_viewMatchId != m_debugContextId ||
            (m_status != SpectatorLiveStatus::Waiting && m_status != SpectatorLiveStatus::Live &&
             m_status != SpectatorLiveStatus::Fallback)) return std::nullopt;
        return SpectatorRemoteFrame {
            .context = { .matchId = m_viewMatchId, .roundNumber = 1, .deathmatchSequence = 0,
                .levelId = debug::kDebugSpectateLevelId, .attemptId = m_viewAttemptId,
                .generation = m_viewGeneration, .epoch = m_viewPresentationEpoch },
            .sequence = sample ? sample->sequence : m_lastViewSequence,
            .pose = sample ? std::optional(sample->pose) : std::nullopt,
            .publisherElapsedMs = sample ? sample->publisherElapsedMs : 0,
            .presentationSegment = sample ? sample->segment : 0,
        };
    }
#endif
    auto& ranked = RankedRuntime::get();
    auto const& match = ranked.view().match;
    if (ranked.view().stage != RuntimeStage::Matched || ranked.isPlacement() ||
        !ranked.isSpectating() || !match.currentMap || match.matchId.empty() ||
        m_viewMatchId != match.matchId || m_viewContextKey != viewerContextKey(match) ||
        (m_status != SpectatorLiveStatus::Waiting && m_status != SpectatorLiveStatus::Live &&
         m_status != SpectatorLiveStatus::Fallback)) {
        return std::nullopt;
    }
    return SpectatorRemoteFrame {
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
}

} // namespace corum::ranked
