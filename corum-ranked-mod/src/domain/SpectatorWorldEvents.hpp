#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
namespace corum::ranked {
struct SpectatorRandomSpawn {
    int group = 0;
    bool ordered = false;
    double delay = 0;
    std::vector<int> remap;
    std::optional<int> triggerId={},controlId={};
};
struct SpectatorRandomEvent {
    std::uint64_t ordinal = 0;
    int objectIndex = -1, objectId = 0;
    float originX = 0, originY = 0;
    std::int64_t tick = 0;
    double elapsedMs = 0;
    std::vector<SpectatorRandomSpawn> outputs;
};
inline bool validSpectatorRandomEvent(SpectatorRandomEvent const &e) {
    if (!e.ordinal || e.ordinal > 1000000 || e.objectIndex < 0 || e.objectIndex > 1000000 ||
        e.objectId <= 0 || e.objectId > 1000000 || e.tick < 0 || e.tick > 1000000000 ||
        !std::isfinite(e.elapsedMs) || e.elapsedMs < 0 || e.elapsedMs > 86400000 ||
        !std::isfinite(e.originX) || !std::isfinite(e.originY) || std::abs(e.originX) > 10000000 ||
        std::abs(e.originY) > 10000000 || e.outputs.size() > 4)
        return false;
    for (auto const &s : e.outputs) {
        if (s.group < 0 || s.group > 1000000 || !std::isfinite(s.delay) || s.delay < 0 || s.delay > 600 ||
            s.remap.size() > 32)
            return false;
        for (auto n : s.remap)
            if (n < 0 || n > 1000000)
                return false;
    }
    return true;
}
using SpectatorRandomEvents = std::shared_ptr<std::vector<SpectatorRandomEvent> const>;
// Accepted packet events survive render interpolation skipping a sample. Bounded
// per Attempt; cached immutable snapshots allocate only at an event boundary.
class SpectatorRandomTimeline {
  public:
    bool push(SpectatorRandomEvents const &events, double sourceClock, double publisherClock) {
        if (!events)
            return true;
        for (auto e : *events) {
            if (!validSpectatorRandomEvent(e))
                return false;
            if (e.ordinal <= m_last)
                continue;
            if (e.ordinal != m_last + 1 || m_events.size() >= 256)
                return false;
            e.elapsedMs = std::max(0., publisherClock + e.elapsedMs - sourceClock);
            m_events.push_back(std::move(e));
            ++m_last;
        }
        return true;
    }
    SpectatorRandomEvents through(double elapsed) const {
        auto count = std::count_if(m_events.begin(), m_events.end(),
                                   [&](auto const &e) { return e.elapsedMs <= elapsed; });
        if (static_cast<std::size_t>(count) != m_cachedCount) {
            auto v = std::make_shared<std::vector<SpectatorRandomEvent>>();
            for (auto const &e : m_events)
                if (e.elapsedMs <= elapsed)
                    v->push_back(e);
            m_cached = std::move(v);
            m_cachedCount = count;
        }
        return m_cached;
    }
    void reset() {
        m_events.clear();
        m_last = 0;
        m_cached.reset();
        m_cachedCount = 0;
    }

  private:
    std::vector<SpectatorRandomEvent> m_events;
    std::uint64_t m_last = 0;
    mutable SpectatorRandomEvents m_cached;
    mutable std::size_t m_cachedCount = 0;
};
} // namespace corum::ranked
