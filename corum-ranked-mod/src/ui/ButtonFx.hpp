#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

namespace corum::ranked::ui {

using namespace geode::prelude;

// Standard: every Corum button face. Hero: the mode start button, which also
// breathes while idle, grows under the pointer and bursts when pressed.
enum class ButtonFxKind { Standard, Hero };

inline constexpr int ButtonFxTag = 8040;
// Lean of every slanted slab, caption and title, in degrees.
inline constexpr float SlabSkew = 8.0f;

// Hover and press feedback for a Corum button face.
//
// The node lives inside the face (the menu item's image) and looks for its
// CCMenuItemSpriteExtra on the first frames; a face that never becomes a
// button stops updating and stays an empty node. Feedback is drawn with
// CCLayerColor overlays only, so it never changes a hit box, a caption or the
// item's own press bounce, and it needs no texture.
class ButtonFx final : public CCNode {
    WeakRef<CCMenuItemSpriteExtra> m_item;
    ButtonFxKind m_kind = ButtonFxKind::Standard;
    float m_skew = 0.0f;
    // Hero only: the scaled body and the two burst slabs behind it.
    CCNode* m_body = nullptr;
    CCLayerColor* m_pings[2] = {nullptr, nullptr};
    CCLayerColor* m_glow = nullptr;
    CCLayerColor* m_sweep = nullptr;
    int m_probes = 0;
    bool m_resolved = false, m_hover = false, m_pressed = false;
    float m_glowAlpha = 0.0f, m_sweepTime = 9.0f, m_sweepFrom = 0.0f, m_sweepTo = 0.0f;
    float m_pingTime[2] = {9.0f, 9.0f}, m_pingReach[2] = {0.2f, 0.2f}, m_pingPeak[2] = {0.0f, 0.0f};
    float m_idle = 0.0f, m_scale = 1.0f, m_bodyScale = 1.0f;

    static bool shown(CCNode* node) {
        for (auto* p = node; p; p = p->getParent()) if (!p->isVisible()) return false;
        return node && node->isRunning();
    }
    // A modal on top owns the pointer: buttons under it must not light up.
    static bool underModal(CCNode* item) {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return false;
        CCNode* top = nullptr;
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren()))
            if (typeinfo_cast<FLAlertLayer*>(child) && child->isVisible()) top = child;
        if (!top) return false;
        for (auto* p = item; p; p = p->getParent()) if (p == top) return false;
        return true;
    }
    bool resolve() {
        auto* face = getParent();
        if (!face) return false;
        CCMenuItemSpriteExtra* found = nullptr;
        int depth = 0;
        for (auto* p = face->getParent(); p && depth < 3; p = p->getParent(), ++depth) {
            found = typeinfo_cast<CCMenuItemSpriteExtra*>(p);
            if (found) break;
        }
        if (!found) return false;
        auto const size = face->getContentSize();
        if (size.width <= 0.0f || size.height <= 0.0f) return false;
        // A chip inside a larger button is decoration, not the button's face.
        if (auto* image = found->getNormalImage()) {
            auto const whole = image->getContentSize();
            if (size.width < whole.width * 0.7f || size.height < whole.height * 0.7f) {
                m_probes = 99;
                return false;
            }
        }
        m_item = WeakRef(found);
        // Button-shaped faces lean like every other slab, captions included.
        // Rows, cards and square icon buttons keep their rectangle.
        if (m_kind == ButtonFxKind::Standard && m_skew == 0.0f && face->getSkewX() == 0.0f &&
            size.height <= 40.0f && size.width <= 200.0f && size.width >= size.height * 1.6f)
            face->setSkewX(SlabSkew);
        m_glow = CCLayerColor::create({255, 255, 255, 0});
        m_glow->setContentSize(size);
        m_glow->setSkewX(m_skew);
        this->addChild(m_glow, 1);
        auto const lean = m_skew > 0.0f ? m_skew : 16.0f;
        auto const overhang = m_skew > 0.0f ? 0.0f : std::tan(lean * 0.01745329f) * size.height * 0.5f;
        auto const bar = std::clamp(size.width * 0.14f, 5.0f, 24.0f);
        m_sweep = CCLayerColor::create({255, 255, 255, 0});
        m_sweep->setContentSize({bar, size.height});
        m_sweep->setSkewX(lean);
        m_sweepFrom = overhang;
        m_sweepTo = std::max(m_sweepFrom, size.width - bar - overhang);
        m_sweep->setPosition({m_sweepFrom, 0.0f});
        this->addChild(m_sweep, 2);
        if (m_body) m_bodyScale = m_body->getScale();
        return true;
    }
    void ping(int index, float reach, float peak, float delay = 0.0f) {
        if (!m_pings[index]) return;
        m_pingTime[index] = -delay;
        m_pingReach[index] = reach;
        m_pingPeak[index] = peak;
    }
    void stepPing(int index, float dt) {
        auto* layer = m_pings[index];
        if (!layer) return;
        constexpr float seconds = 0.52f;
        if (m_pingTime[index] >= seconds) {
            if (layer->getOpacity() != 0) layer->setOpacity(0);
            return;
        }
        m_pingTime[index] += dt;
        if (m_pingTime[index] < 0.0f) return;
        auto const p = std::clamp(m_pingTime[index] / seconds, 0.0f, 1.0f);
        auto const out = 1.0f - (1.0f - p) * (1.0f - p);
        layer->setScale(1.0f + m_pingReach[index] * out);
        layer->setOpacity(static_cast<GLubyte>(std::lround(m_pingPeak[index] * (1.0f - p))));
    }

public:
    static ButtonFx* create(ButtonFxKind kind = ButtonFxKind::Standard, float skew = 0.0f, CCNode* body = nullptr,
        CCLayerColor* ping = nullptr, CCLayerColor* echo = nullptr) {
        auto* node = new ButtonFx;
        if (!node->init()) { delete node; return nullptr; }
        node->m_kind = kind;
        node->m_skew = skew;
        node->m_body = body;
        node->m_pings[0] = ping;
        node->m_pings[1] = echo;
        node->setTag(ButtonFxTag);
        node->autorelease();
        node->scheduleUpdate();
        return node;
    }
    void update(float dt) override {
        dt = std::clamp(dt, 0.0f, 0.05f);
        if (!m_resolved) {
            m_resolved = resolve();
            if (!m_resolved) {
                if (++m_probes >= 3) this->unscheduleUpdate();
                return;
            }
        }
        auto item = m_item.lock();
        if (!item) { this->unscheduleUpdate(); return; }
        auto const hero = m_kind == ButtonFxKind::Hero;
        auto const live = item->isEnabled() && shown(item.data());
        auto const pressed = live && item->isSelected();
        bool hover = false;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (live) {
            auto const point = item->convertToNodeSpace(geode::cocos::getMousePos());
            auto const size = item->getContentSize();
            hover = CCRect{0, 0, size.width, size.height}.containsPoint(point) && !underModal(item.data());
        }
#endif
        if (pressed && !m_pressed) {
            // The press lands as a flash, a shine across the face and, on the
            // start button, two slabs bursting outwards.
            m_glowAlpha = hero ? 190.0f : 120.0f;
            m_sweepTime = 0.0f;
            if (hero) { ping(0, 0.46f, 210.0f); ping(1, 0.80f, 130.0f, 0.10f); }
        } else if (hover && !m_hover) {
            m_sweepTime = 0.0f;
            if (hero) { ping(0, 0.24f, 150.0f); m_idle = 0.0f; }
        }
        m_pressed = pressed;
        m_hover = hover;

        auto const target = pressed ? (hero ? 78.0f : 54.0f) : hover ? (hero ? 40.0f : 26.0f) : 0.0f;
        m_glowAlpha += (target - m_glowAlpha) * std::min(1.0f, dt * 14.0f);
        auto const glow = static_cast<GLubyte>(std::lround(std::clamp(m_glowAlpha, 0.0f, 255.0f)));
        if (m_glow->getOpacity() != glow) m_glow->setOpacity(glow);

        auto const sweepSeconds = hero ? 0.46f : 0.30f;
        if (m_sweepTime < sweepSeconds) {
            m_sweepTime += dt;
            auto const p = std::clamp(m_sweepTime / sweepSeconds, 0.0f, 1.0f);
            auto const eased = p * p * (3.0f - 2.0f * p);
            m_sweep->setPositionX(m_sweepFrom + (m_sweepTo - m_sweepFrom) * eased);
            m_sweep->setOpacity(static_cast<GLubyte>(std::lround(std::sin(p * 3.14159265f) * (hero ? 165.0f : 96.0f))));
        } else if (m_sweep->getOpacity() != 0) {
            m_sweep->setOpacity(0);
        }
        if (!hero) return;

        // Idle: a slow shine keeps the start button alive. Under the pointer
        // it pulses faster and the body grows; a press squeezes it.
        if (live && !pressed) {
            m_idle += dt;
            if (hover && m_idle >= 0.95f) { m_idle = 0.0f; ping(0, 0.24f, 120.0f); }
            else if (!hover && m_idle >= 2.6f) { m_idle = 0.0f; m_sweepTime = 0.0f; ping(0, 0.14f, 70.0f); }
        }
        stepPing(0, dt);
        stepPing(1, dt);
        auto const grow = pressed ? 0.95f : hover ? 1.07f : 1.0f;
        m_scale += (grow - m_scale) * std::min(1.0f, dt * 13.0f);
        if (m_body) m_body->setScale(m_bodyScale * m_scale);
    }
};

} // namespace corum::ranked::ui
