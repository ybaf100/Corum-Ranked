#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace corum::ranked {
struct DeathFeedEvent {
    std::string id, side;
    double progressPercent = 0;
    std::int64_t endedAtMs = 0;
};
struct DeathFeedRow {
    DeathFeedEvent event;
    double createdAtMs = 0;
};
class DeathFeed {
public:
    void reset(std::string context) {
        if (context == m_context) return;
        m_context = std::move(context); m_rows.clear(); m_seen.clear();
    }
    void observe(std::vector<DeathFeedEvent> const& events, double nowMs, std::int64_t serverNowMs) {
        for (auto const& event : events) {
            if (event.id.empty() || (event.side != "A" && event.side != "B") || !std::isfinite(event.progressPercent) ||
                std::find(m_seen.begin(), m_seen.end(), event.id) != m_seen.end()) continue;
            m_seen.push_back(event.id);
            if (m_seen.size() > 128) m_seen.pop_front();
            auto const age = std::max(0.0, static_cast<double>(serverNowMs - event.endedAtMs));
            if (age >= 5000) continue;
            m_rows.push_back({event, nowMs - age});
            // Oldest at the bottom, new entries grow upwards. No fourth row,
            // including during the outgoing row's animation.
            if (m_rows.size() > 3) m_rows.erase(m_rows.begin());
        }
        std::erase_if(m_rows, [nowMs](auto const& row) { return nowMs - row.createdAtMs >= 5250; });
    }
    auto const& rows() const { return m_rows; }
private:
    std::string m_context;
    std::vector<DeathFeedRow> m_rows;
    std::deque<std::string> m_seen;
};
}
