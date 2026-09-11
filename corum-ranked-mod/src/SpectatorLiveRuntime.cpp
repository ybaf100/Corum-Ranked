#include "SpectatorLiveRuntime.hpp"
#include "RankedRuntime.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace {

constexpr int kProtocolVersion = 1;
constexpr auto kPublishInterval = std::chrono::milliseconds(50);
constexpr auto kViewerPollInterval = std::chrono::milliseconds(80);
constexpr auto kFallbackRepollInterval = std::chrono::milliseconds(150);
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
    return
        std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.rotation) &&
        std::abs(pose.x) <= 10'000'000.0f && std::abs(pose.y) <= 10'000'000.0f;
}

float poseDistance(
    corum::ranked::SpectatorGhostPose const& left,
    corum::ranked::SpectatorGhostPose const& right
) {
    auto const dx = right.x - left.x;
    auto const dy = right.y - left.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

namespace corum::ranked {

SpectatorLiveRuntime& SpectatorLiveRuntime::get() {
    static SpectatorLiveRuntime runtime;
    return runtime;
}

SpectatorLiveStatus SpectatorLiveRuntime::status() const {
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
    ++m_publishRequestGeneration;
    m_publishBusy = false;
    m_publishMatchId.clear();
    m_publishAttemptId.clear();
    m_publishSequence = 0;
    m_publishStartedAt = {};
    m_nextPublishAt = {};
}

void SpectatorLiveRuntime::resetViewer() {
    ++m_viewRequestGeneration;
    m_viewBusy = false;
    m_viewMatchId.clear();
    m_viewAttemptId.clear();
    m_viewGeneration = 0;
    m_lastViewSequence = -1;
    m_opponentName.clear();
    m_reason.clear();
    m_status = SpectatorLiveStatus::Idle;
    m_previousPose.reset();
    m_currentPose.reset();
    m_poseReceivedAt = {};
    m_lastPacketAt = {};
    m_nextViewerPollAt = {};
    m_consecutiveViewerErrors = 0;
}

void SpectatorLiveRuntime::reset() {
    resetPublisher();
    resetViewer();
}

void SpectatorLiveRuntime::startPublisherGeneration(
    std::string const& matchId,
    std::string const& attemptId
) {
    m_publishMatchId = matchId;
    m_publishAttemptId = attemptId;
    ++m_publishGeneration;
    if (m_publishGeneration == 0) m_publishGeneration = 1;
    m_publishSequence = 0;
    m_publishStartedAt = std::chrono::steady_clock::now();
    m_nextPublishAt = m_publishStartedAt;
}

void SpectatorLiveRuntime::tickPublisher(SpectatorGhostPose const& pose, bool practice) {
    auto& ranked = RankedRuntime::get();
    auto const& view = ranked.view();
    auto const& match = view.match;
    auto const attemptId = ranked.spectatorLiveAttemptId();
    auto const eligible =
        view.stage == RuntimeStage::Matched &&
        !ranked.isPlacement() &&
        !ranked.isSpectating() &&
        !practice &&
        !match.matchId.empty() &&
        !attemptId.empty() &&
        finitePose(pose);
    if (!eligible) {
        resetPublisher();
        return;
    }
    if (m_publishMatchId != match.matchId || m_publishAttemptId != attemptId) {
        resetPublisher();
        startPublisherGeneration(match.matchId, attemptId);
    }
    auto const now = std::chrono::steady_clock::now();
    if (m_publishBusy || now < m_nextPublishAt) return;
    publish(pose);
}

void SpectatorLiveRuntime::publish(SpectatorGhostPose const& pose) {
    auto& ranked = RankedRuntime::get();
    auto const& match = ranked.view().match;
    if (
        m_publishMatchId.empty() || m_publishAttemptId.empty() ||
        ranked.spectatorLiveSessionToken().empty() || ranked.spectatorLiveMatchToken().empty()
    ) return;

    auto const now = std::chrono::steady_clock::now();
    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_publishStartedAt
    ).count();
    matjson::Value body;
    body["protocolVersion"] = kProtocolVersion;
    body["matchId"] = m_publishMatchId;
    body["roundNumber"] = match.roundNumber;
    if (match.state == "DEATHMATCH_PLAYING" && match.deathmatchSequence > 0) {
        body["deathmatchSequence"] = match.deathmatchSequence;
    }
    body["attemptId"] = m_publishAttemptId;
    body["generation"] = static_cast<std::int64_t>(m_publishGeneration);
    body["sequence"] = static_cast<std::int64_t>(m_publishSequence);
    body["elapsedMs"] = static_cast<std::int64_t>(std::max<std::int64_t>(0, elapsedMs));
    matjson::Value keyframe;
    keyframe["x"] = static_cast<double>(pose.x);
    keyframe["y"] = static_cast<double>(pose.y);
    keyframe["rotation"] = static_cast<double>(pose.rotation);
    keyframe["dead"] = pose.dead;
    keyframe["finished"] = pose.finished;
    body["keyframe"] = keyframe;

    auto request = spectatorRequest(
        ranked.spectatorLiveSessionToken(),
        ranked.spectatorLiveMatchToken()
    );
    request.bodyJSON(body);
    auto const requestGeneration = ++m_publishRequestGeneration;
    auto const matchId = m_publishMatchId;
    auto const attemptId = m_publishAttemptId;
    auto const sequence = m_publishSequence;
    m_publishBusy = true;
    m_publishRequest.spawn(
        request.post(endpoint("/api/ranked/matches/" + matchId + "/spectator-live/publish")),
        [this, requestGeneration, matchId, attemptId, sequence](web::WebResponse response) {
            if (
                requestGeneration != m_publishRequestGeneration ||
                matchId != m_publishMatchId || attemptId != m_publishAttemptId
            ) return;
            m_publishBusy = false;
            m_nextPublishAt = std::chrono::steady_clock::now() + kPublishInterval;
            if (successful(response)) {
                if (sequence == m_publishSequence) ++m_publishSequence;
                return;
            }
            if (response.code() == 409) {
                ++m_publishGeneration;
                if (m_publishGeneration == 0) m_publishGeneration = 1;
                m_publishSequence = 0;
                m_publishStartedAt = std::chrono::steady_clock::now();
                m_nextPublishAt = m_publishStartedAt;
                log::debug("Restarting spectator relay generation after HTTP 409");
            }
        }
    );
}

void SpectatorLiveRuntime::tickViewer() {
    auto& ranked = RankedRuntime::get();
    auto const& view = ranked.view();
    auto const& match = view.match;
    if (
        view.stage != RuntimeStage::Matched || ranked.isPlacement() ||
        !ranked.isSpectating() || match.matchId.empty() ||
        ranked.spectatorLiveSessionToken().empty() || ranked.spectatorLiveMatchToken().empty()
    ) {
        resetViewer();
        return;
    }
    if (m_viewMatchId != match.matchId) {
        resetViewer();
        m_viewMatchId = match.matchId;
        m_status = SpectatorLiveStatus::Waiting;
    }
    auto const now = std::chrono::steady_clock::now();
    if (
        m_status == SpectatorLiveStatus::Live && m_lastPacketAt.time_since_epoch().count() != 0 &&
        now - m_lastPacketAt > kPacketTimeout
    ) {
        setFallback("STREAM_TIMEOUT");
    }
    if (m_viewBusy || now < m_nextViewerPollAt) return;
    pollViewer();
}

void SpectatorLiveRuntime::pollViewer() {
    auto& ranked = RankedRuntime::get();
    auto const matchId = ranked.view().match.matchId;
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
    if (!m_viewAttemptId.empty()) path += "&attemptId=" + m_viewAttemptId;
    if (m_viewGeneration > 0) path += fmt::format("&generation={}", m_viewGeneration);
    if (m_lastViewSequence >= 0) path += fmt::format("&afterSequence={}", m_lastViewSequence);

    auto const requestGeneration = ++m_viewRequestGeneration;
    m_viewBusy = true;
    m_viewRequest.spawn(
        request.get(endpoint(path)),
        [this, requestGeneration, matchId](web::WebResponse response) {
            if (
                requestGeneration != m_viewRequestGeneration ||
                matchId != m_viewMatchId
            ) return;
            m_viewBusy = false;
            m_nextViewerPollAt = std::chrono::steady_clock::now() +
                (m_status == SpectatorLiveStatus::Fallback
                    ? kFallbackRepollInterval
                    : kViewerPollInterval);
            if (response.code() == 403) {
                resetViewer();
                return;
            }
            if (!successful(response)) {
                ++m_consecutiveViewerErrors;
                if (m_consecutiveViewerErrors >= 3) setFallback("NETWORK_ERROR");
                return;
            }
            m_consecutiveViewerErrors = 0;
            applyViewerResponse(response.json().unwrapOr(matjson::Value()));
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
    if (root["fallback"].asBool().unwrapOr(false)) {
        setFallback(root["reason"].asString().unwrapOr("SERVER_FALLBACK"));
        return;
    }
    if (root["waiting"].asBool().unwrapOr(false)) {
        m_status = SpectatorLiveStatus::Waiting;
        m_reason = root["reason"].asString().unwrapOr("WAITING");
        m_previousPose.reset();
        m_currentPose.reset();
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
    if (matchId != m_viewMatchId || attemptId.empty() || generation == 0) {
        setFallback("STREAM_CONTEXT_MISMATCH");
        return;
    }
    auto const resetStream = root["reset"].asBool().unwrapOr(false);
    auto const streamChanged = attemptId != m_viewAttemptId || generation != m_viewGeneration;
    if (m_status == SpectatorLiveStatus::Fallback && !streamChanged && !resetStream) {
        return;
    }
    if (streamChanged || resetStream) {
        m_viewAttemptId = attemptId;
        m_viewGeneration = generation;
        m_lastViewSequence = -1;
        m_previousPose.reset();
        m_currentPose.reset();
        m_lastPacketAt = {};
    }
    m_opponentName = stream["opponentName"].asString().unwrapOr("Opponent");

    auto const packets = root["packets"].asArray();
    if (packets.isErr()) {
        setFallback("INVALID_PACKET_ARRAY");
        return;
    }
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
        if (m_lastViewSequence >= 0 && sequence != m_lastViewSequence + 1) {
            setFallback("SEQUENCE_GAP");
            return;
        }
        auto const keyframe = packet["keyframe"];
        if (!keyframe.isObject()) {
            setFallback("MISSING_KEYFRAME");
            return;
        }
        SpectatorGhostPose pose {
            .x = static_cast<float>(keyframe["x"].asDouble().unwrapOr(std::nan(""))),
            .y = static_cast<float>(keyframe["y"].asDouble().unwrapOr(std::nan(""))),
            .rotation = static_cast<float>(keyframe["rotation"].asDouble().unwrapOr(std::nan(""))),
            .dead = keyframe["dead"].asBool().unwrapOr(false),
            .finished = keyframe["finished"].asBool().unwrapOr(false),
        };
        if (!finitePose(pose)) {
            setFallback("INVALID_POSE");
            return;
        }
        if (m_currentPose && poseDistance(*m_currentPose, pose) > kPoseDiscontinuityDistance) {
            setFallback("POSE_DISCONTINUITY");
            return;
        }
        m_previousPose = m_currentPose;
        m_currentPose = pose;
        m_poseReceivedAt = std::chrono::steady_clock::now();
        m_lastPacketAt = m_poseReceivedAt;
        m_lastViewSequence = sequence;
    }
    m_status = m_currentPose ? SpectatorLiveStatus::Live : SpectatorLiveStatus::Waiting;
    m_reason = m_currentPose ? std::string() : "STREAM_BUFFER_EMPTY";
}

void SpectatorLiveRuntime::setFallback(std::string reason) {
    m_status = SpectatorLiveStatus::Fallback;
    m_reason = std::move(reason);
    m_previousPose.reset();
    m_currentPose.reset();
}

std::optional<SpectatorGhostPose> SpectatorLiveRuntime::interpolatedPose() const {
    if (!m_currentPose) return std::nullopt;
    if (!m_previousPose) return m_currentPose;
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_poseReceivedAt
    ).count();
    auto const alpha = std::clamp(static_cast<float>(elapsed) / 50.0f, 0.0f, 1.0f);
    SpectatorGhostPose pose;
    pose.x = m_previousPose->x + (m_currentPose->x - m_previousPose->x) * alpha;
    pose.y = m_previousPose->y + (m_currentPose->y - m_previousPose->y) * alpha;
    pose.rotation = m_previousPose->rotation +
        (m_currentPose->rotation - m_previousPose->rotation) * alpha;
    pose.dead = m_currentPose->dead;
    pose.finished = m_currentPose->finished;
    return pose;
}

} // namespace corum::ranked
