#pragma once

#include "../RankedVolumeControl.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/Popup.hpp>
#include <cmath>

namespace corum::ranked::ui {
using namespace geode::prelude;

class RankedSettingsPopup final : public Popup {
    Slider* m_volume = nullptr;
    CCLabelBMFont* m_percent = nullptr;
    bool m_closed = false, m_exited = false, m_dirty = false;

    bool init() {
        if (!Popup::init(350, 205)) return false;
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        setID("ranked-settings-popup"_spr);
        auto* panel = surface({350, 205}, {175, 102.5f}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(panel, 10);
        auto* title = label(i18n::text("settings", "Settings"), 0.55f, {175, 172}, Text, FontWeight::Black);
        fitLabelWidth(title, 300, 0.55f, 0.32f); panel->addChild(title, 10);
        auto* caption = label(i18n::text("ranked-volume", "Ranked volume"), 0.32f,
            {175, 135}, Text, FontWeight::ExtraBold);
        fitLabelWidth(caption, 295, 0.32f, 0.20f); panel->addChild(caption, 10);
        m_volume = Slider::create(this, menu_selector(RankedSettingsPopup::onVolume), 0.85f);
        if (!m_volume) return false;
        m_volume->setID("ranked-volume-slider"_spr);
        m_volume->setPosition({175, 99});
        m_volume->setValue(RankedVolumeControl::get().volume());
        panel->addChild(m_volume, 20);
        m_percent = label("", 0.27f, {175, 74}, Gold, FontWeight::ExtraBold);
        panel->addChild(m_percent, 10); updatePercent();
        auto* detail = label(i18n::text("ranked-volume-scope", "Ranked music and sound effects"),
            0.23f, {175, 53}, TextMuted);
        fitLabelWidth(detail, 312, 0.23f, 0.16f); panel->addChild(detail, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu, 20);
        auto* face = surface({130, 29}, {}, Blue, SurfaceRaised, 250, true);
        auto* done = label(i18n::text("close", "Close"), 0.28f, {65, 14.5f}, Text, FontWeight::ExtraBold);
        fitLabelWidth(done, 112, 0.28f, 0.18f); face->addChild(done, 5);
        auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedSettingsPopup::onClose));
        item->setPosition({175, 23}); menu->addChild(item);
        return true;
    }
    void updatePercent() {
        auto const text = fmt::format("{}%", static_cast<int>(std::lround(RankedVolumeControl::get().volume() * 100)));
        m_percent->setString(text.c_str());
    }
    void onVolume(CCObject*) {
        if (m_closed) return;
        RankedVolumeControl::get().setVolume(m_volume->getValue());
        m_dirty = true; updatePercent();
    }
public:
    static RankedSettingsPopup* create() {
        auto* popup = new RankedSettingsPopup;
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onClose(CCObject*) override { dismiss(); }
    void dismiss() {
        if (m_closed) return;
        [[maybe_unused]] Ref<RankedSettingsPopup> keepAlive(this);
        m_closed = true;
        Popup::onClose(nullptr);
    }
    void onExit() override {
        m_closed = true;
        if (!m_exited) {
            m_exited = true;
            // Popup's destructor can be delayed by WeakRefPool. Release only
            // this modal's registration while its scene is leaving.
            CCTouchDispatcher::get()->unregisterForcePrio(this);
            if (m_dirty) Mod::get()->saveData();
        }
        Popup::onExit();
    }
};

} // namespace corum::ranked::ui
