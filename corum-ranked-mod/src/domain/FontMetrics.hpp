#pragma once

#include <algorithm>
#include <string_view>

namespace corum::ranked {

// UI scales were authored against 64px source fonts. Geode generates each
// source at 1/4, 1/2 and full size for SD, HD and UHD respectively.
inline constexpr unsigned CorumFontReferenceSize = 64;
inline constexpr unsigned CorumFontRasterSize = 128;
inline constexpr unsigned CorumCountdownRasterSize = 256;

// Menu copy needs a larger readable size than the compact gameplay HUD.
// Keep this policy at menu call sites; native HUD/countdown scales stay authored.
inline constexpr float rankedMenuTextScale(float logicalScale) {
    return logicalScale > 0 ? std::max(0.36f, logicalScale + 0.18f) : logicalScale;
}

inline constexpr float corumFontScale(float logicalScale, std::string_view resource) {
    // Keep vanilla fallback fonts at their own authored scale. Resolution
    // suffixes and the mod's resource prefix do not change this decision.
    if (resource.find("CorumSans") == std::string_view::npos) return logicalScale;
    auto const rasterSize = resource.find("CorumSansCountdown") != std::string_view::npos
        ? CorumCountdownRasterSize : CorumFontRasterSize;
    return logicalScale * static_cast<float>(CorumFontReferenceSize) / rasterSize;
}

} // namespace corum::ranked

