#pragma once

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

namespace corum::ranked {

// UI scales were authored against 64px source fonts. Geode generates each
// source at 1/4, 1/2 and full size for SD, HD and UHD respectively.
inline constexpr unsigned CorumFontReferenceSize = 64;
inline constexpr unsigned CorumFontRasterSize = 128;
inline constexpr unsigned CorumCountdownRasterSize = 256;

// Each UI weight ships three atlases, with glyphs of 24, 48 and 128 px at UHD
// (CorumSansBold24.fnt, CorumSansBold48.fnt, CorumSansBold.fnt). A label takes
// the smallest atlas whose glyphs are at least about as large as they are
// drawn, so text is drawn at 1-2x reduction and stays crisp with plain
// bilinear sampling. One 128 px atlas drawn at ~20 px on a 1080p window was
// reduced over 6x: jagged without mipmaps, soft with them.
inline constexpr unsigned CorumFontSmallSize = 24;
inline constexpr unsigned CorumFontMediumSize = 48;

// The em of a label on screen, in pixels: the authored 64 px reference em is
// 16 points at logical scale 1 (at every texture quality).
inline constexpr float corumDrawnEmPixels(float logicalScale, float pixelsPerPoint) {
    return logicalScale * static_cast<float>(CorumFontReferenceSize) / 4.0f * pixelsPerPoint;
}

// The atlas (its UHD glyph size) for a label drawn at `logicalScale`.
// `contentScale` is the texture quality's content scale (4 UHD, 2 HD, 1 SD):
// an atlas's glyphs are size * contentScale / 4 pixels. Up to ~1.1x
// enlargement keeps the smaller, sharper atlas.
inline constexpr unsigned corumFontTier(float logicalScale, float pixelsPerPoint, float contentScale) {
    auto const drawn = corumDrawnEmPixels(logicalScale, pixelsPerPoint) * 0.9f;
    for (unsigned size : {CorumFontSmallSize, CorumFontMediumSize})
        if (static_cast<float>(size) * contentScale / 4.0f >= drawn) return size;
    return CorumFontRasterSize;
}

// The file name of a resource, without its folders or mod prefix.
inline constexpr std::string_view corumFontFileName(std::string_view resource) {
    auto const slash = resource.find_last_of("/\\");
    return slash == std::string_view::npos ? resource : resource.substr(slash + 1);
}

// "CorumSansBold24.fnt" -> {"CorumSansBold", 24}; "CorumSansBold-uhd.fnt" ->
// {"CorumSansBold", 128}. The stem is empty for anything but a tiered Corum
// UI font (the countdown digits have a single atlas).
struct CorumFontName {
    std::string_view stem;
    unsigned size = CorumFontRasterSize;
};
inline constexpr CorumFontName corumFontName(std::string_view resource) {
    auto name = corumFontFileName(resource);
    if (!name.starts_with("CorumSans") || name.starts_with("CorumSansCountdown")) return {};
    name = name.substr(0, name.find_first_of(".-"));
    for (unsigned size : {CorumFontSmallSize, CorumFontMediumSize}) {
        auto const suffix = size == CorumFontSmallSize ? std::string_view("24") : std::string_view("48");
        if (name.ends_with(suffix)) return {name.substr(0, name.size() - suffix.size()), size};
    }
    return {name, CorumFontRasterSize};
}

// The glyph size a Corum resource was generated at. Resolution suffixes and
// the mod's resource prefix do not change it.
inline constexpr unsigned corumRasterSize(std::string_view resource) {
    if (corumFontFileName(resource).starts_with("CorumSansCountdown")) return CorumCountdownRasterSize;
    return corumFontName(resource).size;
}

// The same weight's atlas of another size, under the same folder/prefix.
// Anything that is not a tiered Corum UI font is returned unchanged.
inline std::string corumTierResource(std::string_view resource, unsigned size) {
    auto const name = corumFontName(resource);
    if (name.stem.empty()) return std::string(resource);
    auto const folder = resource.substr(0, resource.size() - corumFontFileName(resource).size());
    std::string result(folder);
    result += name.stem;
    if (size == CorumFontSmallSize) result += "24";
    else if (size == CorumFontMediumSize) result += "48";
    result += ".fnt";
    return result;
}

// Menu copy needs a larger readable size than the compact gameplay HUD.
// Keep this policy at menu call sites; native HUD/countdown scales stay authored.
inline constexpr float rankedMenuTextScale(float logicalScale) {
    return logicalScale > 0 ? std::max(0.36f, logicalScale + 0.18f) : logicalScale;
}

inline constexpr float corumFontScale(float logicalScale, std::string_view resource) {
    // Keep vanilla fallback fonts at their own authored scale.
    if (resource.find("CorumSans") == std::string_view::npos) return logicalScale;
    return logicalScale * static_cast<float>(CorumFontReferenceSize) / static_cast<float>(corumRasterSize(resource));
}

} // namespace corum::ranked
