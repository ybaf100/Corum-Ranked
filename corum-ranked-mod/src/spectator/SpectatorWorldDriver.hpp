#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
#include <array>
#endif

class PlayLayer;
class PlayerObject;

namespace corum::ranked {
class SpectatorWorldDriver;

// Only the authenticated adapter can open a write. Native portal/placement
// callbacks cannot independently move the no-authority player away from it.
class ScopedSpectatorWorldDriverWrite {
public:
    ~ScopedSpectatorWorldDriverWrite() { s_player = m_previous; }
    static bool allows(PlayerObject const* player) { return player && s_player == player; }
    ScopedSpectatorWorldDriverWrite(ScopedSpectatorWorldDriverWrite const&) = delete;
    ScopedSpectatorWorldDriverWrite& operator=(ScopedSpectatorWorldDriverWrite const&) = delete;
private:
    friend class SpectatorWorldDriver;
    explicit ScopedSpectatorWorldDriverWrite(PlayerObject* player) : m_previous(s_player) { s_player = player; }
    inline static thread_local PlayerObject const* s_player = nullptr;
    PlayerObject const* m_previous;
};

// Native m_player1 remains hidden and has no physics/result authority. Its
// level-space coordinates are an input to vanilla world/trigger processing.
class SpectatorWorldDriver {
public:
    bool apply(PlayLayer* layer, bool authorizedDummy, std::optional<SpectatorRemoteFrame> const& frame);
    void reconcile(PlayLayer* layer);
    bool owns(PlayerObject const* player) const { return player && player == m_player; }
    bool contextChanged(SpectatorRemoteContext const& context) const { return m_frame && m_frame->context != context; }
    void reset();
    std::optional<SpectatorRemoteFrame> const& frame() const { return m_frame; }
private:
    void write(PlayLayer* layer, bool resetHistory);
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
    void snapshotFollowHistory();
    void restoreFollowHistory();
    std::array<float, 200> m_followHistory{};
    int m_followIndex = 0;
    float m_followAccumulator = 0;
    bool m_hasFollowHistory = false;
#endif
    PlayerObject* m_player = nullptr;
    std::optional<SpectatorRemoteFrame> m_frame;
    std::optional<SpectatorGhostPose> m_previous;
    double m_followRemainderMs = 0;
};
} // namespace corum::ranked
