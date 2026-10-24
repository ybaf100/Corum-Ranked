#pragma once
#include <Geode/ui/ScrollLayer.hpp>
#include <functional>
namespace corum::ranked::ui {
// Reuse the leaderboard's original-finger scroll/tap owner for full-row cards.
geode::ScrollLayer* createRankedTapScroll(cocos2d::CCSize size,std::function<void(cocos2d::CCPoint,cocos2d::CCPoint)> tap);
}
