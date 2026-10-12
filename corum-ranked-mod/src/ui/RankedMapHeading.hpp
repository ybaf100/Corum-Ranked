#pragma once
#include "MapDifficultyBadge.hpp"
#include <cctype>

namespace corum::ranked::ui {
// The same title, GD difficulty badge and duration row used by Solo Placement.
inline void addRankedMapHeading(cocos2d::CCNode* parent,int levelId,std::string title,MapMetadata metadata,
    cocos2d::CCPoint titlePosition,cocos2d::ccColor3B durationColor=Cyan) {
    std::transform(title.begin(),title.end(),title.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
    auto* name=menuLabel(title,.42f,titlePosition,Text,FontWeight::Black);
    fitMenuLabelWidth(name,278,.42f,.25f);parent->addChild(name,5);
    auto* badge=mapDifficultyBadge(levelId,metadata,135);
    badge->setAnchorPoint({.5f,.5f});badge->setPosition({titlePosition.x-38,titlePosition.y-23});parent->addChild(badge,5);
    auto* length=menuLabel(mapLengthText(displayedMapMetadata(levelId,metadata).durationSeconds),.29f,
        {titlePosition.x+73,titlePosition.y-23},durationColor,FontWeight::SemiBold);
    parent->addChild(length,5);
}
} // namespace corum::ranked::ui
