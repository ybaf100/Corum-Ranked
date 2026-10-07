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
} // namespace corum::ranked
