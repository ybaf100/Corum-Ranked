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
    m_previousTransform.zoom=layer->m_gameState.m_cameraZoom;m_previousTransform.angle=layer->m_gameState.m_cameraAngle;
    m_previousTargetZoom=layer->m_gameState.m_targetCameraZoom;m_previousTargetAngle=layer->m_gameState.m_targetCameraAngle;
    std::array<CCNode*,3> nodes{layer->m_objectLayer,layer->m_inShaderObjectLayer,layer->m_aboveShaderObjectLayer};
    for(std::size_t i=0;i<nodes.size();++i)if(auto* n=nodes[i])m_previousTransform.layers[i]={true,n->getScaleX(),n->getScaleY(),n->getRotation()};
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
    // Native world-driver triggers own free/static/offset camera mechanics.
    // A fallback must not restore entry flags over their current state.
    (void)layer;
}

void CorumSpectatorCamera::applyTransform(GJBaseGameLayer* layer,SpectatorCameraTransform const& transform){
    if(!validSpectatorCameraTransform(transform))return;
    layer->m_gameState.m_cameraZoom=transform.zoom;layer->m_gameState.m_cameraAngle=transform.angle;
    std::array<CCNode*,3> nodes{layer->m_objectLayer,layer->m_inShaderObjectLayer,layer->m_aboveShaderObjectLayer};
    for(std::size_t i=0;i<nodes.size();++i)if(auto* n=nodes[i];n && transform.layers[i].present){
        auto const& t=transform.layers[i];n->setScaleX(t.scaleX);n->setScaleY(t.scaleY);n->setRotation(t.rotation);
    }
}

void CorumSpectatorCamera::applyViewport(GJBaseGameLayer* layer, SpectatorCameraPoint target) {
    auto const previous = layer->m_gameState.m_cameraPosition;
    // Final origin drift correction AFTER native world/camera processing.
    // cameraMoveX/Y set forced-camera flags and cancel native trigger tweens;
    // keep those mechanics intact and correct only the viewport origin/nodes.
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
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto const nativeOrigin=layer->m_gameState.m_cameraPosition;
    auto const nativeZoom=layer->m_gameState.m_cameraZoom,nativeAngle=layer->m_gameState.m_cameraAngle;
#endif
    auto const transformAccepted=m_policy.state()!=SpectatorCameraState::Waiting && rendered && rendered->pose &&
        rendered->pose->camera && rendered->pose->camera->transform;
    if(transformAccepted)
        applyTransform(layer,*rendered->pose->camera->transform);
    applyViewport(layer, m_policy.origin());
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto const now=std::chrono::steady_clock::now();
    if(transformAccepted &&
       m_transformLogs<120 && now>=m_nextTransformLog){
        m_nextTransformLog=now+std::chrono::seconds(1);++m_transformLogs;
        auto const& c=*rendered->pose->camera;auto const& t=*c.transform;
        auto const projected=CCPointApplyAffineTransform({c.x,c.y},layer->m_objectLayer->nodeToWorldTransform());
        log::info("[SpectatorCamera] publisherMs={} sequence={} cameraX={} cameraY={} zoom={} angle={} screenOriginError={} zoomError={} angleError={} layerScaleXError={} layerRotationError={} nativeBeforeX={} nativeBeforeY={} nativeBeforeZoom={} nativeBeforeAngle={}",
            rendered->publisherElapsedMs,rendered->sequence,c.x,c.y,t.zoom,t.angle,
            std::hypot(projected.x,projected.y),
            layer->m_gameState.m_cameraZoom-t.zoom,layer->m_gameState.m_cameraAngle-t.angle,
            layer->m_objectLayer->getScaleX()-t.layers[0].scaleX,layer->m_objectLayer->getRotation()-t.layers[0].rotation,
            nativeOrigin.x,nativeOrigin.y,nativeZoom,nativeAngle);
    }
#endif
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
        applyTransform(layer,m_previousTransform);
        layer->m_gameState.m_targetCameraZoom=m_previousTargetZoom;layer->m_gameState.m_targetCameraAngle=m_previousTargetAngle;
        if (layer->m_objectLayer) applyViewport(layer, {m_previousOrigin.x, m_previousOrigin.y});
        layer->m_gameState.m_unkBool4 = m_previousForceX;
        layer->m_gameState.m_unkBool5 = m_previousForceY;
        layer->m_gameState.m_unkInt13 = m_previousTargetY;
        log::info("Spectator camera ownership released");
    }
    m_attached = false;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    m_transformLogs=0;m_nextTransformLog={};
#endif
    m_policy.reset();
}

} // namespace corum::ranked
