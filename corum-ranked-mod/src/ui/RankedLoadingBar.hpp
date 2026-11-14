#pragma once

#include "RankedVisuals.hpp"
#include "../domain/LoadingProgress.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

namespace corum::ranked::ui {

using namespace geode::prelude;

// The lobby's loading bar: a slanted track with a lime fill, eased by
// advanceLoadingBar. The shown value belongs to the lobby, so the re-render
// each status change causes continues the same bar instead of restarting it.
class RankedLoadingBar final : public CCNode {
    float* m_shown = nullptr;
    LoadingStep m_step;
    float m_width = 0.0f, m_height = 0.0f;
    CCLayerColor* m_fill = nullptr;

    void layout() {
        auto const filled = std::clamp(*m_shown, 0.0f, 1.0f) * m_width;
        m_fill->setVisible(filled > 0.5f);
        m_fill->setContentSize({std::max(filled, 0.5f), m_height});
    }

public:
    static RankedLoadingBar* create(float width, float height, float* shown, LoadingStep step) {
        auto* bar = new RankedLoadingBar;
        if (!bar->init() || !shown) { delete bar; return nullptr; }
        bar->m_shown = shown;
        bar->m_step = step;
        bar->m_width = width;
        bar->m_height = height;
        bar->setContentSize({width, height});
        auto* track = CCLayerColor::create({SurfaceRaised.r, SurfaceRaised.g, SurfaceRaised.b, 255}, width, height);
        track->setSkewX(20.0f);
        bar->addChild(track, 0);
        bar->m_fill = CCLayerColor::create({Accent.r, Accent.g, Accent.b, 255}, 0.5f, height);
        bar->m_fill->setSkewX(20.0f);
        bar->addChild(bar->m_fill, 1);
        bar->layout();
        bar->scheduleUpdate();
        bar->autorelease();
        return bar;
    }

    void update(float dt) override {
        *m_shown = advanceLoadingBar(*m_shown, m_step, dt);
        layout();
    }
};

} // namespace corum::ranked::ui
