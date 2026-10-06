#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace corum::ranked {
enum class SpectatorNativePath : std::size_t {
    BlockedPlayerUpdate, CheckCollisions, CollisionObjects, TouchedObject,
    TouchedTrigger, ActivationQuery, ActivationAllowed, Teleport, Spawn,
    Commands, Follow, DynamicMove, TriggerObject, Count
};

// Observation only. No GD calls, no trigger activation and no physics authority.
// Hookable native paths feed this probe only during an authorized Debug TEST
// world frame. Inline iOS/Windows touchedObject is observed via the native
// activated-ID map at the caller, never installed as an unsupported detour.
struct SpectatorNativeProbe {
    std::array<std::uint64_t, static_cast<std::size_t>(SpectatorNativePath::Count)> calls{};
    std::array<int, 64> triggerIds{};
    std::size_t triggerIdsUsed = 0;
    std::uint64_t frames = 0, repeatedTriggerCalls = 0, logs = 0;
    void beginFrame() { ++frames; triggerIdsUsed = 0; }
    void note(SpectatorNativePath path) { ++calls[static_cast<std::size_t>(path)]; }
    void trigger(int id) {
        note(SpectatorNativePath::TriggerObject);
        auto end = triggerIds.begin() + triggerIdsUsed;
        if (std::find(triggerIds.begin(), end, id) != end) ++repeatedTriggerCalls;
        else if (triggerIdsUsed < triggerIds.size()) triggerIds[triggerIdsUsed++] = id;
    }
    std::uint64_t count(SpectatorNativePath path) const { return calls[static_cast<std::size_t>(path)]; }
    void clearWindow() { calls.fill(0); frames = repeatedTriggerCalls = 0; }
};

inline thread_local SpectatorNativeProbe* activeSpectatorNativeProbe = nullptr;
inline thread_local void const* spectatorNativeProbeLayer = nullptr;
inline thread_local void const* spectatorNativeProbePlayer = nullptr;
class ScopedSpectatorNativeProbe final {
    SpectatorNativeProbe* m_previous;
    void const* m_layer;
    void const* m_player;
public:
    ScopedSpectatorNativeProbe(SpectatorNativeProbe& probe, void const* layer, void const* player, bool enabled)
        : m_previous(activeSpectatorNativeProbe), m_layer(spectatorNativeProbeLayer), m_player(spectatorNativeProbePlayer) {
        activeSpectatorNativeProbe = enabled ? &probe : nullptr;
        spectatorNativeProbeLayer = enabled ? layer : nullptr;
        spectatorNativeProbePlayer = enabled ? player : nullptr;
        if (enabled) probe.beginFrame();
    }
    ~ScopedSpectatorNativeProbe() {
        activeSpectatorNativeProbe = m_previous;
        spectatorNativeProbeLayer = m_layer;
        spectatorNativeProbePlayer = m_player;
    }
};
inline void noteSpectatorNativePath(void const* layer, void const* player, SpectatorNativePath path) {
    if (activeSpectatorNativeProbe && layer == spectatorNativeProbeLayer &&
        (!player || player == spectatorNativeProbePlayer)) activeSpectatorNativeProbe->note(path);
}
inline void noteSpectatorNativeTrigger(void const* layer, int objectId) {
    if (activeSpectatorNativeProbe && layer == spectatorNativeProbeLayer) activeSpectatorNativeProbe->trigger(objectId);
}
} // namespace corum::ranked
