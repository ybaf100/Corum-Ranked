#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace corum::ranked::ui {

// Achievement grade up (claimed AP crossed a grade threshold): the previous
// grade's core charges, cracks and breaks, and the new grade's core forms in
// its place. Cores only: the achievement grade is not a rank tier. An empty
// old grade (no grade yet) starts from the unranked core.
class AchievementGradeUpLayer final : public cocos2d::CCLayerColor {
public:
    static AchievementGradeUpLayer* create(std::string oldGrade, std::string newGrade, std::function<void()> completion);
    void keyBackClicked() override;

private:
    std::string m_old, m_new;
    std::function<void()> m_completion;
    cocos2d::CCNode* m_root = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    CCMenuItemSpriteExtra* m_skip = nullptr;
    CCMenuItemSpriteExtra* m_continue = nullptr;
    bool m_skipAllowed = false, m_final = false, m_finished = false;

    bool init(std::string oldGrade, std::string newGrade, std::function<void()> completion);
    cocos2d::CCPoint center() const;
    cocos2d::CCSprite* core(std::string const& grade, float extent) const;
    void play();
    void reveal(bool skipped);
    void burst(cocos2d::ccColor3B color, int count, float distance, float delay, bool inward);
    void flash(cocos2d::ccColor3B color, GLubyte opacity, float delay);
    void onSkip(cocos2d::CCObject*);
    void onContinue(cocos2d::CCObject*);
    void finish();
};

} // namespace corum::ranked::ui
