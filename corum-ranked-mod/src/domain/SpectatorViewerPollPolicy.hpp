#pragma once
#include <algorithm>
#include <cmath>

namespace corum::ranked {
// Serialized HTTP only. Keep an entire RTT of refill headroom before choosing
// the ordinary 40ms gap; low buffer gets 20ms and starvation gets 5ms. Errors
// and idle/terminal streams cannot spin at the starvation rate.
inline int spectatorViewerPollGapMs(bool live, bool fallback, bool starving, bool terminal,
                                    double headroomMs, double delayMs, double rttMs, int errors) {
    if (errors > 0) return std::min(1000, 150 * (1 << std::clamp(errors - 1, 0, 3)));
    if (fallback) return 150;
    if (!live || terminal) return 100;
    if (!std::isfinite(headroomMs) || starving || headroomMs <= std::max(50.0, rttMs + 20)) return 5;
    return headroomMs < delayMs ? 20 : 40;
}
} // namespace corum::ranked
