#pragma once
#include "SpectatorRemotePolicy.hpp"
#include <algorithm>

namespace corum::ranked {
// UI only. No writes to GJGameState, level percentage, journal or scoring.
inline std::optional<float> spectatorProgressPercent(
    SpectatorRemoteFrame const& frame, float levelLength, int levelTimestamp,
    std::optional<int> authoritativeOpponent = std::nullopt
) {
    if (!frame.pose || !finiteSpectatorPose(*frame.pose)) return std::nullopt;
    if (frame.pose->finished) return 100.0f;
    if (authoritativeOpponent) return static_cast<float>(std::clamp(*authoritativeOpponent, 0, 100));
    // GD 2.2 timestamp levels use native 240Hz level-time units, rather than
    // distance. Debug starts at native gameplay; elapsed is its rendered clock.
    auto const percent = levelTimestamp > 0
        ? frame.publisherElapsedMs * 0.24 / levelTimestamp * 100.0
        : levelLength > 0 && std::isfinite(levelLength) ? frame.pose->x / levelLength * 100.0 : -1.0;
    if (!std::isfinite(percent) || percent < 0) return std::nullopt;
    return static_cast<float>(std::clamp(percent, 0.0, 100.0));
}
} // namespace corum::ranked
