#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>

namespace corum::ranked::ui {
struct RankedPopupLayout {
    cocos2d::CCSize canvas, size;
    float scale;
};
inline RankedPopupLayout rankedPopupLayout(cocos2d::CCSize canvas, cocos2d::CCSize viewport) {
    auto const scale=std::max(.01f,std::min({1.f,(viewport.width-24)/canvas.width,(viewport.height-28)/canvas.height}));
    return {canvas,{canvas.width*scale,canvas.height*scale},scale};
}

// FLAlertLayer animates its main layer back to scale 1 when shown. Fit the
// logical canvas and modal menu INSIDE that layer, so native appearance cannot
// enlarge the panel past the screen or separate its visuals from touch bounds.
// Items already in a popup's button menu keep their screen position when the
// menu is rescaled to `scale` around its origin.
template <class Node = cocos2d::CCNode, class Array>
inline void keepPopupItemsOnScreen(Array* children, float scale) {
    if (!children) return;
    for (unsigned index = 0; index < children->count(); ++index) {
        auto* child = static_cast<Node*>(children->objectAtIndex(index));
        auto const at = child->getPosition();
        child->setPosition({at.x / scale, at.y / scale});
    }
}

template <class Menu>
inline cocos2d::CCNode* rankedPopupContent(cocos2d::CCLayer* main, Menu* menu,
    RankedPopupLayout const& layout) {
    main->setLayout(nullptr); // Popup's CopySizeLayout must not resize our logical menu.
    // Popup::init placed its own items (the close button) in a layout.size
    // menu at scale 1. The menu becomes the canvas at layout.scale below, so
    // keep those items where Popup put them on screen: the close button stays
    // on the panel's top-left corner however far a narrow (iPad) screen
    // shrinks the canvas.
    if constexpr (requires { menu->getChildren()->count(); menu->getChildren()->objectAtIndex(0u); })
        keepPopupItemsOnScreen(menu->getChildren(), layout.scale);
    auto* content=cocos2d::CCNode::create();
    for(cocos2d::CCNode* node:{content,static_cast<cocos2d::CCNode*>(menu)}) {
        node->ignoreAnchorPointForPosition(false);
        node->setAnchorPoint({.5f,.5f});node->setContentSize(layout.canvas);
        node->setPosition({layout.size.width/2,layout.size.height/2});node->setScale(layout.scale);
    }
    main->addChild(content,1);
    return content;
}
}
