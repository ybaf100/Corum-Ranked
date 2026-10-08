#pragma once

#include <array>
#include <string_view>

namespace corum::ranked {
struct MapDifficulty {
    std::string_view label = "N/A";
    std::string_view frame = "diffIcon_00_btn_001.png";
    bool operator==(MapDifficulty const&) const = default;
};

// Native GD metadata, never the separate Corum numeric difficulty/rating.
inline MapDifficulty mapDifficulty(bool autoLevel, bool demon, int demonDifficulty, int average) {
    if (autoLevel) return {"Auto", "diffIcon_auto_btn_001.png"};
    if (demon) {
        switch (demonDifficulty) {
            case 3: return {"Easy Demon", "diffIcon_07_btn_001.png"};
            case 4: return {"Medium Demon", "diffIcon_08_btn_001.png"};
            case 5: return {"Insane Demon", "diffIcon_09_btn_001.png"};
            case 6: return {"Extreme Demon", "diffIcon_10_btn_001.png"};
            default: return {"Hard Demon", "diffIcon_06_btn_001.png"};
        }
    }
    static constexpr std::array<MapDifficulty, 6> ordinary {{
        {}, {"Easy", "diffIcon_01_btn_001.png"}, {"Normal", "diffIcon_02_btn_001.png"},
        {"Hard", "diffIcon_03_btn_001.png"}, {"Harder", "diffIcon_04_btn_001.png"},
        {"Insane", "diffIcon_05_btn_001.png"},
    }};
    return average >= 1 && average <= 5 ? ordinary[average] : ordinary[0];
}
} // namespace corum::ranked
