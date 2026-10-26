#pragma once
#include "SpectatorRemotePolicy.hpp"

namespace corum::ranked {

// Observe native frames independently from the 50ms HTTP cadence. Event proof
// survives an unsampled frame / busy publisher; no PlayerObject is simulated.
class SpectatorCapturePolicy {
public:
    void observe(SpectatorGhostPose pose) {
        if (!finiteSpectatorPose(pose)) { m_latest.reset(); return; }
        if (pose.camera && !validSpectatorCamera(*pose.camera)) pose.camera.reset();
        if (m_latest && m_latest->camera && pose.camera &&
            std::hypot(pose.camera->x - m_latest->camera->x,
                       pose.camera->y - m_latest->camera->y) > 400.0f) m_cameraSnap = true;
        if(m_latest && m_latest->camera && m_latest->camera->transform && pose.camera && pose.camera->transform &&
           !pose.camera->transform->nativeTweening){
            auto const& old=*m_latest->camera->transform;auto const& next=*pose.camera->transform;
            if(std::abs(old.zoom-next.zoom)>.0001f || std::abs(std::remainder(next.angle-old.angle,360.f))>.0001f)m_cameraSnap=true;
            for(std::size_t i=0;i<old.layers.size();++i)if(old.layers[i].present!=next.layers[i].present ||
                std::abs(old.layers[i].scaleX-next.layers[i].scaleX)>.0001f || std::abs(old.layers[i].scaleY-next.layers[i].scaleY)>.0001f ||
                std::abs(std::remainder(next.layers[i].rotation-old.layers[i].rotation,360.f))>.0001f)m_cameraSnap=true;
        }
        m_latest = pose;
    }
    void nativeSnap(SpectatorDiscontinuity type, SpectatorGhostPose const& before,
                    SpectatorGhostPose const& after) {
        if (!finiteSpectatorPose(before) || !finiteSpectatorPose(after)) return;
        SpectatorSnapEvidence snap{type, before.x, before.y, after.x, after.y};
        if (before.camera && validSpectatorCamera(*before.camera)) snap.fromCamera = before.camera;
        if (!finiteSpectatorSnap(snap)) return;
        if (m_snap) {
            // Coalesce only an observed native event chain. Never bridge an
            // arbitrary correction or an unrelated callback's coordinates.
            if (std::hypot(before.x - m_snap->toX, before.y - m_snap->toY) > 400.0f) return;
            snap.fromX = m_snap->fromX; snap.fromY = m_snap->fromY;
            snap.fromCamera = m_snap->fromCamera;
            if (m_snap->type == SpectatorDiscontinuity::Teleport) snap.type = m_snap->type;
            if (!finiteSpectatorSnap(snap)) return;
        }
        m_snap = snap; m_cameraSnap = true;
    }
    bool stableNativeFrame(double nativeTime, bool nativeReady) {
        if (!nativeReady || !m_latest || !std::isfinite(nativeTime) || nativeTime < 0 ||
            (m_seeded && nativeTime < m_nativeTime)) {
            m_nativeTime = nativeTime; return false;
        }
        m_nativeTime = nativeTime;
        // The caller proves a completed, started native world (not init/reset).
        // A stationary initial player is a valid world seed; requiring motion
        // used to discard early trigger history before playback normalization.
        m_seeded = true;
        return true;
    }
    std::optional<SpectatorGhostPose> snapshot() const {
        if (!m_latest) return std::nullopt;
        auto pose = *m_latest; pose.discontinuity = m_snap; pose.cameraSnap = m_cameraSnap;
        return pose;
    }
    void sampled() { m_snap.reset(); m_cameraSnap = false; }
    void reset() { m_latest.reset(); sampled(); m_nativeTime = 0; m_seeded = false; }
private:
    std::optional<SpectatorGhostPose> m_latest;
    std::optional<SpectatorSnapEvidence> m_snap;
    double m_nativeTime = 0;
    bool m_cameraSnap = false;
    bool m_seeded = false;
};
} // namespace corum::ranked
