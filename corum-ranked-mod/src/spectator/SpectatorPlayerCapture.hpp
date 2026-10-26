#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
class PlayerObject;
class GJBaseGameLayer;
namespace corum::ranked {
// Observation only. Production live and Debug RECORD use the same native state.
SpectatorGhostPose captureSpectatorPose(PlayerObject* player, bool dead = false,
    bool finished = false, bool dual = false);
unsigned captureSpectatorEffects(PlayerObject* player,SpectatorGameMode mode);
SpectatorCosmetics captureSpectatorCosmetics(PlayerObject* first,PlayerObject* second);
std::optional<SpectatorCameraTransform> captureSpectatorCameraTransform(GJBaseGameLayer* layer);
}
