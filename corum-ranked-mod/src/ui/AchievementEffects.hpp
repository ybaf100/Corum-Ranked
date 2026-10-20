#pragma once
#include <Geode/Geode.hpp>
#include "../domain/AchievementStyle.hpp"
namespace corum::ranked::ui {
cocos2d::ccColor3B achievementColor(int tier);
// Owned target action + per-target shader; removal/cleanup stops all work.
void styleAchievementText(cocos2d::CCLabelBMFont*,int tier);
void styleAchievementTree(cocos2d::CCNode*,int tier);
void styleAchievementFill(cocos2d::CCLayerColor*,int tier);
}
