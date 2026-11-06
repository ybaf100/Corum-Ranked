#pragma once

#include "../domain/SeasonPresentation.hpp"
#include <Geode/Geode.hpp>
#include <string>

namespace corum::ranked::ui {

// Lobby season timer: a clock with one hand that snaps a step every second,
// the season label ("PRESEASON" / "SEASON n") and the countdown. A disabled
// mode shows "starts in" in light grey.
class SeasonTimer final : public cocos2d::CCNode {
    cocos2d::CCDrawNode* m_face = nullptr;
    cocos2d::CCNode* m_hand = nullptr;
    cocos2d::CCLabelBMFont* m_season = nullptr;
    cocos2d::CCLabelBMFont* m_remaining = nullptr;
    float m_clock = 11.0f;
    std::string m_seasonText, m_remainingText;
    SeasonTone m_tone = SeasonTone::Gold;
    bool init(bool compact);
    void drawFace(cocos2d::ccColor3B tone);
public:
    static SeasonTimer* create(bool compact);
    // Returns false when there is nothing to show (no time set).
    bool present(SeasonView const& season, bool modeEnabled, std::int64_t serverNowMs);
};

} // namespace corum::ranked::ui
