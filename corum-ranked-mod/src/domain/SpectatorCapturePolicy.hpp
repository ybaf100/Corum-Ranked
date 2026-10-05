#pragma once
#include "SpectatorRemotePolicy.hpp"

namespace corum::ranked {

// Observe native frames independently from the 50ms HTTP cadence. Event proof
// survives an unsampled frame / busy publisher; no PlayerObject is simulated.
class SpectatorCapturePolicy {
public:
    void observe(SpectatorGhostPose pose) {
        if (!finiteSpectatorPose(pose)) return;
        if (pose.camera && !validSpectatorCamera(*pose.camera)) pose.camera.reset();
        if (m_latest && m_latest->camera && pose.camera &&
            std::hypot(pose.camera->x - m_latest->camera->x,
                       pose.camera->y - m_latest->camera->y) > 400.0f) m_cameraSnap = true;
        m_latest = pose;
    }
    void nativeSnap(SpectatorDiscontinuity type, SpectatorGhostPose const& before,
                    SpectatorGhostPose const& after) {
        if (!finiteSpectatorPose(before) || !finiteSpectatorPose(after)) return;
        SpectatorSnapEvidence snap{type, before.x, before.y, after.x, after.y};
        if (!finiteSpectatorSnap(snap)) return;
        if (m_snap) {
            // Coalesce only an observed native event chain. Never bridge an
            // arbitrary correction or an unrelated callback's coordinates.
            if (std::hypot(before.x - m_snap->toX, before.y - m_snap->toY) > 400.0f) return;
            snap.fromX = m_snap->fromX; snap.fromY = m_snap->fromY;
            if (m_snap->type == SpectatorDiscontinuity::Teleport) snap.type = m_snap->type;
            if (!finiteSpectatorSnap(snap)) return;
        }
        m_snap = snap; m_cameraSnap = true;
    }
    bool stableNativeFrame(double nativeTime, bool nativeReady) {
        if (!nativeReady || !std::isfinite(nativeTime) || nativeTime <= 0 || nativeTime <= m_nativeTime) {
            m_stableFrames = 0; m_nativeTime = nativeTime; m_nativePose.reset(); return false;
        }
        m_nativeTime = nativeTime;
        auto const moved = m_latest && m_nativePose &&
            (std::hypot(m_latest->x - m_nativePose->x, m_latest->y - m_nativePose->y) > 0.01f ||
             m_latest->mode != m_nativePose->mode || m_latest->rotation != m_nativePose->rotation);
        m_nativePose = m_latest;
        // Default/static entrance poses do not arm capture just because a clock
        // advanced. Two completed native gameplay frames establish readiness.
        if (m_stableFrames < 2) m_stableFrames = moved ? m_stableFrames + 1 : 0;
        return m_stableFrames >= 2;
    }
    std::optional<SpectatorGhostPose> snapshot() const {
        if (!m_latest) return std::nullopt;
        auto pose = *m_latest; pose.discontinuity = m_snap; pose.cameraSnap = m_cameraSnap;
        return pose;
    }
    void sampled() { m_snap.reset(); m_cameraSnap = false; }
    void reset() { m_latest.reset(); m_nativePose.reset(); sampled(); m_nativeTime = 0; m_stableFrames = 0; }
private:
    std::optional<SpectatorGhostPose> m_latest;
    std::optional<SpectatorGhostPose> m_nativePose;
    std::optional<SpectatorSnapEvidence> m_snap;
    double m_nativeTime = 0;
    int m_stableFrames = 0;
    bool m_cameraSnap = false;
};
} // namespace corum::ranked
