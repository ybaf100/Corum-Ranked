#pragma once

#include <cmath>

namespace corum::ranked {

struct SpectatorLocalAuthority {
    bool rankedScene = false;
    bool testMode = false;
    bool placement = false;
    bool userPractice = false;
    bool serverSpectator = false;
    bool ownAttemptAlive = false;
};

[[nodiscard]] constexpr bool spectatorAuthorityReady(
    bool serverSpectator, bool placement, bool ownAttemptAlive
) {
    return serverSpectator && !placement && !ownAttemptAlive;
}

// A scene can become a dummy only after server permission AND the end of its
// real gameplay. Once converted, it cannot become a scoring player again: a
// result/context refresh must retire this scene, not resurrect its old Attempt.
class SpectatorLocalSafety {
public:
    bool activate(SpectatorLocalAuthority const& authority) {
        if (m_active || !authority.rankedScene || authority.testMode ||
            authority.userPractice || !spectatorAuthorityReady(
                authority.serverSpectator, authority.placement, authority.ownAttemptAlive
            )) return false;
        m_active = true;
        return true;
    }

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool blocksEvidence(bool handoffPending = false) const {
        return m_active || handoffPending;
    }
    [[nodiscard]] bool suppressDeath(bool exactLocalPlayer) const {
        return m_active && exactLocalPlayer;
    }
    [[nodiscard]] bool blockPersistence(bool exactSceneLevel, bool handoffPending = false) const {
        return exactSceneLevel && blocksEvidence(handoffPending);
    }

    bool freezeAtCompletionBoundary(bool exactLocalPlayer, double progress) {
        if (m_active && exactLocalPlayer && std::isfinite(progress) && progress >= 99.0) {
            m_frozen = true;
        }
        return m_active && exactLocalPlayer && m_frozen;
    }
    [[nodiscard]] bool frozen() const { return m_frozen; }
    void resetDummy() { m_frozen = false; }
    void deactivate() { m_active = false; m_frozen = false; }

private:
    bool m_active = false;
    bool m_frozen = false;
};

// Used at every public runtime evidence entry, BEFORE armed-start/self-heal
// exceptions. It deliberately does not gate delivery of pre-existing FIFO data.
[[nodiscard]] constexpr bool spectatorAttemptTransportBlocked(
    bool authoritativeSpectator, bool localDummyOrHandoff
) {
    return authoritativeSpectator || localDummyOrHandoff;
}

} // namespace corum::ranked
