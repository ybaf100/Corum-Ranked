#pragma once

#include "../RankedVolumeControl.hpp"
#include "../RankedRuntime.hpp"
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
    CCLabelBMFont* m_forumStatus = nullptr;
    CCMenuItemSpriteExtra* m_refreshRecords = nullptr;
    bool m_closed = false, m_exited = false, m_dirty = false;

    bool init() {
        if (!Popup::init(350, 285)) return false;
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        setID("ranked-settings-popup"_spr);
        auto* panel = surface({350, 285}, {175, 142.5f}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(panel, 10);
        auto* title = label(i18n::text("settings", "Settings"), 0.55f, {175, 252}, Text, FontWeight::Black);
        fitLabelWidth(title, 300, 0.55f, 0.32f); panel->addChild(title, 10);
        auto* caption = label(i18n::text("ranked-volume", "Ranked volume"), 0.32f,
            {175, 215}, Text, FontWeight::ExtraBold);
        fitLabelWidth(caption, 295, 0.32f, 0.20f); panel->addChild(caption, 10);
        m_volume = Slider::create(this, menu_selector(RankedSettingsPopup::onVolume), 0.85f);
        if (!m_volume) return false;
        m_volume->setID("ranked-volume-slider"_spr);
        m_volume->setPosition({175, 179});
        m_volume->setValue(RankedVolumeControl::get().volume());
        panel->addChild(m_volume, 20);
        m_percent = label("", 0.27f, {175, 154}, Gold, FontWeight::ExtraBold);
        panel->addChild(m_percent, 10); updatePercent();
        auto* detail = label(i18n::text("ranked-volume-scope", "Ranked music and sound effects"),
            0.23f, {175, 133}, TextMuted);
        fitLabelWidth(detail, 312, 0.23f, 0.16f); panel->addChild(detail, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu, 20);
        auto* refreshFace = surface({275, 31}, {}, Cyan, SurfaceRaised, 250, true);
        auto* refreshText = label(i18n::text("forum-refresh", "Refresh forum records"),
            0.27f, {137.5f, 15.5f}, Text, FontWeight::ExtraBold);
        fitLabelWidth(refreshText, 252, 0.27f, 0.18f); refreshFace->addChild(refreshText);
        m_refreshRecords = CCMenuItemSpriteExtra::create(refreshFace, this, menu_selector(RankedSettingsPopup::onRefreshRecords));
        m_refreshRecords->setID("ranked-forum-refresh-button"_spr);
        m_refreshRecords->setPosition({175, 98}); menu->addChild(m_refreshRecords);
        m_forumStatus = label("", 0.20f, {175, 65}, TextMuted);
        panel->addChild(m_forumStatus);
        RankedRuntime::get().fetchForumRecords();
        schedule(schedule_selector(RankedSettingsPopup::updateForumStatus), 0.15f);
        updateForumStatus(0);
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
    void onRefreshRecords(CCObject*) {
        if (!m_closed) RankedRuntime::get().fetchForumRecords(true);
        updateForumStatus(0);
    }
    void updateForumStatus(float) {
        if (m_closed) return;
        auto const& view = RankedRuntime::get().view().forumRecords;
        m_refreshRecords->setEnabled(!view.loading);
        std::string text;
        if (view.loading) text = i18n::text("forum-updating", "Updating forum records...");
        else if (!view.error.empty()) text = i18n::text("forum-update-failed", "Update failed. Try again.");
        else {
            bool failed = false;
            for (auto const& record : view.records) {
                if (!text.empty()) text += "  /  ";
                text += record.provider == "GDKF" ? "GDKF " : "Pointercrate ";
                if (record.rank) text += fmt::format("#{}", *record.rank);
                else text += record.status == "UNRANKED" ? "-" : "?";
                failed |= record.status == "ERROR";
            }
            if (failed) text += "\n" + i18n::text("forum-cache-kept", "API unavailable; cached records kept.");
        }
        m_forumStatus->setString(text.c_str()); fitLabelWidth(m_forumStatus, 310, 0.20f, 0.14f);
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
        unschedule(schedule_selector(RankedSettingsPopup::updateForumStatus));
        Popup::onClose(nullptr);
    }
    void onExit() override {
        m_closed = true;
        unschedule(schedule_selector(RankedSettingsPopup::updateForumStatus));
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
