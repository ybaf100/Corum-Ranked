#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace corum::ranked {

inline constexpr std::uintmax_t kMaxRankAssetZipBytes = 64 * 1024 * 1024;
inline constexpr std::array<std::string_view, 61> kRankVisualFiles {
    "corum-fire-perfect.png",
    "corum-fire-promotion.png",
    "corum-trophy-aqua.png",
    "corum-trophy-bronze.png",
    "corum-trophy-gold.png",
    "corum-trophy-red.png",
    "corum-trophy-silver.png",
    "corum-trophy-unranked.png",
    "fx-gold-crown-aura.png",
    "fx-gold-halo-inner.png",
    "fx-gold-halo-outer.png",
    "fx-gold-radial-rays.png",
    "fx-rank-broken-ring-white.png",
    "fx-rank-crack-white.png",
    "fx-rank-energy-column-white.png",
    "fx-rank-glint-white.png",
    "fx-rank-light-sweep-white.png",
    "fx-rank-radial-glow-white.png",
    "fx-rank-shockwave-circle-white.png",
    "fx-rank-shockwave-diamond-white.png",
    "fx-silver-crystal-sheen.png",
    "fx-silver-halo.png",
    "fx-silver-orbit.png",
    "placement-grade-a.png",
    "placement-grade-b.png",
    "placement-grade-c.png",
    "placement-grade-d.png",
    "placement-grade-s.png",
    "rank-aqua-body.png",
    "rank-aqua-core.png",
    "rank-aqua-orbit-back.png",
    "rank-aqua-orbit-front.png",
    "rank-bronze-body.png",
    "rank-bronze-core.png",
    "rank-bronze-frame-bottom.png",
    "rank-bronze-frame-left.png",
    "rank-bronze-frame-right.png",
    "rank-bronze-frame-top.png",
    "rank-gold-body.png",
    "rank-gold-core.png",
    "rank-gold-crown.png",
    "rank-gold-lower-crest.png",
    "rank-gold-radiant-ornament.png",
    "rank-gold-side-left.png",
    "rank-gold-side-right.png",
    "rank-red-body.png",
    "rank-red-core.png",
    "rank-red-spikes.png",
    "rank-silver-body.png",
    "rank-silver-core.png",
    "rank-silver-crystal-top.png",
    "rank-silver-wing-left.png",
    "rank-silver-wing-right.png",
    "rank-transition-background.png",
    "rank-transition-geometry-overlay.png",
    "rank-unranked-body.png",
    "rank-unranked-core.png",
    "rank-unranked-fragment-bl.png",
    "rank-unranked-fragment-br.png",
    "rank-unranked-fragment-tl.png",
    "rank-unranked-fragment-tr.png",
};

inline bool rankAssetVersionValid(std::string_view value) {
    if (value.empty() || value.size() > 64 || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    });
}

inline bool rankAssetHashValid(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9');
    });
}

inline bool rankAssetEntryAllowed(std::string_view filename) {
    return filename == "asset-manifest.json" || filename == "GLOBED-LICENSE.txt" ||
        std::find(kRankVisualFiles.begin(), kRankVisualFiles.end(), filename) != kRankVisualFiles.end();
}

struct RankAssetMetadata {
    bool enabled = false;
    std::string assetPackVersion;
    std::string sha256;
    std::uintmax_t sizeBytes = 0;

    bool operator==(RankAssetMetadata const&) const = default;
    [[nodiscard]] bool configured() const {
        return enabled && rankAssetVersionValid(assetPackVersion) && rankAssetHashValid(sha256) &&
            sizeBytes > 0 && sizeBytes <= kMaxRankAssetZipBytes;
    }
    [[nodiscard]] std::string downloadUrl() const {
        if (!configured()) return {};
        return "https://github.com/ybaf100/corum-ranked-assets/releases/download/" +
            assetPackVersion + "/Corum-Ranked-Assets.zip";
    }
};

} // namespace corum::ranked
