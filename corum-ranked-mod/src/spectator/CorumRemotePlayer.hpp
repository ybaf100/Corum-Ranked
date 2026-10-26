#pragma once

#include "../domain/SpectatorRemotePolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <chrono>

class PlayLayer;

namespace corum::ranked {

// Owns a dedicated render-only PlayerObject, never the scene's m_player1/2.
// Retained nodes make cleanup idempotent even after parent scene teardown.
class CorumRemotePlayer final {
public:
    explicit CorumRemotePlayer(bool secondary=false):m_secondaryRole(secondary){}
    ~CorumRemotePlayer();
    CorumRemotePlayer(CorumRemotePlayer const&) = delete;
    CorumRemotePlayer& operator=(CorumRemotePlayer const&) = delete;

    void update(
        PlayLayer* layer, bool authorizedDummy,
        std::optional<SpectatorRemoteFrame> const& frame, bool animateEffects=true
    );
    void cleanup();
    [[nodiscard]] PlayerObject* player() const { return m_player.data(); }
    [[nodiscard]] PlayerObject* secondaryPlayer()const{return m_secondary?m_secondary->player():nullptr;}
    [[nodiscard]] std::size_t visualCount()const{return (m_player?1:0)+(secondaryPlayer()?1:0);}

    [[nodiscard]] std::optional<SpectatorRemoteFrame> cameraFrame() const {
        auto frame = m_player && m_poseCurrent ? m_policy.renderedFrame() : std::nullopt;
        if (frame) frame->publisherElapsedMs = m_renderedElapsedMs;
        return frame;
    }

private:
    bool createVisual(PlayLayer* layer);
    void destroyVisual();
    void applyPose(SpectatorGhostPose const& pose);
    void applyGameplayState(SpectatorGhostPose const& pose);
    void setVisible(bool visible);
    void applyCosmetics(SpectatorGhostPose const& pose,bool modeChanged);
    void updateEffects(SpectatorRemoteFrame const& frame);
    void resetEffects();


#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    std::chrono::steady_clock::time_point m_nextMetricAt{};
    unsigned m_metricCount=0;
#endif
    bool m_secondaryRole=false;
    std::unique_ptr<CorumRemotePlayer> m_secondary;
    std::optional<SpectatorAvatarCosmetics> m_cosmetics;
    std::array<geode::Ref<cocos2d::CCSprite>,8> m_ghosts;
    std::array<double,8> m_ghostTimes{};
    unsigned m_ghostCursor=0,m_previousEffects=0;
    double m_effectElapsedMs=-1,m_lastGhostMs=-100;
    std::uint64_t m_effectSegment=0;
    SpectatorRemotePolicy m_policy;
    geode::Ref<cocos2d::CCLayer> m_root;
    geode::Ref<cocos2d::CCNode> m_effectsRoot;
    geode::Ref<PlayerObject> m_player;
    bool m_discardLogged = false;
    bool m_streamAttachedLogged = false;
    bool m_poseCurrent = false;
    bool m_creationAttempted = false;
    double m_renderedElapsedMs = 0;
    std::optional<SpectatorGhostPose> m_appliedState;
};

} // namespace corum::ranked
