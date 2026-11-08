#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <string>

namespace corum::ranked::ui {

// Achievement screen visuals ("rarity card frame"): a tier-tinted card with a
// double frame, corner gems, a faint diagonal shine and a 1..5 gem row.
// Achieved tier 4 and 5 cards (growth IV/V, Legendary, Mythic) also get two
// light streaks travelling clockwise around the border; Mythic streaks use the
// moving achievement pastel rainbow. Polygons only, straight alpha.
class AchievementCardFrame final : public cocos2d::CCNode {
public:
    static AchievementCardFrame* create(cocos2d::CCSize size, int tier, bool achieved);
    // The rarity gem row: `filled` of five diamonds, right edge at `right`.
    void setGems(cocos2d::CCPoint right, int filled, float size);
    void update(float dt) override;
    cocos2d::ccColor3B tone() const { return m_tone; }
    bool shining() const { return m_shine != nullptr; }

private:
    bool init(cocos2d::CCSize size, int tier, bool achieved);
    void drawStatic();
    void drawShine();
    cocos2d::CCSize m_size;
    int m_tier = 0;
    bool m_achieved = false;
    cocos2d::ccColor3B m_tone{92, 100, 116};
    cocos2d::CCDrawNode* m_static = nullptr;
    cocos2d::CCDrawNode* m_shine = nullptr;
    cocos2d::CCPoint m_gemsRight{};
    int m_gemsFilled = 0;
    float m_gemSize = 0;
    double m_time = 0;
};

// Plate (AP / claimable): a double frame around one line of text.
cocos2d::CCNode* achievementPlate(std::string const& text, cocos2d::CCSize size, cocos2d::ccColor3B accent,
    cocos2d::ccColor3B fill, cocos2d::ccColor3B ink, float scale);
// A thin inner line `inset` inside a node of `size` (the double frame look).
void addAchievementInnerFrame(cocos2d::CCNode* node, cocos2d::CCSize size, cocos2d::ccColor3B color, float inset, GLubyte alpha);
// Scope tab with pointed ends.
cocos2d::CCNode* achievementTabFace(std::string const& text, cocos2d::CCSize size, bool selected);

enum class AchievementButtonStyle { Claim, Muted, Title, Current, Disabled, Nav };
cocos2d::CCNode* achievementButtonFace(std::string const& text, cocos2d::CCSize size, AchievementButtonStyle style, float scale);

} // namespace corum::ranked::ui
