#pragma once

namespace corum::ranked {

// Completion is blocked before vanilla runs. During the remaining synchronous
// reset/quit/exit cleanup, additionally use GD's native no-save mode for the
// exact spectator scene. Never modify the genuine player's level or test flag
// permanently, and never detour GJGameLevel's score/string ABI.
class ScopedSpectatorPersistenceMode {
public:
    ScopedSpectatorPersistenceMode(bool& nativeTestMode, bool exactSpectatorScene)
        : m_nativeTestMode(nativeTestMode), m_previous(nativeTestMode), m_engaged(exactSpectatorScene) {
        if (exactSpectatorScene) m_nativeTestMode = true;
    }
    ~ScopedSpectatorPersistenceMode() { if (m_engaged) m_nativeTestMode = m_previous; }
    ScopedSpectatorPersistenceMode(ScopedSpectatorPersistenceMode const&) = delete;
    ScopedSpectatorPersistenceMode& operator=(ScopedSpectatorPersistenceMode const&) = delete;

private:
    bool& m_nativeTestMode;
    bool m_previous;
    bool m_engaged;
};

} // namespace corum::ranked
