#pragma once
#include "RankedVisuals.hpp"
#include "../domain/SurvivalBadgePresentation.hpp"
#include "../domain/SurvivalHudPresentation.hpp"
#include <Geode/binding/GJDifficultySprite.hpp>

namespace corum::ranked::ui {
inline ccColor3B survivalBadgeColor(std::string_view grade){
    auto const rank=grade=="MYTHIC"?1:grade=="LEGENDARY"?2:grade=="EPIC"?3:grade=="FEATURED"?4:6;
    auto const color=survivalRankColor(rank,grade!="ELIMINATED");return {color.r,color.g,color.b};
}
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
// Measure every native difficulty/effect once in units of the unfeatured
// frame's height. A shared envelope reserves room for ornaments without
// shrinking each face differently. Native face/ornament offsets are retained.
struct SurvivalBadgeFaceEnvelope {float halfWidth=0,halfHeight=0;};
inline SurvivalBadgeFaceEnvelope survivalBadgeFaceEnvelope(){
    static auto const envelope=[] {
        SurvivalBadgeFaceEnvelope result;
        for(int code=-1;code<=10;++code)for(auto grade:{"RATED","FEATURED","EPIC","LEGENDARY","MYTHIC"}){
            auto* probe=GJDifficultySprite::create(code,GJDifficultyName::Short);if(!probe)continue;
            auto const base=probe->getContentSize();if(base.height<=0)continue;
            probe->updateFeatureState(survivalCollectionFeature(grade));
            auto const bounds=survivalCollectionVisualBounds(probe);
            result.halfWidth=std::max(result.halfWidth,std::max(std::abs(bounds.getMinX()-base.width/2),std::abs(bounds.getMaxX()-base.width/2))/base.height);
            result.halfHeight=std::max(result.halfHeight,std::max(std::abs(bounds.getMinY()-base.height/2),std::abs(bounds.getMaxY()-base.height/2))/base.height);
        }
        return result;
    }();
    return envelope;
}
inline CCNode* survivalCollectionBadge(int nativeCode,std::string_view grade,float side,float faceArea=0){
    auto const eliminated=grade=="ELIMINATED";
    auto* card=surface({side,side},{},grade.empty()?Blue:survivalBadgeColor(grade),Surface,242,true);
    auto const area=faceArea>0?std::min(side,faceArea):side;
    if(eliminated){
        auto* cross=menuLabel("X",.82f,{side/2,side/2},Red,FontWeight::Black);
        fitFullMenuLabel(cross,area-16,area-16,.82f);card->addChild(cross,5);return card;
    }
    auto* face=GJDifficultySprite::create(nativeCode,GJDifficultyName::Short);
    if(face){
        auto const base=face->getContentSize();
        face->updateFeatureState(survivalCollectionFeature(grade));
        auto const envelope=survivalBadgeFaceEnvelope();auto const anchor=face->getAnchorPointInPoints();
        auto const height=std::min({area*.46f,(area-12)/(2*std::max(.5f,envelope.halfWidth)),(area-12)/(2*std::max(.5f,envelope.halfHeight))});
        auto const scale=std::max(0.f,height)/std::max(1.f,base.height);
        face->setScale(scale);
        face->setPosition({side/2-(base.width/2-anchor.x)*scale,side/2-(base.height/2-anchor.y)*scale});
        card->addChild(face,5);
    }
    return card;
}
}
