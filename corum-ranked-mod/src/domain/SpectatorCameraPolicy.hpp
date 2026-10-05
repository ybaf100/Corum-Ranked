#pragma once

#include "SpectatorRemotePolicy.hpp"
#include <algorithm>
#include <optional>
#include <string_view>

namespace corum::ranked {

struct SpectatorSceneContext {
    std::string matchId;
    int roundNumber = 0;
    int deathmatchSequence = 0;
    int levelId = 0;
    bool operator==(SpectatorSceneContext const&) const = default;
};

inline bool spectatorSceneWatchable(
    bool matched, SpectatorSceneContext const& scene,
    SpectatorSceneContext const& current, std::string_view state
) {
    return matched && !scene.matchId.empty() && scene.levelId > 0 &&
        scene.roundNumber > 0 && scene == current &&
        (state == "ROUND_PLAYING" || state == "FINAL_ATTEMPT_WINDOW" ||
         state == "LAST_ATTEMPT_WINDOW" || state == "ROUND_SETTLING" ||
         state == "DEATHMATCH_PLAYING");
}

struct SpectatorCameraPoint {
    float x = 0;
    float y = 0;
    bool operator==(SpectatorCameraPoint const&) const = default;
};

enum class SpectatorCameraState { Detached, Waiting, Following, Terminal };

// Presentation only. The caller must supply Phase-1-authorized dummy authority
// and the CURRENT validated live context, never a local player's position.
class SpectatorCameraPolicy {
public:
    void update(
        bool authorizedDummy, SpectatorCameraPoint currentOrigin,
        SpectatorCameraPoint coverage,
        std::optional<SpectatorRemoteContext> const& context,
        std::optional<SpectatorRemoteFrame> const& rendered
    ) {
        if (!authorizedDummy) { reset(); return; }
        if (m_state == SpectatorCameraState::Detached) {
            // Waiting captures the actual camera: no packet means no (0,0) jump.
            if (!finite(currentOrigin)) return;
            m_origin = currentOrigin;
        }
        if (m_context != context) {
            m_context = context;
            m_sequence = -1;
        }
        m_state = SpectatorCameraState::Waiting;
        if (!context || !rendered || rendered->context != *context ||
            !rendered->pose || context->matchId.empty() || context->attemptId.empty() ||
            context->generation == 0 || context->epoch == 0 ||
            context->levelId <= 0 || context->roundNumber <= 0 ||
            rendered->sequence < 0 || rendered->sequence < m_sequence ||
            !finiteSpectatorPose(*rendered->pose) || !finite(coverage) ||
            coverage.x <= 0 || coverage.y <= 0) return;
        m_sequence = rendered->sequence;
        auto const& pose = *rendered->pose;
        // Classic-level look-ahead; Y keeps grounded Cube at the ground and
        // follows airborne/high sections. Trigger/platformer parity is later.
        m_origin = {std::max(0.0f, pose.x - coverage.x * 0.35f),
                    std::max(0.0f, pose.y - coverage.y * 0.45f)};
        m_state = pose.dead || pose.finished ? SpectatorCameraState::Terminal :
            SpectatorCameraState::Following;
    }
    void reset() {
        m_context.reset(); m_sequence = -1;
        m_state = SpectatorCameraState::Detached;
    }
    [[nodiscard]] bool ownsCamera() const { return m_state != SpectatorCameraState::Detached; }
    [[nodiscard]] SpectatorCameraPoint origin() const { return m_origin; }
    [[nodiscard]] SpectatorCameraState state() const { return m_state; }

private:
    static bool finite(SpectatorCameraPoint p) { return std::isfinite(p.x) && std::isfinite(p.y); }
    std::optional<SpectatorRemoteContext> m_context;
    std::int64_t m_sequence = -1;
    SpectatorCameraPoint m_origin;
    SpectatorCameraState m_state = SpectatorCameraState::Detached;
};

} // namespace corum::ranked
