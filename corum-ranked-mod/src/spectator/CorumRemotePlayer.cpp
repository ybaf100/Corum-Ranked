#include "CorumRemotePlayer.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GhostTrailEffect.hpp>
#include <Geode/binding/HardStreak.hpp>

using namespace geode::prelude;

namespace {

// Native PlayerObject creates sibling trails/particles outside its own subtree.
// The companion inventory is adapted from Globed VisualPlayer::cleanupObjectLayer
// (MIT, copyright 2023 dank_meme); see spectator/GLOBED-NOTICE.md.
template <class Visitor>
void visitNativeCompanions(PlayerObject* player, Visitor visit) {
    visit(player->m_shipStreak);
    visit(player->m_regularTrail);
    visit(player->m_waveTrail);
    visit(player->m_ghostTrail);
    visit(player->m_playerGroundParticles);
    visit(player->m_trailingParticles);
    visit(player->m_shipClickParticles);
    visit(player->m_vehicleGroundParticles);
    visit(player->m_ufoClickParticles);
    visit(player->m_robotBurstParticles);
    visit(player->m_dashParticles);
    visit(player->m_swingBurstParticles1);
    visit(player->m_swingBurstParticles2);
    visit(player->m_landParticles0);
    visit(player->m_landParticles1);
}

void isolateNativeCompanions(PlayerObject* player, CCNode* hiddenRoot) {
    visitNativeCompanions(player, [hiddenRoot](auto*& node) {
        if (!node) return;
        Ref<CCNode> retained(node);
        node->removeFromParentAndCleanup(false);
        hiddenRoot->addChild(node);
        node->unscheduleAllSelectors();
        node->setVisible(false);
    });
}

void discardNativeCompanions(PlayerObject* player) {
    visitNativeCompanions(player, [](auto*& node) {
        if (node) node->removeFromParentAndCleanup(true);
        node = nullptr;
    });
}

} // namespace

namespace corum::ranked {

CorumRemotePlayer::~CorumRemotePlayer() { cleanup(); }

bool CorumRemotePlayer::createVisual(PlayLayer* layer) {
    if (!layer || !layer->m_objectLayer || !layer->m_player1) return false;
    auto* root = CCLayer::create();
    if (!root) return false;
    m_root = root;
    auto* effectsRoot = CCNode::create();
    if (!effectsRoot) {
        destroyVisual();
        return false;
    }
    m_effectsRoot = effectsRoot;
    effectsRoot->setVisible(false);
    root->addChild(effectsRoot);
    root->setID("corum-spectator-remote-root");
    root->setAnchorPoint(CCPointZero);
    root->ignoreAnchorPointForPosition(false);
    root->setPosition(CCPointZero);
    root->setVisible(false);
    // Same level coordinate space as the native player, not a HUD/camera node.
    layer->m_objectLayer->addChild(root, layer->m_player1->getZOrder());
    auto* player = PlayerObject::create(1, 1, layer, root, true);
    if (!player || player == layer->m_player1 || player == layer->m_player2) {
        destroyVisual();
        return false;
    }
    m_player = player;
    player->setID("corum-spectator-remote-player");
    root->addChild(player);
    // onEnter may schedule native work: disable it AFTER attaching to the scene.
    player->unscheduleAllSelectors();
    player->stopAllActions();
    player->m_playEffects = false;
    isolateNativeCompanions(player, effectsRoot);
    player->m_isHidden = false;
    player->setColor({100, 190, 255});
    player->setSecondColor({240, 245, 255});
    player->setVisible(false);
    // Not registered with m_objects, collision sections, m_player1 or m_player2.
    log::info("Spectator Remote Player created");
    return true;
}

void CorumRemotePlayer::destroyVisual() {
    m_poseCurrent = false;
    m_appliedState.reset();
    auto const hadVisual = m_player.data() != nullptr || m_root.data() != nullptr;
    if (m_player) {
        m_player->unscheduleAllSelectors();
        m_player->stopAllActions();
        discardNativeCompanions(m_player.data());
        m_player->removeFromParentAndCleanup(true);
        m_player = nullptr;
    }
    if (m_effectsRoot) {
        m_effectsRoot->removeFromParentAndCleanup(true);
        m_effectsRoot = nullptr;
    }
    if (m_root) {
        m_root->removeFromParentAndCleanup(true);
        m_root = nullptr;
    }
    if (hadVisual) log::info("Spectator Remote Player destroyed");
}

void CorumRemotePlayer::cleanup() {
    destroyVisual();
    m_policy.reset();
    m_discardLogged = false;
    m_streamAttachedLogged = false;
}

void CorumRemotePlayer::setVisible(bool visible) {
    if (m_player) m_player->setVisible(visible);
    if (m_root) m_root->setVisible(visible);
}

void CorumRemotePlayer::applyGameplayState(SpectatorGhostPose const& pose) {
    auto* player = m_player.data();
    if (!player) return;
    auto const modeChanged = !m_appliedState || m_appliedState->mode != pose.mode;
    auto const gravityChanged = !m_appliedState || m_appliedState->upsideDown != pose.upsideDown;
    // Presentation flags only; flipGravity can touch movement/physics, so do not
    // invoke it. Native mode helpers consult gravity when configuring vehicles.
    if (gravityChanged) player->m_isUpsideDown = pose.upsideDown;
    auto const reconfigureMode = modeChanged || (gravityChanged && pose.mode == SpectatorGameMode::Ship);
    if (reconfigureMode) {
        // Native GD 2.2081 vehicle setup. No portal/game-layer mode switch, update,
        // collision registration or local player pointer replacement is involved.
        player->toggleFlyMode(false, true);
        player->toggleRollMode(false, true);
        player->toggleBirdMode(false, true);
        player->toggleDartMode(false, true);
        player->toggleRobotMode(false, true);
        player->toggleSpiderMode(false, true);
        player->toggleSwingMode(false, true);
        switch (pose.mode) {
            case SpectatorGameMode::Cube: player->updatePlayerFrame(1); break;
            case SpectatorGameMode::Ship: player->toggleFlyMode(true, false); player->updatePlayerShipFrame(1); break;
            case SpectatorGameMode::Ball: player->toggleRollMode(true, true); player->updatePlayerRollFrame(1); break;
            case SpectatorGameMode::UFO: player->toggleBirdMode(true, true); player->updatePlayerBirdFrame(1); break;
            case SpectatorGameMode::Wave: player->toggleDartMode(true, true); player->updatePlayerDartFrame(1); break;
            case SpectatorGameMode::Robot: player->toggleRobotMode(true, true); player->updatePlayerRobotFrame(1); break;
            case SpectatorGameMode::Spider: player->toggleSpiderMode(true, true); player->updatePlayerSpiderFrame(1); break;
            case SpectatorGameMode::Swing: player->toggleSwingMode(true, true); player->updatePlayerSwingFrame(1); break;
        }
        player->unscheduleAllSelectors();
        player->stopAllActions();
        player->m_playEffects = false;
        isolateNativeCompanions(player, m_effectsRoot.data());
    }
    if (reconfigureMode || !m_appliedState || m_appliedState->mini != pose.mini) {
        player->togglePlayerScale(pose.mini, true);
        player->updatePlayerScale();
    }
    if (player->m_mainLayer && (reconfigureMode || !m_appliedState || m_appliedState->flipped != pose.flipped))
        player->m_mainLayer->setScaleY(pose.flipped ? -1.0f : 1.0f);
    m_appliedState = pose;
}

void CorumRemotePlayer::applyPose(SpectatorGhostPose const& pose) {
    applyGameplayState(pose);
    m_player->setPosition({pose.x, pose.y});
    m_player->setRotation(pose.rotation);
    // CCPoint also accepts CCSize assignment in GD; use an explicit type.
    m_player->m_startPosition = CCPoint {pose.x, pose.y};
    m_player->m_lastPosition = CCPoint {pose.x, pose.y};
    m_player->m_positionX = pose.x;
    m_player->m_positionY = pose.y;
    m_player->m_isDead = pose.dead;
}

void CorumRemotePlayer::update(
    PlayLayer* layer, bool authorizedDummy,
    std::optional<SpectatorRemoteFrame> const& frame
) {
    auto const eligible = authorizedDummy && layer && layer->m_level &&
        frame && frame->context.levelId == static_cast<int>(layer->m_level->m_levelID);
    auto const action = m_policy.reconcile(
        eligible, frame ? std::optional(frame->context) : std::nullopt
    );
    if (action == SpectatorRemoteLifecycle::Destroy) {
        destroyVisual();
        m_discardLogged = false;
        m_streamAttachedLogged = false;
        return;
    }
    if (action == SpectatorRemoteLifecycle::Create || action == SpectatorRemoteLifecycle::Recreate) {
        if (action == SpectatorRemoteLifecycle::Recreate) {
            log::info("Spectator Remote Player reset");
            destroyVisual();
        }
        m_discardLogged = false;
        m_streamAttachedLogged = false;
        if (!createVisual(layer)) {
            // One creation attempt per context; no per-frame allocation/error loop.
            log::warn("Spectator Remote Player creation failed; polling HUD retained");
            return;
        }
    }
    if (!m_player) return;
    if (!frame || !frame->pose) {
        m_poseCurrent = false;
        setVisible(false);
        return;
    }
    auto const decision = m_policy.acceptPose(frame->context, frame->sequence, *frame->pose);
    if (decision == SpectatorRemotePoseDecision::Rejected) {
        m_poseCurrent = false;
        setVisible(false);
        if (!m_discardLogged) {
            log::warn("Stale or invalid spectator pose discarded");
            m_discardLogged = true;
        }
        return;
    }
    if (decision == SpectatorRemotePoseDecision::Applied) {
        applyPose(*m_policy.pose());
        if (m_policy.state() == SpectatorRemoteState::Dead) {
            // Safe presentation fade; no native playerDestroyed/PlayLayer lifecycle.
            setVisible(true);
            m_player->runAction(CCFadeOut::create(0.25f));
            log::info("Spectator opponent death presentation");
        } else if (m_policy.state() == SpectatorRemoteState::Finished) {
            log::info("Spectator opponent Clear presentation: final pose held");
        }
        if (!m_streamAttachedLogged) {
            log::info("Spectator pose stream attached: round={} dm={} generation={} epoch={}",
                frame->context.roundNumber, frame->context.deathmatchSequence,
                frame->context.generation, frame->context.epoch);
            m_streamAttachedLogged = true;
        }
    }
    // Dead fades to opacity 0; Finished holds visibly. No GD gameplay callback.
    m_poseCurrent = true;
    setVisible(m_policy.pose().has_value());
}

} // namespace corum::ranked
