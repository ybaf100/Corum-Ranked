#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
#include "../domain/SpectatorProtocol.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace corum::ranked::debug {
inline constexpr int kDebugSpectateLevelId = 88350599;
inline constexpr int kDebugSpectateFormatVersion = 2;
inline constexpr char kDebugSpectateMapName[] = "VOICE";
inline constexpr char kDebugSpectateCreator[] = "LiteN1ng";
enum class DebugSpectateMode { None, PreparingRecord, Recording, FinalizingRecord, PreparingPlayback, Playback };
struct DebugSpectateFrame {
    std::int64_t sequence = 0;
    std::int64_t elapsedMs = 0;
    SpectatorGhostPose pose;
};
// Recording clock only. TEST has no client replay clock/recording/frame file.
class DebugSpectateCapture {
public:
    bool capture(std::int64_t elapsedMs, SpectatorGhostPose const& pose, bool terminal = false) {
        if (m_ended || !finiteSpectatorPose(pose) || (pose.dead && pose.finished)) return false;
        if (!terminal && !pose.discontinuity && !pose.cameraSnap && elapsedMs < m_nextSampleMs) return false;
        if (m_pending.size() >= 256 || m_nextSequence >= 12001 || elapsedMs > 600000) {
            m_ended = true; m_completed = false; return false;
        }
        auto const time = std::max<std::int64_t>(std::max<std::int64_t>(0, elapsedMs), m_lastElapsedMs + 1);
        m_pending.push_back({m_nextSequence++, time, pose});
        m_lastElapsedMs = time;
        m_nextSampleMs = time + kSpectatorPublishIntervalMs;
        if (terminal) { m_ended = true; m_completed = pose.dead || pose.finished; }
        return true;
    }
    void cancel() { m_ended = true; m_completed = false; }
    std::vector<DebugSpectateFrame> batch() const {
        auto const count = std::min<std::size_t>(16, m_pending.size());
        return {m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(count)};
    }
    void acknowledge(std::int64_t sequence) {
        while (!m_pending.empty() && m_pending.front().sequence <= sequence) m_pending.pop_front();
    }
    bool empty() const { return m_pending.empty(); }
    bool ended() const { return m_ended; }
    bool completed() const { return m_completed; }
    std::int64_t nextSequence() const { return m_nextSequence; }
private:
    std::deque<DebugSpectateFrame> m_pending;
    std::int64_t m_nextSequence = 0, m_nextSampleMs = 0, m_lastElapsedMs = -1;
    bool m_ended = false, m_completed = false;
};
} // namespace corum::ranked::debug
