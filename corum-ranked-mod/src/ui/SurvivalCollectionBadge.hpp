#pragma once
#include "SurvivalRoundBadge.hpp"
#include <Geode/binding/GJDifficultySprite.hpp>

namespace corum::ranked::ui {
inline GJFeatureState survivalCollectionFeature(std::string_view grade){
    return grade=="FEATURED"?GJFeatureState::Featured:grade=="EPIC"?GJFeatureState::Epic:
        grade=="LEGENDARY"?GJFeatureState::Legendary:grade=="MYTHIC"?GJFeatureState::Mythic:GJFeatureState::None;
}
// Include native ornaments in the fitted bounds without changing their native
// offsets, anchors or canvas. Legendary/Mythic effects can exceed the face.
inline CCRect survivalCollectionVisualBounds(CCNode* node){
    auto const size=node->getContentSize();CCRect bounds{0,0,size.width,size.height};
    for(auto* child:node->getChildrenExt())if(child->isVisible()){
        auto const rect=CCRectApplyAffineTransform(survivalCollectionVisualBounds(child),child->nodeToParentTransform());
        auto const left=std::min(bounds.getMinX(),rect.getMinX()),bottom=std::min(bounds.getMinY(),rect.getMinY());
        auto const right=std::max(bounds.getMaxX(),rect.getMaxX()),top=std::max(bounds.getMaxY(),rect.getMaxY());
        bounds={left,bottom,right-left,top-bottom};
    }
    return bounds;
}
inline CCNode* survivalCollectionBadge(int nativeCode,std::string_view grade,float side){
    auto const eliminated=grade=="ELIMINATED";
    auto* card=surface({side,side},{},grade.empty()?Blue:survivalBadgeColor(grade),Surface,242,true);
    if(eliminated){
        auto* cross=menuLabel("X",.82f,{side/2,side/2},Red,FontWeight::Black);
        fitFullMenuLabel(cross,side-16,side-16,.82f);card->addChild(cross,5);return card;
    }
    auto* face=GJDifficultySprite::create(nativeCode,GJDifficultyName::Short);
    if(face){
        face->updateFeatureState(survivalCollectionFeature(grade));
        auto const bounds=survivalCollectionVisualBounds(face);auto const anchor=face->getAnchorPointInPoints();
        auto const scale=(side-16)/std::max({1.f,bounds.size.width,bounds.size.height});
        face->setScale(scale);
        face->setPosition({side/2-(bounds.getMidX()-anchor.x)*scale,side/2-(bounds.getMidY()-anchor.y)*scale});
        card->addChild(face,5);
    }
    return card;
}
}
