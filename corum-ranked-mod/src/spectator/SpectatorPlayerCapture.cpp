#include "SpectatorPlayerCapture.hpp"
#include <Geode/binding/PlayerObject.hpp>
#include <limits>

namespace corum::ranked {
SpectatorGhostPose captureSpectatorPose(PlayerObject* player, bool dead, bool finished, bool dual) {
    SpectatorGhostPose pose;
    if (!player) { pose.x = std::numeric_limits<float>::quiet_NaN(); return pose; }
    auto const position = player->getPosition();
    pose.x = position.x; pose.y = position.y; pose.rotation = player->getRotation();
    // Geode 5.8.2 / GD 2.2081 bindings; no portal reimplementation or player writes.
    if (player->m_isShip) pose.mode = SpectatorGameMode::Ship;
    else if (player->m_isBall) pose.mode = SpectatorGameMode::Ball;
    else if (player->m_isBird) pose.mode = SpectatorGameMode::UFO;
    else if (player->m_isDart) pose.mode = SpectatorGameMode::Wave;
    else if (player->m_isRobot) pose.mode = SpectatorGameMode::Robot;
    else if (player->m_isSpider) pose.mode = SpectatorGameMode::Spider;
    else if (player->m_isSwing) pose.mode = SpectatorGameMode::Swing;
    pose.mini = player->m_vehicleSize != 1.0f;
    pose.upsideDown = player->m_isUpsideDown;
    pose.flipped = player->m_mainLayer && player->m_mainLayer->getScaleY() < 0.0f;
    pose.dead = dead || player->m_isDead;
    pose.finished = finished; pose.dual = dual;
    return pose;
}
}
