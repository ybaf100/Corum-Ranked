#pragma once
#include <Geode/Geode.hpp>
#include "../domain/AchievementStyle.hpp"
namespace corum::ranked::ui {
cocos2d::ccColor3B achievementColor(int tier);
// Owned target action + per-target shader; removal/cleanup stops all work.
void styleAchievementText(cocos2d::CCLabelBMFont*,int tier,bool animated=true);
void styleAchievementTree(cocos2d::CCNode*,int tier);
void styleAchievementFill(cocos2d::CCLayerColor*,int tier,bool rainbowProgress=false,bool achieved=false);
// The achievement bar shine on any node (a draw node too): tier 4 the
// Legendary gold sweep, tier 5 the Mythic rainbow, anything else restores
// `plainShader`. Draw the node's colour white under the shine.
void styleAchievementShine(cocos2d::CCNode*,int tier,char const* plainShader);
}
