#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
#include "../domain/SpectatorProtocol.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include <string_view>

namespace corum::ranked::debug {
inline constexpr int kDebugSpectateLevelId = 88350599;
inline constexpr int kDebugSpectateFormatVersion = 2;
inline constexpr char kDebugSpectateMapName[] = "VOICE";
inline constexpr char kDebugSpectateCreator[] = "LiteN1ng";
// Locale-independent UI parser. Empty means the normal 0% start; no exponent,
// sign, overflow or silently clamped request can select a different test.
inline std::optional<float> debugSpectateStartPercent(std::string_view text){
    if(text.empty())return 0.f;
    if(text.size()>6)return std::nullopt;
    unsigned value=0,decimals=0;bool dot=false,any=false;
    for(char c:text){if(c=='.'&&!dot){dot=true;continue;}if(c<'0'||c>'9')return std::nullopt;
        any=true;value=value*10+static_cast<unsigned>(c-'0');if(dot&&++decimals>2)return std::nullopt;}
    if(!any || (dot&&decimals==0))return std::nullopt;
    auto result=static_cast<float>(value)/(decimals==2?100.f:decimals==1?10.f:1.f);
    return result<=100.f?std::optional(result):std::nullopt;
}
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
        if (!terminal && !pose.discontinuity && !(pose.player2 && pose.player2->discontinuity) && !pose.cameraSnap && elapsedMs < m_nextSampleMs) return false;
        if (m_pending.size() >= 256 || m_nextSequence >= 12001 || elapsedMs > 600000) {
            m_ended = true; m_completed = false; return false;
        }
        auto const time = std::max<std::int64_t>(std::max<std::int64_t>(0, elapsedMs), m_lastElapsedMs + 1);
        auto stored=pose;
        if(stored.cosmetics){
            if(m_lastCosmetics && *m_lastCosmetics==*stored.cosmetics)stored.cosmetics.reset();
            else m_lastCosmetics=stored.cosmetics;
        }
        m_pending.push_back({m_nextSequence++, time, stored});
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
    std::shared_ptr<SpectatorCosmetics const> m_lastCosmetics;
    std::deque<DebugSpectateFrame> m_pending;
    std::int64_t m_nextSequence = 0, m_nextSampleMs = 0, m_lastElapsedMs = -1;
    bool m_ended = false, m_completed = false;
};
} // namespace corum::ranked::debug
