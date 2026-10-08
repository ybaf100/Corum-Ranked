#pragma once
#include <algorithm>
#include <string>
namespace corum::ranked {
// A scene-independent monotonic budget. Re-entering Pause/Practice or advancing
// to another round cannot refill it. Server observations may only reduce it.
class MatchPauseBudget {
public:
    void bind(std::string const& matchId) {
        if (matchId == m_matchId) return;
        m_matchId = matchId; m_remaining = 30000; m_active = false; m_started = 0;
    }
    double remaining(double nowMs) const {
        return std::max(0.0, m_remaining - (m_active ? std::max(0.0, nowMs - m_started) : 0));
    }
    void observe(double availableMs, double nowMs) {
        m_remaining = std::min(remaining(nowMs), std::clamp(availableMs, 0.0, 30000.0));
        m_started = nowMs;
    }
    void begin(double nowMs) { if (!m_active) { m_started = nowMs; m_active = true; } }
    void end(double nowMs) { m_remaining = remaining(nowMs); m_active = false; }
private:
    std::string m_matchId;
    double m_remaining = 30000, m_started = 0;
    bool m_active = false;
};
}
