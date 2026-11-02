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

// Native cascade stops at plain CCNodes (rows, icon wrappers). Walk the group
// opacity through them, and enable cascade on every RGBA node so later
// per-label fades (death feed, standings) keep multiplying by the group.
inline void propagateHudOpacity(cocos2d::CCNode* node, GLubyte inherited) {
    if (!node) return;
    if (auto* rgba = dynamic_cast<cocos2d::CCRGBAProtocol*>(node)) {
        rgba->setCascadeOpacityEnabled(true);
        rgba->updateDisplayedOpacity(inherited);
        inherited = rgba->getDisplayedOpacity();
    }
    for (auto* child : node->getChildrenExt()) propagateHudOpacity(child, inherited);
}

inline GLubyte hudGroupOpacity(cocos2d::CCNode* group) {
    auto* rgba = dynamic_cast<cocos2d::CCRGBAProtocol*>(group);
    return rgba ? rgba->getOpacity() : 255;
}

// Re-applies the group opacity to children added after the HUD was built.
// A fully opaque group needs no walk: its children already display as-is.
inline void refreshHudOpacity(cocos2d::CCNode* group) {
    if (!group) return;
    auto const opacity = hudGroupOpacity(group);
    if (opacity == 255) return;
    for (auto* child : group->getChildrenExt()) propagateHudOpacity(child, opacity);
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
    if (auto* rgba = dynamic_cast<cocos2d::CCRGBAProtocol*>(group)) {
        auto const opacity = hudOpacityByte(transform.opacity);
        auto const changed = rgba->getOpacity() != opacity;
        rgba->setOpacity(opacity);
        if (changed || opacity != 255)
            for (auto* child : group->getChildrenExt()) propagateHudOpacity(child, opacity);
    }
}

inline cocos2d::CCNodeRGBA* createHudPlacementGroup(cocos2d::CCNode* parent, HudMode mode, std::string_view id,
    HudLayoutConfig const& layout, int z) {
    auto const size = cocos2d::CCDirector::sharedDirector()->getWinSize();
    auto* group = cocos2d::CCNodeRGBA::create();
    group->setCascadeOpacityEnabled(true);
    group->setCascadeColorEnabled(false);
    group->setContentSize(size);
    group->setID(std::string("hud-group"_spr) + (mode == HudMode::Solo ? "-solo-" : "-survival-") + std::string(id));
    applyHudPlacement(group, resolveHudTransform(mode, id, layout, size.width, size.height));
    if (parent) parent->addChild(group, z);
    return group;
}

} // namespace corum::ranked::ui
