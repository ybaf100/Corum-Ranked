#pragma once

#include "RankedInput.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <functional>
#include <utility>

namespace corum::ranked::ui {
using namespace geode::prelude;

class RankedModeDisabledPopup final : public Popup {
    bool m_closed = false, m_exited = false, m_cancelled = false;
    CCMenu* m_actions = nullptr;
    std::function<void(bool)> m_finished;

    bool init(std::string const& mode, std::function<void(bool)> finished) {
        if (!Popup::init(320, 180)) return false;
        m_finished = std::move(finished);
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        setID("ranked-mode-disabled-popup"_spr);
        auto* panel = surface({320, 180}, {160, 90}, Gold, Surface, 255, true);
        m_mainLayer->addChild(panel);
        auto* title = menuLabel(mode + " · " + i18n::text("mode-disabled", "MODE DISABLED"),
            .40f, {160, 150}, Gold, FontWeight::SemiBold);
        fitMenuLabelWidth(title, 292, .40f, .25f);
        panel->addChild(title, 5);
        auto* detail = menuLabel(i18n::text("mode-disabled-notice",
            "This mode is currently disabled.\nContinue to the view-only lobby."),
            .30f, {160, 108}, Text, FontWeight::SemiBold);
        fitFullMenuLabel(detail, 290, 48, .30f);
        panel->addChild(detail, 5);
        m_actions = CCMenu::create();
        m_actions->setPosition(CCPointZero);
        panel->addChild(m_actions, 20);
        auto* face = surface({274, 32}, {}, Gold, SurfaceRaised, 255, true);
        auto* caption = menuLabel(i18n::text("confirm", "CONFIRM"), .35f,
            {137, 16}, Gold, FontWeight::SemiBold);
        fitMenuLabelWidth(caption, 252, .35f, .27f);
        face->addChild(caption, 5);
        auto* confirm = CCMenuItemSpriteExtra::create(face, this,
            menu_selector(RankedModeDisabledPopup::onBrowse));
        confirm->setID("ranked-disabled-browse-confirm"_spr);
        confirm->setPosition({160, 58});
        m_actions->addChild(confirm);
        auto* cancel = CCMenuItemSpriteExtra::create(
            menuLabel(i18n::text("cancel", "CANCEL"), .30f, {}, Text, FontWeight::SemiBold),
            this, menu_selector(RankedModeDisabledPopup::onClose));
        cancel->setPosition({160, 23});
        m_actions->addChild(cancel);
        return true;
    }

    void finish(bool browse) {
        if (m_closed) return;
        m_closed = true;
        for (auto* object : CCArrayExt<CCObject*>(m_actions->getChildren()))
            if (auto* item = typeinfo_cast<CCMenuItem*>(object)) item->setEnabled(false);
        // Retire touch dispatch before opening the channel, outside the current
        // menu callback. Keep the popup alive until that retirement completes.
        auto const keep = Ref<RankedModeDisabledPopup>(this);
        auto finished = std::exchange(m_finished, {});
        queueInMainThread([keep, browse, finished = std::move(finished)] {
            if (!keep->isRunning()) return;
            auto const accepted = browse && !keep->m_cancelled;
            cancelMenuTouch(keep->m_actions);
            keep->Popup::onClose(nullptr);
            if (finished) finished(accepted);
        });
    }

    void onBrowse(CCObject*) { finish(true); }

public:
    static RankedModeDisabledPopup* create(std::string const& mode, std::function<void(bool)> finished) {
        auto* popup = new RankedModeDisabledPopup;
        if (popup->init(mode, std::move(finished))) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
    void onClose(CCObject*) override { m_cancelled = true; finish(false); }
    void dismiss() { onClose(nullptr); }
    void onExit() override {
        m_closed = true;
        m_finished = {};
        if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
}
