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
    CCLabelBMFont* m_forumCooldown = nullptr;
    CCMenuItemSpriteExtra* m_refreshRecords = nullptr;
    bool m_refreshDisabled = false;
    bool m_closed = false, m_exited = false, m_dirty = false;

    CCNode* refreshButtonFace(bool disabled) {
        auto* face = surface({264, 31}, {}, Cyan, SurfaceRaised, 250, true);
        auto* caption = menuLabel(i18n::text("forum-refresh", "Refresh forum records"),
            0.34f, {132, 15.5f}, Text, FontWeight::ExtraBold);
        fitMenuLabelWidth(caption, 242, 0.34f, 0.26f); face->addChild(caption, 5);
        if (disabled) grayButtonVisual(face);
        return face;
    }

    bool init() {
        if (!Popup::init(300, 252)) return false;
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        setID("ranked-settings-popup"_spr);
        auto* panel = surface({300, 252}, {150, 126}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(panel, 10);
        auto* title = menuLabel(i18n::text("settings", "Settings"), 0.55f, {150, 220}, Text, FontWeight::Black);
        fitMenuLabelWidth(title, 268, 0.55f, 0.34f); panel->addChild(title, 10);
        auto* caption = menuLabel(i18n::text("ranked-volume", "Ranked menu BGM"), 0.36f,
            {150, 185}, Text, FontWeight::ExtraBold);
        fitMenuLabelWidth(caption, 264, 0.36f, 0.28f); panel->addChild(caption, 10);
        m_volume = Slider::create(this, menu_selector(RankedSettingsPopup::onVolume), 0.85f);
        if (!m_volume) return false;
        m_volume->setID("ranked-volume-slider"_spr);
        m_volume->setPosition({150, 155});
        m_volume->setValue(RankedVolumeControl::get().volume());
        panel->addChild(m_volume, 20);
        m_percent = menuLabel("", 0.37f, {150, 130}, Gold, FontWeight::ExtraBold);
        panel->addChild(m_percent, 10); updatePercent();
        auto* detail = menuLabel(i18n::text("ranked-volume-scope", "Lobby, matchmaking and results BGM"),
            0.24f, {150, 108}, TextMuted);
        fitMenuLabelWidth(detail, 264, 0.24f, 0.20f); panel->addChild(detail, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu, 20);
        auto* refreshFace = refreshButtonFace(false);
        m_refreshRecords = CCMenuItemSpriteExtra::create(refreshFace, this, menu_selector(RankedSettingsPopup::onRefreshRecords));
        m_refreshRecords->setID("ranked-forum-refresh-button"_spr);
        m_refreshRecords->setPosition({150, 84}); menu->addChild(m_refreshRecords);
        m_forumStatus = menuLabel("", 0.22f, {150, 59}, TextMuted);
        panel->addChild(m_forumStatus, 10);
        m_forumCooldown = menuLabel("", 0.26f, {150, 41}, TextMuted, FontWeight::ExtraBold);
        m_forumCooldown->setID("ranked-forum-refresh-cooldown"_spr);
        panel->addChild(m_forumCooldown, 10);
        RankedRuntime::get().fetchForumRecords();
        schedule(schedule_selector(RankedSettingsPopup::updateForumStatus), 0.15f);
        updateForumStatus(0);
        auto* face = surface({120, 26}, {}, Blue, SurfaceRaised, 250, true);
        auto* done = menuLabel(i18n::text("close", "Close"), 0.36f, {60, 13}, Text, FontWeight::ExtraBold);
        fitMenuLabelWidth(done, 100, 0.36f, 0.26f); face->addChild(done, 5);
        auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedSettingsPopup::onClose));
        item->setPosition({150, 19}); menu->addChild(item);
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
        auto const remaining = RankedRuntime::get().forumRefreshRemainingMs();
        auto const disabled = view.loading || remaining > 0;
        m_refreshRecords->setEnabled(!disabled);
        if (disabled != m_refreshDisabled) {
            m_refreshDisabled = disabled;
            m_refreshRecords->setNormalImage(refreshButtonFace(disabled));
        }
        auto const seconds = (remaining + 999) / 1000;
        auto const cooldown = remaining > 0 ? fmt::format("{} {:02}:{:02}",
            i18n::text("forum-refresh-cooldown", "Next refresh in"), seconds / 60, seconds % 60) : "";
        m_forumCooldown->setString(cooldown.c_str());
        fitMenuLabelWidth(m_forumCooldown, 264, 0.26f, 0.22f);
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
        m_forumStatus->setString(text.c_str()); fitFullMenuLabel(m_forumStatus, 264, 14, 0.22f);
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
