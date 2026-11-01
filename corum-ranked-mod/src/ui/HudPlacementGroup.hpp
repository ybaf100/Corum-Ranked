#pragma once

#include "../domain/HudLayout.hpp"
#include <Geode/Geode.hpp>
#include <string>
#include <string_view>

namespace corum::ranked::ui {

using namespace geode::prelude;

// The saved layout is read once per HUD build (match/round start), never
// per frame. The editor writes it and the next HUD build picks it up.
inline HudLayoutConfig loadHudLayout() {
    return parseHudLayoutText(geode::Mod::get()->getSavedValue<std::string>(std::string(HudLayoutSavedKey), ""));
}

inline void saveHudLayout(HudLayoutConfig const& config) {
    geode::Mod::get()->setSavedValue<std::string>(std::string(HudLayoutSavedKey), hudLayoutJson(config).dump(matjson::NO_INDENTATION));
    (void)geode::Mod::get()->saveData();
}

// A full-window group whose children keep their authored absolute
// coordinates. Its anchor is the element pivot, so position/scale move the
// element around that pivot without touching the children's own layout.
inline void applyHudPlacement(cocos2d::CCNode* group, HudTransform const& transform) {
    if (!group) return;
    auto const size = group->getContentSize();
    if (size.width <= 0.0f || size.height <= 0.0f) return;
    group->setAnchorPoint({transform.pivot.x/size.width,transform.pivot.y/size.height});
    group->setPosition({transform.position.x, transform.position.y});
    group->setScale(transform.scale);
    group->setVisible(transform.visible);
}

inline cocos2d::CCNode* createHudPlacementGroup(cocos2d::CCNode* parent, HudMode mode, std::string_view id,
    HudLayoutConfig const& layout, int z) {
    auto const size = cocos2d::CCDirector::sharedDirector()->getWinSize();
    auto* group = cocos2d::CCNode::create();
    group->setContentSize(size);
    group->setID(std::string("hud-group"_spr) + (mode == HudMode::Solo ? "-solo-" : "-survival-") + std::string(id));
    applyHudPlacement(group, resolveHudTransform(mode, id, layout, size.width, size.height));
    if (parent) parent->addChild(group, z);
    return group;
}

} // namespace corum::ranked::ui
