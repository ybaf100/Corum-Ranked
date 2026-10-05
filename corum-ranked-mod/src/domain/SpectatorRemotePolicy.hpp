#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace corum::ranked {

struct SpectatorGhostPose {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    bool dead = false;
    bool finished = false;
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
};

inline bool finiteSpectatorPose(SpectatorGhostPose const& pose) {
    return std::isfinite(pose.x) && std::isfinite(pose.y) &&
        std::isfinite(pose.rotation) && std::abs(pose.x) <= 10'000'000.0f &&
        std::abs(pose.y) <= 10'000'000.0f;
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
            sequence < 0 || sequence < m_sequence || !finiteSpectatorPose(pose)) {
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
