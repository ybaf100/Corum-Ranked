#include "SpectatorPlayerCapture.hpp"
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <limits>

namespace corum::ranked {
std::optional<SpectatorCameraTransform> captureSpectatorCameraTransform(GJBaseGameLayer* layer){
    if(!layer)return std::nullopt;
    SpectatorCameraTransform t;t.zoom=layer->m_gameState.m_cameraZoom;t.angle=layer->m_gameState.m_cameraAngle;
    t.nativeTweening=std::abs(layer->m_gameState.m_targetCameraZoom-t.zoom)>.0001f ||
        std::abs(std::remainder(layer->m_gameState.m_targetCameraAngle-t.angle,360.f))>.0001f;
    std::array<cocos2d::CCNode*,3> nodes{layer->m_objectLayer,layer->m_inShaderObjectLayer,layer->m_aboveShaderObjectLayer};
    for(std::size_t i=0;i<nodes.size();++i)if(auto* n=nodes[i])t.layers[i]={true,n->getScaleX(),n->getScaleY(),n->getRotation()};
    return validSpectatorCameraTransform(t)?std::optional(t):std::nullopt;
}
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
    pose.visible = player->isVisible();
    return pose;
}
}
