#pragma once
#include <algorithm>
#include <cmath>

namespace corum::ranked {

// Free play while the mod update or the Ranked resources download: a small
// card at the top left of every scene shows how far the download is.
struct DownloadOverlayInputs {
    bool modUpdating = false;   // update package downloading or verifying
    float modProgress = 0;      // 0..1
    bool assetsDownloading = false;
    float assetProgress = 0;    // 0..1
    bool audioDownloading = false;
    int audioReady = 0, audioTotal = 0, audioActivePercent = 0;
};
struct DownloadOverlayState {
    bool visible = false;
    bool modUpdate = false;  // otherwise Ranked resources
    int percent = 0;         // 0..100
};

inline DownloadOverlayState downloadOverlayState(DownloadOverlayInputs const& in) {
    auto const unit = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; };
    if (in.modUpdating) return {true, true, static_cast<int>(std::floor(unit(in.modProgress) * 100.0f))};
    float sum = 0; int parts = 0;
    if (in.assetsDownloading) { sum += unit(in.assetProgress); ++parts; }
    if (in.audioDownloading && in.audioTotal > 0) {
        auto const active = std::clamp(in.audioActivePercent, 0, 100) / 100.0f;
        sum += unit((static_cast<float>(std::clamp(in.audioReady, 0, in.audioTotal)) + active) / static_cast<float>(in.audioTotal));
        ++parts;
    }
    if (parts == 0) return {};
    return {true, false, std::min(99, static_cast<int>(std::floor(sum / static_cast<float>(parts) * 100.0f)))};
}

} // namespace corum::ranked
