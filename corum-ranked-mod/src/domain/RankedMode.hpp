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
    void recoverSurvival() { if (!m_moving) m_selected = RankedMode::Survival; }
    [[nodiscard]] bool moving() const { return m_moving; }
    [[nodiscard]] std::optional<RankedMode> selected() const { return m_selected; }
private:
    std::optional<RankedMode> m_selected;
    bool m_moving = false;
};

struct ModeLobbyLayout { float scale, leftX, rightX, centerY; };
inline ModeLobbyLayout modeLobbyLayout(float width, float height) {
    // All three panels use the original 188x210 card. Whole-card selection
    // removes the external Enter/Browse rows and restores full size at 320px.
    auto const scale = std::clamp(std::min({1.0f, (width - 48.0f) / 424.0f, (height - 88.0f) / 232.0f}), 0.1f, 1.0f);
    auto const centerY = height / 2.0f - 9.0f;
    return {scale, width / 2 - 118 * scale, width / 2 + 118 * scale, centerY};
}

struct ModeLobbyFrame {
    float survivalX, soloX, menuX;
    int survivalOpacity, soloOpacity, menuOpacity, entryOpacity;
};
/** Fade out, move, fade in: what leaves fades out in place before anything
 * moves, the selected card then slides, and what arrives fades in at its
 * final place. Closing plays the same frames backwards. */
inline constexpr float ModeLobbyFadeOutEnd = 0.29f;  // Solo: 0.18 s of 0.62 s
inline constexpr float ModeLobbyFadeInStart = 0.71f; // Solo: the last 0.18 s
inline constexpr float ModeLobbySurvivalOutEnd = 0.35f, ModeLobbySurvivalInStart = 0.55f;
inline constexpr float ModeLobbySeconds = 0.62f;
inline ModeLobbyFrame modeLobbyFrame(ModeLobbyLayout const& layout, RankedMode mode, float progress) {
    auto const p = std::clamp(progress,0.0f,1.0f);
    auto const mix = [](float from,float to,float t) { return from + (to-from)*t; };
    auto const fade = [p](float from,float to) {
        auto const t = std::clamp((p-from)/(to-from),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    };
    auto const opacity = [](float v) { return static_cast<int>(std::lround(255.0f*v)); };
    if (mode == RankedMode::SoloRanked) {
        // Survival and the Solo entry controls fade out in place; then the
        // Solo card slides to the left; then the menu fades in on the right.
        auto const out = fade(0,ModeLobbyFadeOutEnd);
        auto const move = fade(ModeLobbyFadeOutEnd,ModeLobbyFadeInStart);
        return {layout.leftX,mix(layout.rightX,layout.leftX,move),layout.rightX,
            opacity(1-out),255,opacity(fade(ModeLobbyFadeInStart,1)),opacity(1-out)};
    }
    // Survival stays: Solo and the entry controls fade out in place, then the
    // menu fades in where Solo was.
    auto const out = fade(0,ModeLobbySurvivalOutEnd);
    return {layout.leftX,layout.rightX,layout.rightX,255,opacity(1-out),opacity(fade(ModeLobbySurvivalInStart,1)),opacity(1-out)};
}
} // namespace corum::ranked
