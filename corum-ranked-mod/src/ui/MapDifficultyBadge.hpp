#pragma once
#include "../RankedMapDetails.hpp"
#include "RankedVisuals.hpp"
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <array>

namespace corum::ranked::ui {
inline MapMetadata displayedMapMetadata(int levelId, MapMetadata info) {
    auto const native = RankedMapDetails::get().metadata(levelId);
    if (info.kind == "UNKNOWN") {
        auto const duration = info.durationSeconds;
        info = native;
        if (duration) info.durationSeconds = duration;
    }
    if (!info.durationSeconds) info.durationSeconds = native.durationSeconds;
    return info;
}
inline cocos2d::ccColor3B mapTierColor(std::string const& text) {
    if (text.size() != 7 || text[0] != '#') return TextMuted;
    auto const hex = geode::utils::numFromString<unsigned int>(text.substr(1),16);
    if (!hex.isOk()) return TextMuted;
    auto const color = hex.unwrap();
    return {static_cast<GLubyte>((color>>16)&255),static_cast<GLubyte>((color>>8)&255),static_cast<GLubyte>(color&255)};
}
// GD face, numeric circle/square and label share one baseline. No GDDL mod dependency.
inline cocos2d::CCNode* mapDifficultyBadge(int levelId, MapMetadata metadata, float width, float height = 21) {
    using namespace geode::prelude;
    auto const info = displayedMapMetadata(levelId,std::move(metadata));
    auto* row = CCNode::create(); row->setContentSize({width,height});
    auto const demon = info.kind == "DEMON";
    auto const extent = height - 2;
    auto const fill = demon ? mapTierColor(info.gddlColor) : ccc3(36,56,78);
    auto* shape = CCDrawNode::create(); row->addChild(shape);
    std::array<CCPoint,48> polygon;
    if (demon) {
        polygon[0]=CCPoint{1,1};
        polygon[1]=CCPoint{extent+1,1};
        polygon[2]=CCPoint{extent+1,extent+1};
        polygon[3]=CCPoint{1,extent+1};
    } else for (std::size_t i=0;i<polygon.size();++i) {
        auto const angle = static_cast<float>(i)*6.28318530718f/polygon.size();
        polygon[i]=CCPoint{1+extent/2+extent/2*std::cos(angle),1+extent/2+extent/2*std::sin(angle)};
    }
    auto const rgba = [](ccColor3B c) { return ccc4f(c.r/255.f,c.g/255.f,c.b/255.f,1); };
    shape->drawPolygon(polygon.data(),demon ? 4 : polygon.size(),rgba(fill),0.7f,rgba(TextMuted));
    auto const number = demon ? info.gddlTier : info.stars;
    auto* numeric = CCLabelBMFont::create(number ? std::to_string(*number).c_str() : "?","bigFont.fnt");
    numeric->setScale(extent * 0.62f/std::max(1.0f,numeric->getContentSize().height));
    numeric->setPosition({extent/2+1,height/2});numeric->setColor(ccc3(255,255,255));row->addChild(numeric,3);
    auto value = RankedMapDetails::get().difficulty(levelId);
    if (info.officialLevelId || value.label == "N/A") {
        if (demon) value=mapDifficulty(false,true,0,0);
        else if (info.stars) {
            auto const stars=*info.stars;
            auto const average=stars>=8 ? 5 : stars>=6 ? 4 : stars>=4 ? 3 : stars==3 ? 2 : stars==2 ? 1 : 0;
            value=mapDifficulty(stars==1,false,0,average);
        }
    }
    auto* face = CCSprite::createWithSpriteFrameName(value.frame.data());
    auto const faceX=extent*1.5f+5;
    if (face && face->getContentSize().width>0 && face->getContentSize().height>0) {
        face->setScale(std::min(extent/face->getContentSize().width,extent/face->getContentSize().height));
        face->setPosition({faceX,height/2});row->addChild(face);
    }
    auto const textLeft=extent*2+8;
    auto* text=menuLabel(demon ? "GDDL" : std::string(value.label),0.23f,{(width+textLeft)/2,height/2},Text,FontWeight::SemiBold);
    fitMenuLabelWidth(text,std::max(10.0f,width-textLeft),0.23f,0.14f);row->addChild(text);
    row->setID("ranked-map-difficulty"_spr); return row;
}
} // namespace corum::ranked::ui
