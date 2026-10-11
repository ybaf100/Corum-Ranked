#pragma once
#include <algorithm>
#include <optional>

namespace corum::ranked {
enum class RankedMode { Survival = 0, SoloRanked = 1 };

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
    auto const scale = std::clamp(std::min({1.0f, (width - 48.0f) / 424.0f, (height - 78.0f) / 290.0f}), 0.1f, 1.0f);
    return {scale, width / 2 - 118 * scale, width / 2 + 118 * scale, height / 2 - 8};
}
} // namespace corum::ranked
