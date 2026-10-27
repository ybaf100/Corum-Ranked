#pragma once
#include "SpectatorRemotePolicy.hpp"
#include <algorithm>
#include <deque>

namespace corum::ranked {

struct SpectatorBufferedSample {
    std::int64_t sequence = -1;
    double publisherElapsedMs = 0;
    SpectatorGhostPose pose;
    // Validated client-side segment boundary, not another wire event. Evidence
    // remains in pose.discontinuity; this also covers bounded legacy snaps.
    std::uint64_t segment = 0;
};

// One publisher clock for pose AND camera. HTTP arrival is used only for clock
// progression and a measured buffer budget, never as a sample's timestamp.
class SpectatorInterpolationBuffer {
public:
    static constexpr std::size_t kMaxSamples = 64;
    static constexpr double kMaxSpanMs = 2000;
    static constexpr double kMinDelayMs = 100, kMaxDelayMs = 800;
    void setSourcePaused(bool paused, double nowMs) {
        if (!std::isfinite(nowMs)) return;
        if (paused != m_sourcePaused) {
            m_lastRenderWallMs = nowMs;
            // Resume with the measured headroom, rather than advancing through
            // wall-clock pause time or counting an intentional pause as loss.
            if (!paused) m_running = false;
            m_starved = false; m_starvedSinceMs = 0;
        }
        m_sourcePaused = paused;
    }

    void noteResponse(double nowMs, double rttMs, double pollGapMs, std::size_t packets) {
        if (!std::isfinite(nowMs)) return;
        auto const interval = m_responses ? nowMs - m_lastResponseMs : rttMs + pollGapMs;
        if (interval > 0 && interval <= 3000) {
            m_arrivalMs = m_responses ? m_arrivalMs * 0.75 + interval * 0.25 : interval;
            if (m_responses) { m_arrivalTotal += interval; ++m_arrivalCount; }
        }
        m_lastResponseMs = nowMs;
        ++m_responses; m_packets += packets;
        if (std::isfinite(rttMs) && rttMs >= 0) m_rttTotal += rttMs;
        updateDelay();
    }

    bool push(SpectatorBufferedSample sample, bool verifiedSnap, double nowMs) {
        if (sample.sequence < 0 || !std::isfinite(sample.publisherElapsedMs) ||
            sample.publisherElapsedMs < 0 || !finiteSpectatorPose(sample.pose)) return false;
        if (!m_samples.empty()) {
            auto const delta = sample.publisherElapsedMs - m_samples.back().publisherElapsedMs;
            if (sample.sequence <= m_samples.back().sequence || delta < 0) return false;
            if (delta >= 20 && delta <= 500) m_sampleMs = m_sampleMs * 0.8 + delta * 0.2;
            if (m_samples.back().pose.dead || m_samples.back().pose.finished) return false;
            // Equal native timestamps are legal (e.g. a terminal at contact).
            if (delta == 0) m_samples.pop_back();
        }
        if (verifiedSnap) ++m_segment;
        // A same-timestamp terminal inherits the current destination segment.
        sample.segment = m_segment;
        if (m_samples.empty() && !m_wallSet) m_renderMs = sample.publisherElapsedMs;
        m_samples.push_back(std::move(sample));
        prune(); updateDelay();
        if (m_samples.size() > kMaxSamples || spanMs() > kMaxSpanMs) {
            // Do not make an unverified catch-up teleport when a client is too
            // far behind. The caller enters its existing safe stream fallback.
            clear(); return false;
        }
        if (!m_running && (m_samples.back().publisherElapsedMs - m_renderMs >= m_delayMs ||
            m_samples.back().pose.dead || m_samples.back().pose.finished)) {
            m_running = true; m_starved = false; m_starvedSinceMs = 0;
            m_lastRenderWallMs = nowMs; m_wallSet = true;
        }
        return true;
    }

    std::optional<SpectatorBufferedSample> render(double nowMs) {
        if (m_samples.empty() || !std::isfinite(nowMs)) return std::nullopt;
        auto const delta = m_wallSet ? std::max(0.0, nowMs - m_lastRenderWallMs) : 0.0;
        m_lastRenderWallMs = nowMs; m_wallSet = true;
        if (m_terminalRendered) return m_samples.back();
        auto const latest = m_samples.back().publisherElapsedMs;
        auto const terminal = m_samples.back().pose.dead || m_samples.back().pose.finished;
        if (!m_running && (latest - m_renderMs >= m_delayMs || terminal || m_sourcePaused)) {
            // First pose is visible immediately. Warm-up never skips its early
            // movement; after warm-up the publisher clock advances at 1x.
            m_running = true; m_starved = false; m_starvedSinceMs = 0;
        } else if (m_running) {
            auto const wanted = m_renderMs + delta;
            m_renderMs = std::min(wanted, latest);
            if (wanted > latest + 0.001 && !terminal && !m_sourcePaused) {
                if (!m_starved) { ++m_starvations; m_starvedSinceMs = nowMs; }
                m_starved = true;
                if (nowMs - m_starvedSinceMs >= 250) m_running = false;
            } else {
                m_starved = false; m_starvedSinceMs = 0;
            }
        }
        prune();
        auto result = m_samples.front();
        result.publisherElapsedMs = m_renderMs;
        // prune() switches ALL fields atomically at the destination timestamp.
        // Until then stay in the source segment; never sweep B -> snap C.
        if (m_samples.size() > 1 && m_samples[0].segment == m_samples[1].segment) {
            auto const& older = m_samples[0]; auto const& newer = m_samples[1];
            auto const interval = newer.publisherElapsedMs - older.publisherElapsedMs;
            auto const t = interval > 0 ? std::clamp((m_renderMs - older.publisherElapsedMs) / interval, 0.0, 1.0) : 0.0;
            // All discrete state belongs to the older boundary, not the latest
            // HTTP packet. Only continuous position/rotation/camera interpolate.
            auto const lerp = [t](float a, float b) { return static_cast<float>(a + (b - a) * t); };
            result.pose.x = lerp(older.pose.x, newer.pose.x);
            result.pose.y = lerp(older.pose.y, newer.pose.y);
            result.pose.rotation = static_cast<float>(older.pose.rotation +
                std::remainder(newer.pose.rotation - older.pose.rotation, 360.0f) * t);
            if(older.pose.dual && newer.pose.dual && older.pose.player2 && newer.pose.player2){
                auto const& a=*older.pose.player2;auto const& b=*newer.pose.player2;
                if(a.segment==b.segment && !b.discontinuity && std::hypot(b.x-a.x,b.y-a.y)<=400 && a.mode==b.mode && a.upsideDown==b.upsideDown){
                    result.pose.player2->x=lerp(a.x,b.x);result.pose.player2->y=lerp(a.y,b.y);
                    result.pose.player2->rotation=static_cast<float>(a.rotation+std::remainder(b.rotation-a.rotation,360.f)*t);
                }
            }
            if(older.pose.audio && newer.pose.audio && older.pose.audio->trackHash==newer.pose.audio->trackHash &&
               newer.pose.audio->positionMs>=older.pose.audio->positionMs && newer.pose.audio->positionMs-older.pose.audio->positionMs<=250)
                result.pose.audio->positionMs=older.pose.audio->positionMs+(newer.pose.audio->positionMs-older.pose.audio->positionMs)*t;
            if(older.pose.progress && newer.pose.progress)result.pose.progress=lerp(*older.pose.progress,*newer.pose.progress);
            if (older.pose.camera && newer.pose.camera && !newer.pose.cameraSnap) {
                result.pose.camera = SpectatorCameraOrigin{lerp(older.pose.camera->x, newer.pose.camera->x),
                    lerp(older.pose.camera->y, newer.pose.camera->y)};
                if(older.pose.camera->transform && newer.pose.camera->transform)
                    result.pose.camera->transform=interpolateSpectatorCameraTransform(*older.pose.camera->transform,*newer.pose.camera->transform,t);
            }
        } else if (m_samples.size() > 1 && m_samples[1].pose.discontinuity) {
            // A native event proves its source as well as its destination. Move
            // continuously TO that source, never THROUGH the teleport. Holding
            // the whole preceding pose used to stop horizontal Spider travel
            // at every click even with a healthy presentation buffer.
            auto const& older = m_samples[0]; auto const& newer = m_samples[1];
            auto const& snap = *newer.pose.discontinuity;
            auto const interval = newer.publisherElapsedMs - older.publisherElapsedMs;
            auto const t = interval > 0 ? std::clamp((m_renderMs - older.publisherElapsedMs) / interval, 0.0, 1.0) : 0.0;
            result.pose.x = static_cast<float>(older.pose.x + (snap.fromX - older.pose.x) * t);
            result.pose.y = static_cast<float>(older.pose.y + (snap.fromY - older.pose.y) * t);
            if (older.pose.camera && snap.fromCamera && validSpectatorCamera(*snap.fromCamera)) {
                result.pose.camera = SpectatorCameraOrigin {
                    static_cast<float>(older.pose.camera->x + (snap.fromCamera->x - older.pose.camera->x) * t),
                    static_cast<float>(older.pose.camera->y + (snap.fromCamera->y - older.pose.camera->y) * t)};
                result.pose.camera->transform=older.pose.camera->transform;
            }
            // Rotation, gravity, mode and camera remain in the source segment;
            // prune() applies the entire destination atomically at its timestamp.
        }
        if ((result.pose.dead || result.pose.finished) && m_renderMs >= latest) m_terminalRendered = true;
        return result;
    }

    void clear() {
        m_samples.clear(); m_renderMs = 0; m_lastRenderWallMs = 0;
        m_segment = 0;
        m_sourcePaused = false;
        m_running = m_wallSet = m_starved = m_terminalRendered = false; m_starvedSinceMs = 0;
    }
    void reset() { *this = SpectatorInterpolationBuffer{}; }
    [[nodiscard]] std::size_t size() const { return m_samples.size(); }
    [[nodiscard]] double spanMs() const { return m_samples.empty() ? 0 : m_samples.back().publisherElapsedMs - m_samples.front().publisherElapsedMs; }
    [[nodiscard]] double renderTimeMs() const { return m_renderMs; }
    [[nodiscard]] double latestTimeMs() const { return m_samples.empty() ? 0 : m_samples.back().publisherElapsedMs; }
    [[nodiscard]] double delayMs() const { return m_delayMs; }
    [[nodiscard]] std::uint64_t starvations() const { return m_starvations; }
    [[nodiscard]] bool buffering() const { return m_starved && !m_running; }
    [[nodiscard]] bool starving() const { return m_starved; }
    [[nodiscard]] bool terminalRendered() const { return m_terminalRendered; }
    [[nodiscard]] double averageRttMs() const { return m_responses ? m_rttTotal / m_responses : 0; }
    [[nodiscard]] double averageArrivalMs() const { return m_arrivalCount ? m_arrivalTotal / m_arrivalCount : 0; }
    [[nodiscard]] double packetsPerResponse() const { return m_responses ? static_cast<double>(m_packets) / m_responses : 0; }

private:
    void updateDelay() { m_delayMs = std::clamp(std::ceil(std::max(2 * m_sampleMs, m_arrivalMs + 2*m_sampleMs)), kMinDelayMs, kMaxDelayMs); }
    void prune() {
        while (m_samples.size() > 1 && m_samples[1].publisherElapsedMs <= m_renderMs) m_samples.pop_front();
    }
    std::deque<SpectatorBufferedSample> m_samples;
    double m_renderMs = 0, m_lastRenderWallMs = 0, m_starvedSinceMs = 0;
    double m_delayMs = 100, m_sampleMs = 50, m_arrivalMs = 0;
    double m_lastResponseMs = 0, m_rttTotal = 0, m_arrivalTotal = 0;
    std::uint64_t m_responses = 0, m_packets = 0, m_arrivalCount = 0, m_starvations = 0;
    std::uint64_t m_segment = 0;
    bool m_running = false, m_wallSet = false, m_starved = false, m_terminalRendered = false;
    bool m_sourcePaused = false;
};
} // namespace corum::ranked
