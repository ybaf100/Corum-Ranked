#pragma once
#include "RankedVisuals.hpp"
#include <algorithm>
#include <string>

namespace corum::ranked::ui {
using namespace geode::prelude;

/** Presentational cover shown while the native world is fast-forwarded
 * (replay seek, Survival live-world catch-up, spectator restore backfill).
 * A dim full-screen layer hides the intermediate frames; a centered Corum
 * window shows the caption and the real simulated progress. It never drives
 * or alters the simulation. */
struct SeekProgressOverlay {
    CCLayerColor* layer = nullptr;
    CCLayerColor* fill = nullptr;
    CCLabelBMFont* caption = nullptr;
    CCLabelBMFont* detail = nullptr;
    float barWidth = 0;
    bool shown = false;

    void attach(CCNode* parent, int z) {
        if (layer || !parent) return;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        layer = CCLayerColor::create(ccc4(11, 12, 15, 214), size.width, size.height);
        if (!layer) return;
        layer->setVisible(false);
        parent->addChild(layer, z);
        auto const width = std::min(360.f, size.width - 40.f), height = 92.f;
        auto* window = surface({width, height}, {size.width / 2, size.height / 2}, Accent, Surface, 250, true);
        layer->addChild(window, 1);
        barWidth = width - 40.f;
        auto* track = CCLayerColor::create(ccc4(255, 255, 255, 40), barWidth, 8);
        track->setPosition({20.f, 24.f});
        window->addChild(track, 5);
        fill = CCLayerColor::create(ccc4(Accent.r, Accent.g, Accent.b, 255), barWidth, 8);
        fill->setAnchorPoint({0, 0});
        fill->ignoreAnchorPointForPosition(false);
        fill->setPosition({20.f, 24.f});
        fill->setScaleX(0);
        window->addChild(fill, 6);
        caption = menuLabel("", .30f, {width / 2, height - 24.f}, Text, FontWeight::ExtraBold);
        window->addChild(caption, 7);
        detail = menuLabel("", .22f, {width / 2, 46.f}, TextMuted, FontWeight::SemiBold);
        window->addChild(detail, 7);
    }

    void hide() {
        if (layer && shown) layer->setVisible(false);
        shown = false;
    }

    void show(std::string const& title, std::string const& info, double progress) {
        if (!layer) return;
        if (!shown) layer->setVisible(true);
        shown = true;
        auto const value = static_cast<float>(std::clamp(progress, 0., 1.));
        if (fill) fill->setScaleX(value);
        if (caption) {
            caption->setString(title.c_str());
            fitFullMenuLabel(caption, barWidth, 22, .30f);
        }
        if (detail) {
            detail->setString(info.c_str());
            fitFullMenuLabel(detail, barWidth, 16, .22f);
        }
    }
};
}
