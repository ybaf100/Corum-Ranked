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
inline cocos2d::CCNode* rankedPopupContent(cocos2d::CCLayer* main, cocos2d::CCMenu* menu,
    RankedPopupLayout const& layout) {
    main->setLayout(nullptr); // Popup's CopySizeLayout must not resize our logical menu.
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
