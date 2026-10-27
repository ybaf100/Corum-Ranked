#include "CorumRemotePlayer.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GhostTrailEffect.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <Geode/binding/GameManager.hpp>
#include <chrono>

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
        node->stopAllActions();
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
void CorumRemotePlayer::hideNativeVisuals(PlayerObject* player){
    if(!player)return;
    player->m_playEffects=false;
    for(auto* child:player->getChildrenExt())child->setVisible(false);
    visitNativeCompanions(player,[](auto* node){if(node)node->setVisible(false);});
}

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
    effectsRoot->setVisible(true);
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
    if(m_secondary)m_secondary->cleanup();
    resetEffects();for(auto& ghost:m_ghosts)ghost=nullptr;
    m_cosmetics.reset();m_appliedState.reset();
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
    m_creationAttempted = false;
}

void CorumRemotePlayer::setVisible(bool visible) {
    if (m_player) m_player->setVisible(visible);
    if (m_root) m_root->setVisible(visible);
    if(!visible && m_secondary)m_secondary->setVisible(false);
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
    applyCosmetics(pose,reconfigureMode);
    m_appliedState = pose;
}

void CorumRemotePlayer::applyCosmetics(SpectatorGhostPose const& pose,bool modeChanged){
    auto const desired=pose.cosmetics && validSpectatorCosmetics(*pose.cosmetics)
        ?pose.cosmetics->players[m_secondaryRole?1:0]:m_cosmetics.value_or(SpectatorAvatarCosmetics{});
    auto const changed=!m_cosmetics || *m_cosmetics!=desired;
    if(!modeChanged && !changed)return;
    auto* player=m_player.data();auto* game=GameManager::sharedState();
    auto id=[&](std::size_t mode){
        // Pinned GD IconType order is Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing.
        auto count=game?game->countForType(static_cast<IconType>(mode)):1;
        return desired.icons[mode]>=1 && desired.icons[mode]<=count?desired.icons[mode]:1;
    };
    player->updatePlayerFrame(id(0));
    switch(pose.mode){
        case SpectatorGameMode::Cube:break;
        case SpectatorGameMode::Ship:player->updatePlayerShipFrame(id(1));break;
        case SpectatorGameMode::Ball:player->updatePlayerRollFrame(id(2));break;
        case SpectatorGameMode::UFO:player->updatePlayerBirdFrame(id(3));break;
        case SpectatorGameMode::Wave:player->updatePlayerDartFrame(id(4));break;
        case SpectatorGameMode::Robot:player->updatePlayerRobotFrame(id(5));break;
        case SpectatorGameMode::Spider:player->updatePlayerSpiderFrame(id(6));break;
        case SpectatorGameMode::Swing:player->updatePlayerSwingFrame(id(7));break;
    }
    auto color=[](SpectatorColor c){return ccc3(c.r,c.g,c.b);};
    player->setColor(color(desired.primary));player->setSecondColor(color(desired.secondary));
    player->m_hasGlow=desired.glow;
    player->enableCustomGlowColor(color(desired.glowColor));player->updatePlayerGlow();player->updateGlowColor();
    // The remote trail resource is selected once per descriptor, never every pose.
    if(!m_cosmetics || m_cosmetics->trail!=desired.trail){
        player->m_playerStreak=game && desired.trail<=game->countForType(IconType::Special)?desired.trail:1;player->setupStreak();
        isolateNativeCompanions(player,m_effectsRoot.data());
    }
    player->unscheduleAllSelectors();player->m_playEffects=false;
    m_cosmetics=desired;
}

void CorumRemotePlayer::resetEffects(){
    m_effectElapsedMs=-1;m_previousEffects=0;m_lastGhostMs=-100;m_effectSegment=0;
    m_ghostTimes.fill(-1e9);m_ghostCursor=0;
    for(auto& ghost:m_ghosts)if(ghost)ghost->setVisible(false);
    if(!m_player)return;
    auto* p=m_player.data();
    if(p->m_regularTrail)p->m_regularTrail->reset();
    if(p->m_shipStreak)p->m_shipStreak->reset();
    if(p->m_waveTrail)p->m_waveTrail->reset();
    std::array<CCParticleSystemQuad*,11> particles{p->m_playerGroundParticles,p->m_trailingParticles,p->m_shipClickParticles,p->m_vehicleGroundParticles,
        p->m_ufoClickParticles,p->m_robotBurstParticles,p->m_swingBurstParticles1,p->m_swingBurstParticles2,p->m_landParticles0,p->m_landParticles1,p->m_dashParticles};
    for(auto* node:particles)if(node){node->resetSystem();node->stopSystem();node->setVisible(false);}
}

void CorumRemotePlayer::updateEffects(SpectatorRemoteFrame const& frame){
    auto const& pose=*frame.pose;auto* p=m_player.data();
    auto const elapsed=frame.publisherElapsedMs;
    auto const boundary=m_effectElapsedMs<0 || frame.presentationSegment!=m_effectSegment ||
        elapsed<m_effectElapsedMs || elapsed-m_effectElapsedMs>250 || pose.dead || pose.finished;
    auto const delta=m_effectElapsedMs>=0?elapsed-m_effectElapsedMs:0;
    if(boundary)resetEffects();
    m_effectSegment=frame.presentationSegment;m_effectElapsedMs=elapsed;
    if(boundary || delta<=0 || !pose.visible)return;
    auto const dt=static_cast<float>(delta/1000.0);auto const point=CCPoint{pose.x,pose.y};
    auto motion=[&](CCMotionStreak* trail,bool active){if(!trail)return;trail->setVisible(active);
        if(active){trail->setPosition(point);trail->update(dt);}else trail->reset();};
    motion(p->m_regularTrail,pose.mode==SpectatorGameMode::Cube && (pose.effects&1));
    motion(p->m_shipStreak,pose.mode==SpectatorGameMode::Ship && (pose.effects&2));
    if(auto* wave=p->m_waveTrail){auto const active=pose.mode==SpectatorGameMode::Wave && (pose.effects&4);wave->setVisible(active);
        if(active){if(!(m_previousEffects&4)){wave->resumeStroke();wave->unscheduleAllSelectors();}if(wave->m_pointArray && wave->m_pointArray->count()>128)wave->reset();wave->addPoint(point);wave->updateStroke(dt);}else wave->reset();}
    // The native emitters are detached siblings owned by our effects root. Only
    // observed publisher emission flags and publisher delta drive these visuals.
    std::array<CCParticleSystemQuad*,11> particles{p->m_playerGroundParticles,p->m_trailingParticles,p->m_shipClickParticles,p->m_vehicleGroundParticles,
        p->m_ufoClickParticles,p->m_robotBurstParticles,p->m_swingBurstParticles1,p->m_swingBurstParticles2,p->m_landParticles0,p->m_landParticles1,p->m_dashParticles};
    for(std::size_t i=0;i<particles.size();++i)if(auto* node=particles[i]){
        auto const bit=1u<<(i+4);auto active=(pose.effects&bit)!=0;
        if(i==2)active=active&&pose.mode==SpectatorGameMode::Ship;
        if(i==4)active=active&&pose.mode==SpectatorGameMode::UFO;
        if(i==5)active=active&&pose.mode==SpectatorGameMode::Robot;
        if(i==6||i==7)active=active&&pose.mode==SpectatorGameMode::Swing;
        // Imported native presets have finite capacity; refuse oversized modded emitters.
        if(node->getTotalParticles()>512){node->stopSystem();node->setVisible(false);continue;}
        node->setAutoRemoveOnFinish(false);node->setPosition(point);
        if(active && !(m_previousEffects&bit))node->resetSystem();
        if(!active)node->stopSystem();
        node->setVisible(active || node->getParticleCount()>0);node->update(dt);
    }
    // Eight retained native sprite snapshots, no scheduled callbacks/actions or
    // unbounded GhostTrailEffect allocation. This intentionally omits custom shaders.
    if((pose.effects&8) && p->m_iconSprite && elapsed-m_lastGhostMs>=100){
        auto index=m_ghostCursor++%m_ghosts.size();auto& ghost=m_ghosts[index];
        if(!ghost){ghost=CCSprite::create();if(ghost)m_effectsRoot->addChild(ghost.data());}
        if(ghost){ghost->setDisplayFrame(p->m_iconSprite->displayFrame());ghost->setPosition(point);ghost->setRotation(pose.rotation);
            ghost->setScale(pose.mini?.6f:1.f);ghost->setColor(ccc3(m_cosmetics->primary.r,m_cosmetics->primary.g,m_cosmetics->primary.b));m_ghostTimes[index]=elapsed;ghost->setVisible(true);}
        m_lastGhostMs=elapsed;
    }
    for(std::size_t i=0;i<m_ghosts.size();++i)if(auto& ghost=m_ghosts[i];ghost){
        auto const alpha=std::clamp(1.0-(elapsed-m_ghostTimes[i])/400.0,0.0,1.0);
        ghost->setVisible(alpha>0);ghost->setOpacity(static_cast<GLubyte>(100*alpha));
    }
    m_previousEffects=pose.effects;
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
    std::optional<SpectatorRemoteFrame> const& frame, bool animateEffects
) {
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto const workStarted=std::chrono::steady_clock::now();
#endif
    auto const eligible = authorizedDummy && layer && layer->m_level &&
        frame && frame->context.levelId == static_cast<int>(layer->m_level->m_levelID);
    auto const action = m_policy.reconcile(
        eligible, frame ? std::optional(frame->context) : std::nullopt
    );
    if (action == SpectatorRemoteLifecycle::Destroy) {
        destroyVisual();
        m_discardLogged = false;
        m_streamAttachedLogged = false;
        m_creationAttempted = false;
        return;
    }
    if (action == SpectatorRemoteLifecycle::Create || action == SpectatorRemoteLifecycle::Recreate) {
        if (action == SpectatorRemoteLifecycle::Recreate && m_player) {
            log::info("Spectator Remote Player reset");
            destroyVisual();
        }
        m_discardLogged = false;
        m_streamAttachedLogged = false;
        m_creationAttempted = false;
        // WAITING has no native visual allocation. The first validated packet
        // creates it once; an empty response retains an already attached object.
        if (!frame->pose) return;
        m_creationAttempted = true;
        if (!createVisual(layer)) {
            // One creation attempt per context; no per-frame allocation/error loop.
            log::warn("Spectator Remote Player creation failed; polling HUD retained");
            return;
        }
    }
    if (!m_player && !m_creationAttempted && frame && frame->pose && action == SpectatorRemoteLifecycle::None) {
        m_creationAttempted = true;
        if (!createVisual(layer)) return;
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
        m_renderedElapsedMs = frame->publisherElapsedMs;
        applyPose(*m_policy.pose());
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const effectsStarted=std::chrono::steady_clock::now();
#endif
        if(animateEffects)updateEffects(*frame);else if(m_effectElapsedMs>=0)resetEffects();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const effectsUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-effectsStarted).count();
#endif
        if(!m_secondaryRole && frame->pose->dual && frame->pose->player2){
            if(!m_secondary)m_secondary=std::make_unique<CorumRemotePlayer>(true);
            auto second=*frame;second.presentationSegment=frame->pose->player2->segment;second.pose=spectatorSecondaryAsPose(*frame->pose->player2);
            second.pose->cosmetics=frame->pose->cosmetics;
            m_secondary->update(layer,authorizedDummy,second,animateEffects);
        }else if(m_secondary)m_secondary->cleanup();
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        auto const metricNow=std::chrono::steady_clock::now();
        if(!m_secondaryRole && m_metricCount<120 && metricNow>=m_nextMetricAt){
            m_nextMetricAt=metricNow+std::chrono::seconds(1);++m_metricCount;
            log::info("[SpectatorRender] dual={} sequence={} publisherMs={} updateUs={} primaryEffectsUs={} players={} primaryEffectNodes={} secondaryEffectNodes={} poseBytes={} effectsAllowed={}",
                frame->pose->dual,frame->sequence,frame->publisherElapsedMs,
                std::chrono::duration<double,std::micro>(metricNow-workStarted).count(),effectsUs,visualCount(),
                m_effectsRoot?m_effectsRoot->getChildrenCount():0,
                m_secondary && m_secondary->m_effectsRoot?m_secondary->m_effectsRoot->getChildrenCount():0,sizeof(SpectatorGhostPose),animateEffects);
        }
#endif
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
    setVisible(m_policy.pose().has_value() && m_policy.pose()->visible);
}

} // namespace corum::ranked
