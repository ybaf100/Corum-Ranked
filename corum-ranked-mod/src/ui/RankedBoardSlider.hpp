#pragma once

#include "RankedVisuals.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <functional>

namespace corum::ranked::ui {
using namespace geode::prelude;

// Q1 board slider: a thin slanted track (.bar.thin), the lime fill and the
// light slanted knob (.sk 16 x 20 px). A touch only starts on the slider
// and only while its popup is the topmost alert, so a dialog opened above
// the popup keeps its own input.
class RankedBoardSlider final : public CCLayer {
    std::function<void()> m_changed;
    WeakRef<CCNode> m_owner;
    CCLayerColor* m_fill = nullptr;
    CCLayerColor* m_knob = nullptr;
    float m_width = 0.0f, m_value = 0.0f;
    int m_touch = -1;

    bool setup(float width, float value, CCNode* owner, std::function<void()> changed) {
        if (!CCLayer::init()) return false;
        m_width = std::max(1.0f, width); m_owner = WeakRef(owner); m_changed = std::move(changed);
        setContentSize({m_width, 10.0f});
        auto* track = CCLayerColor::create({32, 35, 46, 255}, m_width, 3.0f);
        track->setPosition({0.0f, 3.5f}); track->setSkewX(20.0f); addChild(track, 1);
        m_fill = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 255}, 0.0f, 3.0f);
        m_fill->setPosition({0.0f, 3.5f}); m_fill->setSkewX(20.0f); addChild(m_fill, 2);
        m_knob = CCLayerColor::create({Text.r, Text.g, Text.b, 255}, 8.0f, 10.0f);
        m_knob->setSkewX(8.0f); addChild(m_knob, 3);
        setValue(value);
        setTouchEnabled(true);
        return true;
    }
    bool topAlert() const {
        auto owner = m_owner.lock();
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!owner || !scene || owner->getParent() != scene || !scene->getChildren()) return false;
        CCNode* top = nullptr;
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child->isVisible() || !typeinfo_cast<FLAlertLayer*>(child)) continue;
            if (!top || child->getZOrder() >= top->getZOrder()) top = child;
        }
        return top == owner.data();
    }
    void drag(float x) {
        setValue(x / m_width);
        if (m_changed) m_changed();
    }

public:
    static RankedBoardSlider* create(float width, float value, CCNode* owner, std::function<void()> changed) {
        auto* slider = new RankedBoardSlider;
        if (slider->setup(width, value, owner, std::move(changed))) { slider->autorelease(); return slider; }
        delete slider; return nullptr;
    }
    float getValue() const { return m_value; }
    void setValue(float value) {
        m_value = std::clamp(value, 0.0f, 1.0f);
        if (m_fill) m_fill->setContentSize({m_width * m_value, 3.0f});
        if (m_knob) m_knob->setPosition({m_width * m_value - 4.0f, 0.0f});
    }
    void registerWithTouchDispatcher() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->addPrioTargetedDelegate(this, -65000, true);
    }
    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (m_touch != -1 || !isVisible() || !topAlert()) return false;
        auto const point = convertToNodeSpace(touch->getLocation());
        if (point.x < -6.0f || point.x > m_width + 6.0f || point.y < -8.0f || point.y > 18.0f) return false;
        m_touch = touch->getID();
        drag(point.x);
        return true;
    }
    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (touch->getID() == m_touch) drag(convertToNodeSpace(touch->getLocation()).x);
    }
    void ccTouchEnded(CCTouch* touch, CCEvent*) override {
        if (touch->getID() == m_touch) m_touch = -1;
    }
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override { ccTouchEnded(touch, event); }
    void onExit() override {
        m_touch = -1;
        CCLayer::onExit();
    }
};

} // namespace corum::ranked::ui
