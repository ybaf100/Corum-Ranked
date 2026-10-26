#pragma once
#include "SpectatorProtocol.hpp"
#include "SpectatorRemotePolicy.hpp"
#include <algorithm>
#include <deque>
#include <vector>

namespace corum::ranked {
struct SpectatorPublishSample {
    std::int64_t sequence = 0, elapsedMs = 0;
    SpectatorGhostPose pose;
};
// Native capture is independent of HTTP ACK. A single ordered, immutable batch
// is retried until acknowledged; there are never parallel unordered POSTs.
class SpectatorPublishQueue {
public:
    bool capture(std::int64_t elapsedMs, SpectatorGhostPose const& pose) {
        if (m_terminal || !finiteSpectatorPose(pose)) return false;
        auto const urgent = pose.discontinuity.has_value() || pose.cameraSnap || pose.dead || pose.finished;
        if (!urgent && elapsedMs < m_nextSampleMs) return false;
        if (m_pending.size() >= 64) return false; // bounded; caller restarts the presentation generation
        auto const time = std::max<std::int64_t>({0, elapsedMs, m_lastElapsedMs});
        m_pending.push_back({m_sequence++, time, pose});
        m_lastElapsedMs = time; m_nextSampleMs = time + kSpectatorPublishIntervalMs;
        m_terminal = pose.dead || pose.finished;
        return true;
    }
    std::vector<SpectatorPublishSample> batch() const {
        auto const count = std::min<std::size_t>(16, m_pending.size());
        return {m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(count)};
    }
    void acknowledge(std::int64_t sequence) {
        while (!m_pending.empty() && m_pending.front().sequence <= sequence) m_pending.pop_front();
    }
    bool empty() const { return m_pending.empty(); }
    bool full() const { return m_pending.size() >= 64; }
    std::size_t size() const { return m_pending.size(); }
    std::int64_t nextSequence() const { return m_sequence; }
    std::int64_t lastElapsedMs() const { return m_lastElapsedMs; }
private:
    std::deque<SpectatorPublishSample> m_pending;
    std::int64_t m_sequence = 0, m_nextSampleMs = 0, m_lastElapsedMs = -1;
    bool m_terminal = false;
};
}
