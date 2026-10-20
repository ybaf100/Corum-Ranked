#pragma once
#include <Geode/Geode.hpp>
namespace corum::ranked::ui {
// The source is a settled server result. Display is local and never awards AP.
void showAchievementUnlocks(cocos2d::CCNode* owner,std::string const& resultKey,matjson::Value const& notices);
}
