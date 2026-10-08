#pragma once

#include "../RankedLevelPreparation.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/LevelTools.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/ui/Popup.hpp>
#include <set>

namespace corum::ranked::ui {
using namespace geode::prelude;

inline bool rankedJukeboxLoaded() {
    return Loader::get() && Loader::get()->isModLoaded("fleym.nongd");
}
inline CCMenuItem* rankedJukeboxOpener(CustomSongWidget* widget) {
    if (!rankedJukeboxLoaded() || !widget || !widget->m_songInfoObject) return nullptr;
    auto* item = typeinfo_cast<CCMenuItem*>(widget->getChildByIDRecursive("fleym.nongd/nong-button"));
    return item && item->m_pListener == widget && item->m_pfnSelector && item->isEnabled() ? item : nullptr;
}

// Jukebox v3.8.0 has no public popup-opening API. Ask its own native widget
// callback to open the UI; never cast to its private class or call a detour address.
class RankedJukeboxPopup final : public Popup {
    CustomSongWidget* m_widget = nullptr;
    Ref<CCNode> m_countdown;
    CCLabelBMFont* m_countdownLabel = nullptr;
    bool m_openRequested = false, m_opened = false;
    LevelPreparationContext m_context;
    bool m_closed = false, m_exited = false;
    std::vector<WeakRef<FLAlertLayer>> m_ownedDialogs;

    bool init(GJGameLevel* level) {
        if (!rankedJukeboxLoaded() || !level || !RankedLevelPreparation::get().authorizes(level, currentPreparationContext()) ||
            !Popup::init(345, 200)) return false;
        m_context = currentPreparationContext();
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-jukebox-popup"_spr);
        // Invisible carrier: the Ranked button opens the actual Jukebox UI.
        setVisible(false);
        auto const robtop = level->m_songID <= 0;
        auto* music = MusicDownloadManager::sharedState();
        if (!music) return false;
        auto* info = robtop ? LevelTools::getSongObject(level->m_audioTrack) : music->getSongInfoObject(level->m_songID);
        if (!info && !robtop) info = SongInfoObject::create(level->m_songID);
        if (!info) return false;
        m_widget = CustomSongWidget::create(info, nullptr, false, false, false, robtop, false, false, 0);
        if (!m_widget) return false;
        // Hidden carrier for Jukebox's own callback and SongInfo/NONG events.
        // No native playback/download controls or duplicate delegate ownership.
        m_widget->setVisible(false); m_mainLayer->addChild(m_widget);
        if (!level->m_songIDs.empty() || !level->m_sfxIDs.empty())
            m_widget->updateWithMultiAssets(level->m_songIDs, level->m_sfxIDs, 0);
        schedule(schedule_selector(RankedJukeboxPopup::watch), 0.2f); watch(0);
        return true;
    }
    void disableCarrierMenus(CCNode* node) {
        if (!node) return;
        if (auto* menu = typeinfo_cast<CCMenu*>(node)) menu->setTouchEnabled(false);
        for (auto* child : node->getChildrenExt()) disableCarrierMenus(child);
    }
    void watch(float) {
        if (m_closed) return;
        if (!rankedJukeboxLoaded() || m_context != currentPreparationContext()) { dismiss(); return; }
        disableCarrierMenus(m_widget);
        bool attached = false;
        for (auto const& weak : m_ownedDialogs) if (auto dialog = weak.lock(); dialog && dialog->getParent()) attached = true;
        if (m_opened && !attached) { dismiss(); return; }
        if (m_openRequested && !m_opened && rankedJukeboxOpener(m_widget)) onOpen(nullptr);
    }
    void onOpen(CCObject*) {
        if (m_closed || m_context != currentPreparationContext()) return;
        for (auto const& weak : m_ownedDialogs) if (auto dialog = weak.lock(); dialog && dialog->getParent()) return;
        auto* opener = rankedJukeboxOpener(m_widget);
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!opener || !scene) return;
        std::set<CCNode*> before;
        for (auto* child : scene->getChildrenExt()) before.insert(child);
        [[maybe_unused]] Ref<RankedJukeboxPopup> keepAlive(this);
        m_opened = true;
        opener->activate();
        // Track only dialogs created by this exact synchronous callback. Never
        // close an unrelated mod's pre-existing modal on Ranked scene exit.
        for (auto* child : scene->getChildrenExt()) {
            if (before.count(child)) continue;
            if (auto* dialog = typeinfo_cast<FLAlertLayer*>(child)) m_ownedDialogs.emplace_back(dialog);
        }
        if (m_closed || m_context != currentPreparationContext()) { closeOwnedDialogs(); return; }
        if (!m_ownedDialogs.empty()) {
            auto const size = CCDirector::sharedDirector()->getWinSize();
            auto* timer = surface({126, 30}, {size.width - 77, size.height - 25}, Cyan, Surface, 245, true);
            timer->setID("ranked-jukebox-countdown"_spr);
            m_countdownLabel = menuLabel("", 0.25f, {63, 15}, Gold, FontWeight::ExtraBold);
            timer->addChild(m_countdownLabel, 5);
            m_countdown = timer; scene->addChild(timer, 32000);
        }
    }
    void closeOwnedDialogs() {
        if (m_countdown && m_countdown->getParent()) m_countdown->removeFromParentAndCleanup(true);
        m_countdown = nullptr; m_countdownLabel = nullptr;
        auto dialogs = std::move(m_ownedDialogs); m_ownedDialogs.clear();
        for (auto const& weak : dialogs) if (auto dialog = weak.lock(); dialog && dialog->getParent()) {
            dialog->keyBackClicked();
            CCTouchDispatcher::get()->unregisterForcePrio(dialog.data());
        }
    }
public:
    void openWhenReady() { m_openRequested = true; watch(0); }
    void setCountdown(std::string const& text) {
        if (!m_closed && m_countdownLabel) {
            m_countdownLabel->setString(text.c_str());
            fitMenuLabelWidth(m_countdownLabel, 114, 0.25f, 0.16f);
        }
    }
    static RankedJukeboxPopup* create(GJGameLevel* level) {
        auto* popup = new RankedJukeboxPopup;
        if (popup->init(level)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onClose(CCObject*) override { dismiss(); }
    void dismiss() {
        if (m_closed) return;
        [[maybe_unused]] Ref<RankedJukeboxPopup> keepAlive(this);
        m_closed = true; closeOwnedDialogs(); Popup::onClose(nullptr);
    }
    void onExit() override {
        m_closed = true; unschedule(schedule_selector(RankedJukeboxPopup::watch));
        if (!m_exited) {
            m_exited = true; closeOwnedDialogs();
            CCTouchDispatcher::get()->unregisterForcePrio(this);
        }
        Popup::onExit();
    }
};
} // namespace corum::ranked::ui
