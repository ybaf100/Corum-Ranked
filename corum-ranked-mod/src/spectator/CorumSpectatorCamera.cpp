#include "CorumSpectatorCamera.hpp"
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace geode::prelude;

namespace corum::ranked {

void CorumSpectatorCamera::update(
    GJBaseGameLayer* layer, bool authorizedDummy,
    std::optional<SpectatorRemoteContext> const& liveContext,
    std::optional<SpectatorRemoteFrame> const& rendered
) {
    if (!layer || !layer->m_objectLayer || !authorizedDummy) {
        cleanup(layer);
        return;
    }
    auto const previousState = m_policy.state();
    auto const size = CCDirector::sharedDirector()->getWinSize();
    auto const scale = std::abs(layer->m_objectLayer->getScale());
    auto const origin = layer->m_gameState.m_cameraPosition;
    m_policy.update(true, {origin.x, origin.y},
        scale > 0.001f ? SpectatorCameraPoint{size.width / scale, size.height / scale} : SpectatorCameraPoint{},
        liveContext, rendered);
    if (!m_policy.ownsCamera()) return;
    if (!m_attached) {
        m_previousForceX = layer->m_gameState.m_unkBool4;
        m_previousForceY = layer->m_gameState.m_unkBool5;
        m_previousTargetY = layer->m_gameState.m_unkInt13;
        m_previousOrigin = origin;
        m_attached = true;
        log::info("Spectator camera attached (remote presentation only)");
    }
    auto const target = m_policy.origin();
    // Official GD 2.2081 cameraMoveX/Y establish native forced-camera ownership.
    // Apply an immediate target, not moveCameraToPos's 1.2-second tween. Native
    // updateCamera still owns world transform/background/culling/zoom/offsets.
    // NEVER move or swap m_player1 to trick vanilla camera targeting.
    layer->cameraMoveX(target.x, 0.0f, 1.0f, false);
    layer->cameraMoveY(target.y, 0.0f, 1.0f, true);
    layer->m_gameState.stopTweenAction(1);
    layer->m_gameState.stopTweenAction(2);
    layer->m_gameState.m_cameraPosition = CCPoint{target.x, target.y};
    if (previousState != m_policy.state()) {
        if (m_policy.state() == SpectatorCameraState::Waiting) {
            log::info("Spectator waiting for valid remote pose; camera held");
        } else {
            log::info("Spectator camera target acquired: terminal={}",
                m_policy.state() == SpectatorCameraState::Terminal);
        }
    }
}

void CorumSpectatorCamera::cleanup(GJBaseGameLayer* layer) {
    if (m_attached && layer) {
        layer->m_gameState.stopTweenAction(1);
        layer->m_gameState.stopTweenAction(2);
        layer->m_gameState.m_unkBool4 = m_previousForceX;
        layer->m_gameState.m_unkBool5 = m_previousForceY;
        layer->m_gameState.m_unkInt13 = m_previousTargetY;
        layer->m_gameState.m_cameraPosition = m_previousOrigin;
        log::info("Spectator camera ownership released");
    }
    m_attached = false;
    m_policy.reset();
}

} // namespace corum::ranked
