#pragma once
#include "SpectatorGameplayState.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {

struct SpectatorCameraOrigin { float x = 0, y = 0; };
inline bool validSpectatorCamera(SpectatorCameraOrigin camera) {
    return std::isfinite(camera.x) && std::isfinite(camera.y) &&
        std::abs(camera.x) <= 10'000'000.0f && std::abs(camera.y) <= 10'000'000.0f;
}
enum class SpectatorDiscontinuity { Teleport, SpiderSnap };
inline std::string_view spectatorDiscontinuityName(SpectatorDiscontinuity type) {
    return type == SpectatorDiscontinuity::Teleport ? "TELEPORT" : "SPIDER_SNAP";
}
struct SpectatorSnapEvidence {
    SpectatorDiscontinuity type = SpectatorDiscontinuity::Teleport;
    float fromX = 0, fromY = 0, toX = 0, toY = 0;
    std::optional<SpectatorCameraOrigin> fromCamera = std::nullopt;
};

struct SpectatorGhostPose {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    bool dead = false;
    bool finished = false;
    SpectatorGameMode mode = SpectatorGameMode::Cube;
    bool mini = false;
    bool upsideDown = false;
    // Native visual flip is distinct from gravity (e.g. Ship/UFO).
    bool flipped = false;
    bool dual = false;
    bool visible = true;
    std::optional<SpectatorCameraOrigin> camera = std::nullopt;
    bool cameraSnap = false;
    // Native event endpoints, never an unrestricted large-jump opt-out.
    std::optional<SpectatorSnapEvidence> discontinuity = std::nullopt;
    // Decoder-only distinction: malformed camera holds the prior viewport;
    // an absent camera may use the scoped native remote-target fallback.
    bool cameraInvalid = false;
};

struct SpectatorRemoteContext {
    std::string matchId;
    int roundNumber = 0;
    int deathmatchSequence = 0;
    int levelId = 0;
    std::string attemptId;
    std::uint64_t generation = 0;
    // Local presentation epoch also changes for reset=true with the SAME tuple.
    std::uint64_t epoch = 0;
    bool operator==(SpectatorRemoteContext const&) const = default;
};

struct SpectatorRemoteFrame {
    SpectatorRemoteContext context;
    std::int64_t sequence = -1;
    std::optional<SpectatorGhostPose> pose;
    double publisherElapsedMs = 0;
    // Client presentation segment; not serialized. Survives skipped render
    // frames so native crossing history cannot sweep through a snap.
    std::uint64_t presentationSegment = 0;
};

inline bool finiteSpectatorPose(SpectatorGhostPose const& pose) {
    return std::isfinite(pose.x) && std::isfinite(pose.y) &&
        std::isfinite(pose.rotation) && std::abs(pose.x) <= 10'000'000.0f &&
        std::abs(pose.y) <= 10'000'000.0f && std::abs(pose.rotation) <= 100'000.0f &&
        !spectatorModeName(pose.mode).empty() && !(pose.dead && pose.finished);
}

inline bool finiteSpectatorSnap(SpectatorSnapEvidence const& snap) {
    if (!validSpectatorCamera({snap.fromX, snap.fromY}) ||
        !validSpectatorCamera({snap.toX, snap.toY})) return false;
    auto const dx = snap.toX - snap.fromX, dy = snap.toY - snap.fromY;
    auto const distance = std::hypot(dx, dy);
    return distance > 0 && (snap.type == SpectatorDiscontinuity::Teleport ? distance <= 100'000.0f :
        std::abs(dx) <= 400.0f && std::abs(dy) <= 2400.0f);
}
inline bool verifiedSpectatorSnap(SpectatorGhostPose const& previous, SpectatorGhostPose const& next) {
    if (!next.discontinuity || !finiteSpectatorSnap(*next.discontinuity)) return false;
    auto const& snap = *next.discontinuity;
    // Both endpoints must align with adjacent accepted samples. The residual
    // continuous motion still has the original 400-unit safety bound.
    if (std::hypot(snap.fromX - previous.x, snap.fromY - previous.y) > 400.0f ||
        std::hypot(next.x - snap.toX, next.y - snap.toY) > 400.0f) return false;
    return snap.type == SpectatorDiscontinuity::Teleport ||
        (next.mode == SpectatorGameMode::Spider &&
         (previous.mode != next.mode || previous.upsideDown != next.upsideDown));
}

// Keep the original correction limit. A bounded, native Spider gravity snap or
// mode portal transition snaps presentation instead of lerping across the map.
inline bool spectatorPoseSnapAllowed(SpectatorGhostPose const& previous, SpectatorGhostPose const& next) {
    if (next.discontinuity) return verifiedSpectatorSnap(previous, next);
    auto const dx = std::abs(next.x - previous.x);
    auto const dy = std::abs(next.y - previous.y);
    return dx <= 400.0f && dy <= 2400.0f &&
        (previous.mode != next.mode ||
         (previous.mode == SpectatorGameMode::Spider && next.mode == SpectatorGameMode::Spider &&
          previous.upsideDown != next.upsideDown));
}

enum class SpectatorRemoteLifecycle { None, Create, Recreate, Destroy };
enum class SpectatorRemoteState { Waiting, Live, Dead, Finished };
enum class SpectatorRemotePoseDecision { Applied, Frozen, Rejected };

// Presentation ONLY. There is no PlayLayer, scoring, Practice, persistence,
// camera or transport callback in this policy.
class SpectatorRemotePolicy {
public:
    SpectatorRemoteLifecycle reconcile(
        bool authorizedDummy, std::optional<SpectatorRemoteContext> const& context
    ) {
        auto const valid = context && !context->matchId.empty() &&
            context->roundNumber > 0 && context->levelId > 0 && context->epoch > 0 &&
            context->deathmatchSequence >= 0 &&
            ((context->attemptId.empty() && context->generation == 0) ||
             (!context->attemptId.empty() && context->generation > 0));
        if (!authorizedDummy || !valid) {
            auto const action = m_context ? SpectatorRemoteLifecycle::Destroy : SpectatorRemoteLifecycle::None;
            reset();
            return action;
        }
        if (m_context && *m_context == *context) return SpectatorRemoteLifecycle::None;
        auto const action = m_context ? SpectatorRemoteLifecycle::Recreate : SpectatorRemoteLifecycle::Create;
        reset();
        m_context = context;
        return action;
    }

    SpectatorRemotePoseDecision acceptPose(
        SpectatorRemoteContext const& context, std::int64_t sequence,
        SpectatorGhostPose const& pose
    ) {
        if (!m_context || *m_context != context || context.attemptId.empty() ||
            sequence < 0 || sequence < m_sequence || !finiteSpectatorPose(pose) || pose.dual) {
            return SpectatorRemotePoseDecision::Rejected;
        }
        // Same sequence is allowed: existing interpolation samples it each frame.
        // Terminal motion cannot resume until a new stream/context is attached.
        if (m_state == SpectatorRemoteState::Dead || m_state == SpectatorRemoteState::Finished) {
            return SpectatorRemotePoseDecision::Frozen;
        }
        m_sequence = sequence;
        m_pose = pose;
        m_state = pose.dead ? SpectatorRemoteState::Dead :
            pose.finished ? SpectatorRemoteState::Finished : SpectatorRemoteState::Live;
        return SpectatorRemotePoseDecision::Applied;
    }

    void reset() {
        m_context.reset();
        m_pose.reset();
        m_sequence = -1;
        m_state = SpectatorRemoteState::Waiting;
    }
    [[nodiscard]] SpectatorRemoteState state() const { return m_state; }
    [[nodiscard]] std::optional<SpectatorGhostPose> const& pose() const { return m_pose; }
    [[nodiscard]] std::optional<SpectatorRemoteFrame> renderedFrame() const {
        if (!m_context || !m_pose) return std::nullopt;
        return SpectatorRemoteFrame{*m_context, m_sequence, m_pose};
    }

private:
    std::optional<SpectatorRemoteContext> m_context;
    std::optional<SpectatorGhostPose> m_pose;
    std::int64_t m_sequence = -1;
    SpectatorRemoteState m_state = SpectatorRemoteState::Waiting;
};

} // namespace corum::ranked
