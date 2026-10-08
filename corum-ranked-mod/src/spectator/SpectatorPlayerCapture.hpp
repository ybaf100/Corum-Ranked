#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
class PlayerObject;
namespace corum::ranked {
// Observation only. Production live and Debug RECORD use the same native state.
SpectatorGhostPose captureSpectatorPose(PlayerObject* player, bool dead = false,
    bool finished = false, bool dual = false);
}
