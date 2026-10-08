#pragma once
#include "../RankedMapDetails.hpp"
#include "RankedVisuals.hpp"

namespace corum::ranked::ui {
// Native GD faces share one baseline with their textual difficulty.
inline cocos2d::CCNode* mapDifficultyBadge(int levelId, float width, float height = 21) {
    using namespace geode::prelude;
    auto* row = CCNode::create(); row->setContentSize({width, height});
    auto const value = RankedMapDetails::get().difficulty(levelId);
    auto* icon = CCSprite::createWithSpriteFrameName(value.frame.data());
    float const iconWidth = height - 2;
    if (icon) {
        auto const size = icon->getContentSize();
        if (size.width > 0 && size.height > 0) icon->setScale(std::min(iconWidth / size.width, iconWidth / size.height));
        icon->setPosition({iconWidth / 2 + 1, height / 2}); row->addChild(icon);
    }
    auto* text = menuLabel(std::string(value.label), 0.23f, {(width + iconWidth + 4) / 2, height / 2},
        Text, FontWeight::SemiBold);
    fitMenuLabelWidth(text, std::max(10.0f, width - iconWidth - 5), 0.23f, 0.12f);
    row->addChild(text);
    row->setID("ranked-map-difficulty"_spr);
    return row;
}
} // namespace corum::ranked::ui
