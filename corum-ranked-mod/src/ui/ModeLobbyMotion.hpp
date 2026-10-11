#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

namespace corum::ranked::ui {
// Corum panels and badge assemblies contain plain CCNodes. Native cascade
// stops at those nodes; walk opacity through them without rebuilding the UI.
inline void propagateModeOpacity(cocos2d::CCNode* node, GLubyte inherited) {
    if (auto* rgba = dynamic_cast<cocos2d::CCRGBAProtocol*>(node)) {
        rgba->updateDisplayedOpacity(inherited);
        if (rgba->isCascadeOpacityEnabled()) inherited = rgba->getDisplayedOpacity();
    }
    for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) propagateModeOpacity(child,inherited);
}
inline void setModeLobbyOpacity(cocos2d::CCLayerColor* group, GLubyte opacity) {
    group->setOpacity(opacity);
    for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(group->getChildren())) propagateModeOpacity(child,opacity);
}

class ModeLobbyFade final : public cocos2d::CCActionInterval {
public:
    static ModeLobbyFade* create(float seconds, GLubyte opacity) {
        auto* action = new ModeLobbyFade;
        if (!action->initWithDuration(seconds)) { delete action; return nullptr; }
        action->m_end = opacity; action->autorelease(); return action;
    }
    void startWithTarget(cocos2d::CCNode* target) override {
        cocos2d::CCActionInterval::startWithTarget(target);
        m_group = dynamic_cast<cocos2d::CCLayerColor*>(target);
        m_start = m_group ? m_group->getOpacity() : 255;
    }
    void update(float progress) override {
        if (m_group) setModeLobbyOpacity(m_group,static_cast<GLubyte>(std::lround(
            m_start + (m_end - m_start) * std::clamp(progress,0.0f,1.0f))));
    }
private:
    cocos2d::CCLayerColor* m_group = nullptr;
    int m_start = 255, m_end = 255;
};
} // namespace corum::ranked::ui
