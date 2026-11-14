#pragma once

#include "RankedVisuals.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

namespace corum::ranked::ui {

using namespace geode::prelude;

// The season ladder bar during a promotion series: the full bar in the
// tier's own colour (silver for a SILVER series, gold for GOLD, ...) with the
// achievement title's shine, a light band sweeping across every 2.4 s over a
// slow pulse. Same slant and footprint as the lobby's plain ladder bar.
class LadderShineBar final : public CCLayerColor {
    float m_width = 0.0f, m_height = 0.0f, m_time = 0.0f;
    ccColor3B m_tone{};
    CCLayerColor* m_fill = nullptr;
    CCLayerColor* m_band = nullptr;

    static CCLayerColor* piece(ccColor3B color, GLubyte alpha, float width, float height) {
        auto* layer = CCLayerColor::create({color.r, color.g, color.b, alpha}, width, height);
        layer->setSkewX(20.0f);
        layer->setCascadeOpacityEnabled(true);
        return layer;
    }

public:
    static constexpr float SweepSeconds = 2.4f;

    // `leftCenter` like addSkewBar: the bar's left end, vertically centred.
    static LadderShineBar* create(CCPoint leftCenter, float width, float height, ccColor3B tone) {
        auto* bar = new LadderShineBar;
        // A zero-sized, untinted root that only passes the lobby fade down.
        if (!bar->initWithColor({255, 255, 255, 255}, 0.0f, 0.0f)) { delete bar; return nullptr; }
        bar->setCascadeOpacityEnabled(true);
        bar->setPosition({leftCenter.x, leftCenter.y - height / 2.0f});
        bar->m_width = width;
        bar->m_height = height;
        bar->m_tone = tone;
        bar->addChild(piece({32, 35, 46}, 255, width, height), 0);
        bar->m_fill = piece(tone, 255, width, height);
        bar->addChild(bar->m_fill, 1);
        // A thin light edge along the top, as on the achievement bars.
        auto* edge = piece({255, 255, 255}, 70, width, std::max(1.0f, height * 0.3f));
        edge->setPosition({height * 0.7f * std::tan(20.0f * 0.01745329f), height * 0.7f});
        bar->addChild(edge, 2);
        bar->m_band = piece({255, 255, 255}, 0, std::min(22.0f, width * 0.12f), height);
        bar->addChild(bar->m_band, 3);
        bar->scheduleUpdate();
        bar->autorelease();
        bar->update(0.0f);
        return bar;
    }

    void update(float dt) override {
        m_time += std::clamp(dt, 0.0f, 0.25f);
        // Slow pulse: the tier colour brightens a little towards white.
        auto const pulse = 0.12f + 0.10f * std::sin(m_time * 3.0f);
        auto const lift = [&](GLubyte c) { return static_cast<GLubyte>(std::lround(c + (255 - c) * pulse)); };
        m_fill->setColor({lift(m_tone.r), lift(m_tone.g), lift(m_tone.b)});
        // Sweep: crosses the bar in the first 1.6 s of each 2.4 s, fading in and out.
        auto const phase = std::fmod(m_time, SweepSeconds) / 1.6f;
        auto const bandWidth = m_band->getContentSize().width;
        if (phase >= 1.0f) {
            m_band->setOpacity(0);
            return;
        }
        m_band->setPositionX(std::max(0.0f, m_width - bandWidth) * phase);
        m_band->setOpacity(static_cast<GLubyte>(std::lround(150.0f * std::sin(phase * 3.14159265f))));
    }
};

} // namespace corum::ranked::ui
