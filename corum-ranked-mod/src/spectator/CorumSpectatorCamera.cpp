#include "CorumSpectatorCamera.hpp"
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <array>

using namespace geode::prelude;

namespace corum::ranked {

void CorumSpectatorCamera::attach(GJBaseGameLayer* layer) {
    if (m_attached || !layer || !layer->m_objectLayer) return;
    m_previousForceX = layer->m_gameState.m_unkBool4;
    m_previousForceY = layer->m_gameState.m_unkBool5;
    m_previousTargetY = layer->m_gameState.m_unkInt13;
    m_previousOrigin = layer->m_gameState.m_cameraPosition;
    m_attached = true;
    auto const size = CCDirector::sharedDirector()->getWinSize();
    auto const scale = std::abs(layer->m_objectLayer->getScale());
    m_policy.update(true, {m_previousOrigin.x, m_previousOrigin.y},
        {size.width / std::max(0.001f, scale), size.height / std::max(0.001f, scale)},
        std::nullopt, std::nullopt);
    log::info("Spectator camera attached (remote presentation only)");
    log::info("Spectator waiting for valid remote pose; camera held");
}

void CorumSpectatorCamera::prepareNativeFallback(GJBaseGameLayer* layer) {
    if (!layer || !m_attached) return;
    layer->m_gameState.stopTweenAction(1); layer->m_gameState.stopTweenAction(2);
    layer->m_gameState.m_unkBool4 = m_previousForceX;
    layer->m_gameState.m_unkBool5 = m_previousForceY;
    layer->m_gameState.m_unkInt13 = m_previousTargetY;
}

void CorumSpectatorCamera::applyViewport(GJBaseGameLayer* layer, SpectatorCameraPoint target) {
    auto const previous = layer->m_gameState.m_cameraPosition;
    // cameraMove establishes ownership/tween state; it does NOT guarantee that
    // the render nodes moved. This is a final correction AFTER updateCamera.
    layer->cameraMoveX(target.x, 0.0f, 1.0f, false);
    layer->cameraMoveY(target.y, 0.0f, 1.0f, true);
    layer->m_gameState.stopTweenAction(1); layer->m_gameState.stopTweenAction(2);
    layer->m_gameState.m_cameraPosition = CCPoint{target.x, target.y};
    layer->m_gameState.m_cameraPosition2 = CCPoint{target.x, target.y};
    layer->m_gameState.m_cameraStepDiff.x += target.x - previous.x;
    layer->m_gameState.m_cameraStepDiff.y += target.y - previous.y;
    // Map the recorded level-space viewport origin to screen origin through the
    // actual native affine transform. Preserve native scale/rotation/anchors;
    // do not assume a variable assignment updated object/shader render layers.
    std::array<CCNode*, 3> nodes{layer->m_objectLayer, layer->m_inShaderObjectLayer, layer->m_aboveShaderObjectLayer};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto* node = nodes[i];
        if (!node || std::find(nodes.begin(), nodes.begin() + i, node) != nodes.begin() + i) continue;
        auto const projected = CCPointApplyAffineTransform({target.x, target.y}, node->nodeToParentTransform());
        auto const screenOrigin = node->getParent() ? node->getParent()->convertToNodeSpace(CCPointZero) : CCPointZero;
        auto const position = node->getPosition();
        node->setPosition({position.x + screenOrigin.x - projected.x, position.y + screenOrigin.y - projected.y});
    }
    // Keep native background/ground art and zoom mechanics, without another
    // updateCamera call or a second temporal interpolation layer.
    layer->updateCameraBGArt({target.x, target.y}, layer->m_gameState.m_cameraZoom);
}

void CorumSpectatorCamera::update(
    GJBaseGameLayer* layer, bool authorizedDummy,
    std::optional<SpectatorRemoteContext> const& liveContext,
    std::optional<SpectatorRemoteFrame> const& rendered
) {
    if (!layer || !layer->m_objectLayer || !authorizedDummy) {
        cleanup(layer);
        return;
    }
    attach(layer);
    auto const previousState = m_policy.state();
    auto const size = CCDirector::sharedDirector()->getWinSize();
    auto const scale = std::abs(layer->m_objectLayer->getScale());
    auto const origin = layer->m_gameState.m_cameraPosition;
    m_policy.update(true, {origin.x, origin.y},
        scale > 0.001f ? SpectatorCameraPoint{size.width / scale, size.height / scale} : SpectatorCameraPoint{},
        liveContext, rendered);
    if (!m_policy.ownsCamera()) return;
    applyViewport(layer, m_policy.origin());
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
        if (layer->m_objectLayer) applyViewport(layer, {m_previousOrigin.x, m_previousOrigin.y});
        layer->m_gameState.m_unkBool4 = m_previousForceX;
        layer->m_gameState.m_unkBool5 = m_previousForceY;
        layer->m_gameState.m_unkInt13 = m_previousTargetY;
        log::info("Spectator camera ownership released");
    }
    m_attached = false;
    m_policy.reset();
}

} // namespace corum::ranked
