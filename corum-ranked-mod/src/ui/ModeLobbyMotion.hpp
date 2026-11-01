#pragma once
#include <Geode/Geode.hpp>
#include "../domain/RankedMode.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

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

// Easing lives inside the fade: no CCEase wrapper owns a separate inner action.
enum class ModeLobbyEase { Linear, SineIn, SineOut };
inline float modeLobbyEase(ModeLobbyEase ease, float progress) {
    progress = std::clamp(progress,0.0f,1.0f);
    if (ease == ModeLobbyEase::SineIn) return 1.0f - std::cos(progress * 1.57079632679f);
    if (ease == ModeLobbyEase::SineOut) return std::sin(progress * 1.57079632679f);
    return progress;
}
class ModeLobbyFade final : public cocos2d::CCActionInterval {
public:
    static ModeLobbyFade* create(float seconds, GLubyte opacity, ModeLobbyEase ease = ModeLobbyEase::Linear) {
        auto* action = new ModeLobbyFade;
        if (!action->initWithDuration(seconds)) { delete action; return nullptr; }
        action->m_end = opacity; action->m_ease = ease; action->autorelease(); return action;
    }
    void startWithTarget(cocos2d::CCNode* target) override {
        cocos2d::CCActionInterval::startWithTarget(target);
        m_group = dynamic_cast<cocos2d::CCLayerColor*>(target);
        m_start = m_group ? m_group->getOpacity() : 255;
    }
    void update(float progress) override {
        if (m_group) setModeLobbyOpacity(m_group,static_cast<GLubyte>(std::lround(
            m_start + (m_end - m_start) * modeLobbyEase(m_ease,progress))));
    }
private:
    cocos2d::CCLayerColor* m_group = nullptr;
    int m_start = 255, m_end = 255;
    ModeLobbyEase m_ease = ModeLobbyEase::Linear;
};

/** One action owns all crossing nodes, so the content swap cannot drift from
 * their positions. Nodes and existing badge actions survive the whole motion. */
class ModeLobbySlide final : public cocos2d::CCActionInterval {
public:
    static ModeLobbySlide* create(float seconds, cocos2d::CCLayerColor* survival,
        cocos2d::CCLayerColor* solo, ModeLobbyLayout layout, RankedMode mode, bool opening) {
        auto* action = new ModeLobbySlide;
        if (!action->initWithDuration(seconds)) { delete action; return nullptr; }
        action->m_survival = survival; action->m_solo = solo; action->m_layout = layout;
        action->m_mode = mode; action->m_opening = opening; action->autorelease(); return action;
    }
    void startWithTarget(cocos2d::CCNode* target) override {
        cocos2d::CCActionInterval::startWithTarget(target);
        m_panel = dynamic_cast<cocos2d::CCLayerColor*>(target);
        update(0);
    }
    void update(float progress) override {
        if (!m_panel || !m_survival || !m_solo) return;
        auto const frame = modeLobbyFrame(m_layout,m_mode,m_opening ? progress : 1-progress);
        m_survival->setPosition({frame.survivalX,m_layout.centerY});
        m_solo->setPosition({frame.soloX,m_layout.centerY});
        m_panel->setPosition({frame.menuX - 94*m_layout.scale,m_layout.centerY});
        m_panel->setScale(m_layout.scale);
        setModeLobbyOpacity(m_survival.data(),static_cast<GLubyte>(frame.survivalOpacity));
        setModeLobbyOpacity(m_solo.data(),static_cast<GLubyte>(frame.soloOpacity));
        setModeLobbyOpacity(m_panel,static_cast<GLubyte>(frame.menuOpacity));
        auto* selected = m_mode == RankedMode::Survival ? m_survival.data() : m_solo.data();
        if (auto* controls = dynamic_cast<cocos2d::CCLayerColor*>(selected->getChildByID("ranked-mode-entry-controls"_spr)))
            setModeLobbyOpacity(controls,static_cast<GLubyte>(frame.entryOpacity));
    }
private:
    geode::Ref<cocos2d::CCLayerColor> m_survival, m_solo;
    cocos2d::CCLayerColor* m_panel = nullptr;
    ModeLobbyLayout m_layout {};
    RankedMode m_mode = RankedMode::Survival;
    bool m_opening = true;
};

/** Progress-driven tween for the queue expansion (card width, fades). The
 * callback receives eased-by-caller progress in [0,1]. */
class ModeLobbyTween final : public cocos2d::CCActionInterval {
public:
    static ModeLobbyTween* create(float seconds, std::function<void(float)> apply) {
        auto* action = new ModeLobbyTween;
        if (!action->initWithDuration(seconds)) { delete action; return nullptr; }
        action->m_apply = std::move(apply); action->autorelease(); return action;
    }
    void update(float progress) override { if (m_apply) m_apply(std::clamp(progress,0.0f,1.0f)); }
private:
    std::function<void(float)> m_apply;
};
} // namespace corum::ranked::ui
