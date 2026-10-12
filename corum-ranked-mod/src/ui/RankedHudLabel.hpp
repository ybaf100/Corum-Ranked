#pragma once
#include "RankedVisuals.hpp"

namespace corum::ranked::ui {
// Shared by the Solo PlayLayer and Survival HUD, including its original
// bitmap-font scale, weight and black outline/shadow.
struct RankedHudLabel {
    cocos2d::CCLabelBMFont* foreground=nullptr;
    cocos2d::CCLabelBMFont* shadow=nullptr;
    void text(std::string const& value) const {foreground->setString(value.c_str());shadow->setString(value.c_str());}
    void color(cocos2d::ccColor3B value) const {foreground->setColor(value);}
    void fit(float width,float maximum,float minimum) const {fitLabelWidth(foreground,width,maximum,minimum);shadow->setScale(foreground->getScale());}
    void opacity(GLubyte value) const {foreground->setOpacity(value);shadow->setOpacity(static_cast<GLubyte>(205*value/255));}
};
inline RankedHudLabel rankedHudLabel(cocos2d::CCNode* parent,std::string const& text,FontWeight weight,
    float scale,cocos2d::CCPoint anchor,cocos2d::CCPoint position,cocos2d::ccColor3B color,int z=3) {
    auto* shadow=label(text,scale,{position.x+.75f,position.y-.75f},{0,0,0},weight);
    shadow->setAnchorPoint(anchor);shadow->setOpacity(205);parent->addChild(shadow,z-1);
    auto* foreground=label(text,scale,position,color,weight);foreground->setAnchorPoint(anchor);parent->addChild(foreground,z);
    return {foreground,shadow};
}
} // namespace corum::ranked::ui
