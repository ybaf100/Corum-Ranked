#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace corum::ranked {
struct EntryCountdownFrame {
    int digit = 0;
    float scale = 1;
    unsigned char opacity = 0;
};
// Presentation follows the shared server clock, rather than a per-device
// three-second action sequence. A delayed frame skips to the correct digit.
inline EntryCountdownFrame entryCountdownFrame(std::optional<std::int64_t> remainingMs) {
    if (!remainingMs || *remainingMs <= 0) return {};
    auto const remaining = std::clamp<std::int64_t>(*remainingMs, 1, 3000);
    auto const digit = static_cast<int>((remaining + 999) / 1000);
    auto const phase = 1.0f - static_cast<float>(remaining - (digit - 1) * 1000) / 1000.0f;
    auto const appear = std::clamp(phase / 0.18f, 0.0f, 1.0f);
    auto const ease = 1.0f - std::pow(1.0f - appear, 3.0f);
    auto const alpha = std::min(std::clamp(phase / 0.12f, 0.0f, 1.0f),
        std::clamp((1.0f - phase) / 0.20f, 0.0f, 1.0f));
    return {digit, 1.32f - 0.32f * ease,
        static_cast<unsigned char>(std::lround(255.0f * alpha))};
}

// HTTP clock observations can move remaining time backwards around a second
// boundary. A scene's presentation progresses once through 3, 2, 1; gameplay
// release still uses the original server certificate and unmodified clock.
class EntryCountdownPresentation {
public:
    EntryCountdownFrame observe(std::optional<std::int64_t> remainingMs) {
        if (remainingMs) {
            auto const value = std::clamp<std::int64_t>(*remainingMs, 0, 3000);
            m_remaining = m_remaining ? std::min(*m_remaining, value) : value;
        }
        return entryCountdownFrame(m_remaining);
    }
    bool started() const { return m_remaining.has_value(); }
private:
    std::optional<std::int64_t> m_remaining;
};
} // namespace corum::ranked

