#pragma once
#include "RankedVisuals.hpp"
namespace corum::ranked::ui {
inline void fitSurvivalSingleLine(CCLabelBMFont* label,float width,float height,float maximum){
    if(!label || width<=0 || height<=0)return;
    label->setWidth(-1);setLabelScale(label,rankedMenuTextScale(maximum));
    auto const size=label->getContentSize();
    if(size.width>0 && size.height>0)label->setScale(std::min({label->getScale(),width/size.width,height/size.height}));
}
// One full-row menu item owns the caption and pill, so its visible bounds and
// touch bounds agree even on a scaled popup. No nested touch-swallowing menu.
inline CCNode* survivalOptionFace(CCSize size,std::string const& caption,bool on) {
    auto* face=surface(size,{},on?Green:TextMuted,SurfaceRaised,250,false);
    float const pillWidth=36,padding=6;
    auto* name=menuLabel(caption,.23f,{(size.width-pillWidth-padding*3)/2+padding,size.height/2},Text,FontWeight::SemiBold);
    fitFullMenuLabel(name,size.width-pillWidth-padding*3,size.height-8,.23f);face->addChild(name,5);
    auto* pill=surface({pillWidth,20},{size.width-padding-pillWidth/2,size.height/2},on?Green:TextMuted,on?ccc3(27,112,68):ccc3(44,56,75),255,false);
    auto* value=menuLabel(on?"ON":"OFF",.20f,{pillWidth/2,10},Text,FontWeight::ExtraBold);
    fitSurvivalSingleLine(value,pillWidth-6,14,.20f);pill->addChild(value,5);face->addChild(pill,5);return face;
}
}
