#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace corum::ranked::ui {
// GD paints CCLayerColor chrome from its own opacity. Fade those renderable
// layers explicitly as well as inheriting the content group's text/icon fade.
// One tween on the faded root drives every surface through strong refs: no
// per-layer CCEase wrapper exists whose separately owned inner fade could be
// released under it (iOS crash in CCEaseSineIn::update, beta.137).
inline constexpr int ModeLobbySurfaceFadeTag = 0x5f4d;
struct ModeLobbySurfaceStep {
    geode::Ref<cocos2d::CCLayerColor> layer;
    GLubyte from = 0, to = 0;
};
inline float modeLobbySurfaceProgress(float progress, bool reveal) {
    progress = std::clamp(progress,0.0f,1.0f);
    return reveal ? std::sin(progress * 1.57079632679f) : 1.0f - std::cos(progress * 1.57079632679f);
}
class ModeLobbySurfaceFade final : public cocos2d::CCActionInterval {
public:
    static ModeLobbySurfaceFade* create(float seconds,std::vector<ModeLobbySurfaceStep> steps,bool reveal) {
        auto* action=new ModeLobbySurfaceFade;
        if(!action->initWithDuration(seconds)){delete action;return nullptr;}
        action->m_steps=std::move(steps);action->m_reveal=reveal;action->autorelease();return action;
    }
    void update(float progress) override {
        auto const eased=modeLobbySurfaceProgress(progress,m_reveal);
        for(auto& step:m_steps)if(step.layer)step.layer->setOpacity(static_cast<GLubyte>(std::lround(
            step.from+(static_cast<float>(step.to)-static_cast<float>(step.from))*eased)));
    }
private:
    std::vector<ModeLobbySurfaceStep> m_steps;
    bool m_reveal=true;
};
inline void collectModeLobbySurfaces(cocos2d::CCNode* root,bool reveal,std::vector<ModeLobbySurfaceStep>& steps) {
    for(auto* child:geode::cocos::CCArrayExt<cocos2d::CCNode*>(root->getChildren())) {
        if(auto* layer=dynamic_cast<cocos2d::CCLayerColor*>(child)) {
            auto const size=layer->getContentSize();
            if(size.width>0 && size.height>0) {
                auto const opacity=layer->getOpacity();
                steps.push_back({geode::Ref<cocos2d::CCLayerColor>(layer),static_cast<GLubyte>(reveal?0:opacity),static_cast<GLubyte>(reveal?opacity:0)});
                if(reveal)layer->setOpacity(0);
            }
        }
        collectModeLobbySurfaces(child,reveal,steps);
    }
}
inline void fadeModeLobbySurfaces(cocos2d::CCNode* root,float seconds,bool reveal) {
    if(!root)return;
    root->stopActionByTag(ModeLobbySurfaceFadeTag);
    std::vector<ModeLobbySurfaceStep> steps;
    collectModeLobbySurfaces(root,reveal,steps);
    if(steps.empty())return;
    if(auto* fade=ModeLobbySurfaceFade::create(seconds,std::move(steps),reveal)){fade->setTag(ModeLobbySurfaceFadeTag);root->runAction(fade);}
}
}
