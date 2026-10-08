#include "RenderFpsMeter.hpp"

namespace corum::ranked {

void RenderFpsMeter::observeFrame(std::int64_t steadyNowMicros) {
    if (!m_previousFrameMicros) {
        m_previousFrameMicros = steadyNowMicros;
        return;
    }
    auto const elapsed = steadyNowMicros - *m_previousFrameMicros;
    m_previousFrameMicros = steadyNowMicros;
    if (elapsed <= 0 || elapsed > 1'000'000) {
        m_smoothedFrameMicros = 0.0;
        m_validIntervals = 0;
        return;
    }
    if (m_validIntervals == 0) {
        m_smoothedFrameMicros = static_cast<double>(elapsed);
    } else {
        constexpr double alpha = 0.15;
        m_smoothedFrameMicros = alpha * static_cast<double>(elapsed) +
            (1.0 - alpha) * m_smoothedFrameMicros;
    }
    ++m_validIntervals;
}

void RenderFpsMeter::reset() {
    m_previousFrameMicros.reset();
    m_smoothedFrameMicros = 0.0;
    m_validIntervals = 0;
}

std::optional<double> RenderFpsMeter::fps() const {
    if (m_validIntervals < 5 || m_smoothedFrameMicros <= 0.0) return std::nullopt;
    return 1'000'000.0 / m_smoothedFrameMicros;
}

} // namespace corum::ranked
