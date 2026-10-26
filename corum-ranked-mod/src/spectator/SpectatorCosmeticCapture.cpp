#include "SpectatorPlayerCapture.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/PlayerObject.hpp>

namespace corum::ranked {
SpectatorCosmetics captureSpectatorCosmetics(PlayerObject* first,PlayerObject* second){
    SpectatorCosmetics result;
    auto* game=GameManager::sharedState();if(!game)return result;
    std::array<int,8> icons{game->getPlayerFrame(),game->getPlayerShip(),game->getPlayerBall(),game->getPlayerBird(),
        game->getPlayerDart(),game->getPlayerRobot(),game->getPlayerSpider(),game->getPlayerSwing()};
    auto convert=[](cocos2d::ccColor3B c){return SpectatorColor{c.r,c.g,c.b};};
    for(std::size_t i=0;i<2;++i){auto* native=i?second:first;auto& p=result.players[i];p.icons=icons;
        p.primary=convert(native?native->m_playerColor1:game->colorForIdx(i?game->getPlayerColor2():game->getPlayerColor()));
        p.secondary=convert(native?native->m_playerColor2:game->colorForIdx(i?game->getPlayerColor():game->getPlayerColor2()));
        p.glow=native?native->m_hasGlow:game->getPlayerGlow();
        p.glowColor=convert(native&&native->m_hasCustomGlowColor?native->m_glowColor:game->colorForIdx(game->getPlayerGlowColor()));
        p.trail=std::clamp(game->getPlayerStreak(),1,1000);
        for(auto& id:p.icons)if(id<1||id>10000)id=1;
    }
    return result;
}
unsigned captureSpectatorEffects(PlayerObject* player,SpectatorGameMode mode){
    if(!player)return 0;
    SpectatorGhostPose pose;pose.mode=mode;
    if(player->m_regularTrail && player->m_regularTrail->isVisible() && pose.mode==SpectatorGameMode::Cube)pose.effects|=1;
    if(player->m_shipStreak && player->m_shipStreak->isVisible() && pose.mode==SpectatorGameMode::Ship)pose.effects|=2;
    if(player->m_waveTrail && player->m_waveTrail->isVisible() && pose.mode==SpectatorGameMode::Wave)pose.effects|=4;
    if(player->m_ghostTrail && player->m_ghostTrail->isVisible())pose.effects|=8;
    std::array<cocos2d::CCParticleSystemQuad*,11> particles{player->m_playerGroundParticles,player->m_trailingParticles,player->m_shipClickParticles,
        player->m_vehicleGroundParticles,player->m_ufoClickParticles,player->m_robotBurstParticles,player->m_swingBurstParticles1,
        player->m_swingBurstParticles2,player->m_landParticles0,player->m_landParticles1,player->m_dashParticles};
    for(std::size_t i=0;i<particles.size();++i)if(auto* particle=particles[i];particle && particle->isActive() && particle->isVisible())pose.effects|=1u<<(i+4);

    return pose.effects;
}
}
