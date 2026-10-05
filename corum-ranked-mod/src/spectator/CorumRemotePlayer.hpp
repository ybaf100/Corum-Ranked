#pragma once

#include "../domain/SpectatorRemotePolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>

class PlayLayer;

namespace corum::ranked {

// Owns a dedicated render-only PlayerObject, never the scene's m_player1/2.
// Retained nodes make cleanup idempotent even after parent scene teardown.
class CorumRemotePlayer final {
public:
    CorumRemotePlayer() = default;
    ~CorumRemotePlayer();
    CorumRemotePlayer(CorumRemotePlayer const&) = delete;
    CorumRemotePlayer& operator=(CorumRemotePlayer const&) = delete;

    void update(
        PlayLayer* layer, bool authorizedDummy,
        std::optional<SpectatorRemoteFrame> const& frame
    );
    void cleanup();
    [[nodiscard]] PlayerObject* player() const { return m_player.data(); }
    [[nodiscard]] std::optional<SpectatorRemoteFrame> cameraFrame() const {
        return m_player && m_poseCurrent ? m_policy.renderedFrame() : std::nullopt;
    }

private:
    bool createVisual(PlayLayer* layer);
    void destroyVisual();
    void applyPose(SpectatorGhostPose const& pose);
    void applyGameplayState(SpectatorGhostPose const& pose);
    void setVisible(bool visible);

    SpectatorRemotePolicy m_policy;
    geode::Ref<cocos2d::CCLayer> m_root;
    geode::Ref<cocos2d::CCNode> m_effectsRoot;
    geode::Ref<PlayerObject> m_player;
    bool m_discardLogged = false;
    bool m_streamAttachedLogged = false;
    bool m_poseCurrent = false;
    bool m_creationAttempted = false;
    std::optional<SpectatorGhostPose> m_appliedState;
};

} // namespace corum::ranked
