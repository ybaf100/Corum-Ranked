#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace corum::ranked {
enum class RankedMode { Survival = 0, SoloRanked = 1 };
enum class HistoryScope { All = 0, Survival = 1, SoloRanked = 2 };
inline char const* historyScopeKey(HistoryScope scope) {
    return scope == HistoryScope::Survival ? "SURVIVAL" : scope == HistoryScope::SoloRanked ? "SOLO_RANKED" : "ALL";
}
inline HistoryScope historyScopeForLobby(std::optional<RankedMode> selected) {
    return !selected ? HistoryScope::All : *selected == RankedMode::Survival ? HistoryScope::Survival : HistoryScope::SoloRanked;
}

/** The animation owns its transition until completion, including Back input. */
class ModeLobbyTransition final {
public:
    bool select(RankedMode mode) {
        if (m_moving || m_selected) return false;
        m_selected = mode; m_moving = true; return true;
    }
    bool back() {
        if (m_moving || !m_selected) return false;
        m_selected.reset(); m_moving = true; return true;
    }
    void finish() { m_moving = false; }
    void recoverSolo() { if (!m_moving) m_selected = RankedMode::SoloRanked; }
    [[nodiscard]] bool moving() const { return m_moving; }
    [[nodiscard]] std::optional<RankedMode> selected() const { return m_selected; }
private:
    std::optional<RankedMode> m_selected;
    bool m_moving = false;
};

struct ModeLobbyLayout { float scale, leftX, rightX, centerY; };
inline ModeLobbyLayout modeLobbyLayout(float width, float height) {
    // Preserve the original 188x210 profile / 188x196 menu proportions. The
    // external mode caption and entry controls also fit above the footer.
    auto const scale = std::clamp(std::min({1.0f, (width - 48.0f) / 424.0f, (height - 116.0f) / 296.0f}), 0.1f, 1.0f);
    auto const centerY = (height - 70.0f + 46.0f) / 2.0f + 18.0f * scale;
    return {scale, width / 2 - 118 * scale, width / 2 + 118 * scale, centerY};
}

struct ModeLobbyFrame {
    float survivalX, soloX, menuX;
    int survivalOpacity, soloOpacity, menuOpacity, entryOpacity;
};
inline ModeLobbyFrame modeLobbyFrame(ModeLobbyLayout const& layout, RankedMode mode, float progress) {
    auto const p = std::clamp(progress,0.0f,1.0f);
    auto const mix = [](float from,float to,float t) { return from + (to-from)*t; };
    auto const fade = [p](float from,float to) {
        auto const t = std::clamp((p-from)/(to-from),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    };
    auto const opacity = [](float v) { return static_cast<int>(std::lround(255.0f*v)); };
    auto const menuX = mix(layout.leftX,layout.rightX,p);
    if (mode == RankedMode::SoloRanked) {
        // Both cards cross at the center. The outgoing Survival card and the
        // incoming menu follow the same path and exchange content at crossing.
        auto const swap = fade(0.42f,0.58f);
        return {menuX,mix(layout.rightX,layout.leftX,p),menuX,
            opacity(1-swap),255,opacity(swap),opacity(1-fade(0,0.25f))};
    }
    // The menu emerges from behind the stationary Survival card and covers
    // Solo on the right. Reverse uses exactly the same positions and opacity.
    return {layout.leftX,layout.rightX,menuX,255,opacity(1-fade(0.4f,0.95f)),255,
        opacity(1-fade(0,0.25f))};
}
} // namespace corum::ranked
