#pragma once

#include <cstdint>
#include <optional>

namespace corum::ranked {

class RenderFpsMeter {
public:
    void observeFrame(std::int64_t steadyNowMicros);
    void reset();
    [[nodiscard]] std::optional<double> fps() const;

private:
    std::optional<std::int64_t> m_previousFrameMicros;
    double m_smoothedFrameMicros = 0.0;
    int m_validIntervals = 0;
};

} // namespace corum::ranked
