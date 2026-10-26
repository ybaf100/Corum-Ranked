#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
class PlayerObject;
class GJBaseGameLayer;
namespace corum::ranked {
// Observation only. Production live and Debug RECORD use the same native state.
SpectatorGhostPose captureSpectatorPose(PlayerObject* player, bool dead = false,
    bool finished = false, bool dual = false);
std::optional<SpectatorCameraTransform> captureSpectatorCameraTransform(GJBaseGameLayer* layer);
}
