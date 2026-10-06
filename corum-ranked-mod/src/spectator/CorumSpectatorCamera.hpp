#pragma once

#include "../domain/SpectatorCameraPolicy.hpp"
#include <Geode/Geode.hpp>

class GJBaseGameLayer;

namespace corum::ranked {

// No retained/raw PlayerObject target and no local player alias/pose writes.
// Camera ownership is released by the scene before native teardown.
class CorumSpectatorCamera final {
public:
    // Capture the pre-reset waiting viewport without driving native camera.
    void attach(GJBaseGameLayer* layer);
    void prepareNativeFallback(GJBaseGameLayer* layer);
    void update(
        GJBaseGameLayer* layer, bool authorizedDummy,
        std::optional<SpectatorRemoteContext> const& liveContext,
        std::optional<SpectatorRemoteFrame> const& rendered
    );
    void cleanup(GJBaseGameLayer* layer);
    [[nodiscard]] bool ownsCamera() const { return m_policy.ownsCamera(); }
    [[nodiscard]] SpectatorCameraState state() const { return m_policy.state(); }

private:
    void applyViewport(GJBaseGameLayer* layer, SpectatorCameraPoint target);
    SpectatorCameraPolicy m_policy;
    bool m_attached = false;
    bool m_previousForceX = false;
    bool m_previousForceY = false;
    float m_previousTargetY = 0;
    cocos2d::CCPoint m_previousOrigin;
};

} // namespace corum::ranked
