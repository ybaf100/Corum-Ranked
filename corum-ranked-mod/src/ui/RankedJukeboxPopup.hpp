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
    CCMenuItemSpriteExtra* m_open = nullptr;
    CCLabelBMFont* m_status = nullptr;
    LevelPreparationContext m_context;
    bool m_closed = false, m_exited = false;
    std::vector<WeakRef<FLAlertLayer>> m_ownedDialogs;

    bool init(GJGameLevel* level) {
        if (!rankedJukeboxLoaded() || !level || !RankedLevelPreparation::get().authorizes(level, currentPreparationContext()) ||
            !Popup::init(345, 200)) return false;
        m_context = currentPreparationContext();
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-jukebox-popup"_spr);
        auto* panel = surface({345, 200}, {172.5f, 100}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(panel, 10);
        panel->addChild(label("Jukebox", 0.50f, {172.5f, 169}, Text, FontWeight::Black), 10);
        auto* map = label(std::string(level->m_levelName.c_str()), 0.28f, {172.5f, 140}, Cyan, FontWeight::SemiBold);
        fitLabelWidth(map, 306, 0.28f, 0.17f); panel->addChild(map, 10);
        auto const robtop = level->m_songID <= 0;
        auto* music = MusicDownloadManager::sharedState();
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
        auto* help = label(i18n::text("jukebox-song-scope", "Change the song used by this map"),
            0.24f, {172.5f, 114}, TextMuted);
        fitLabelWidth(help, 305, 0.24f, 0.16f); panel->addChild(help, 10);
        m_status = label("", 0.21f, {172.5f, 89}, Gold, FontWeight::SemiBold); panel->addChild(m_status, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu, 20);
        auto button = [&](std::string text, CCPoint position, SEL_MenuHandler handler) {
            auto* face = surface({165, 29}, {}, Blue, SurfaceRaised, 250, true);
            auto* caption = label(text, 0.26f, {82.5f, 14.5f}, Text, FontWeight::ExtraBold);
            fitLabelWidth(caption, 150, 0.26f, 0.17f); face->addChild(caption, 5);
            auto* item = CCMenuItemSpriteExtra::create(face, this, handler);
            item->setPosition(position); menu->addChild(item); return item;
        };
        m_open = button(i18n::text("jukebox-open", "Open Jukebox"), {172.5f, 62}, menu_selector(RankedJukeboxPopup::onOpen));
        button(i18n::text("close", "Close"), {172.5f, 24}, menu_selector(RankedJukeboxPopup::onClose));
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
        auto const ready = rankedJukeboxOpener(m_widget) != nullptr;
        m_open->setEnabled(ready); m_open->setOpacity(ready ? 255 : 130);
        m_status->setString(i18n::text(ready ? "ready" : "jukebox-waiting", ready ? "READY" : "Waiting for Jukebox song data...").c_str());
        fitLabelWidth(m_status, 305, 0.21f, 0.13f);
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
        opener->activate();
        // Track only dialogs created by this exact synchronous callback. Never
        // close an unrelated mod's pre-existing modal on Ranked scene exit.
        for (auto* child : scene->getChildrenExt()) {
            if (before.count(child)) continue;
            if (auto* dialog = typeinfo_cast<FLAlertLayer*>(child)) m_ownedDialogs.emplace_back(dialog);
        }
    }
    void closeOwnedDialogs() {
        auto dialogs = std::move(m_ownedDialogs); m_ownedDialogs.clear();
        for (auto const& weak : dialogs) if (auto dialog = weak.lock(); dialog && dialog->getParent()) {
            dialog->keyBackClicked();
            CCTouchDispatcher::get()->unregisterForcePrio(dialog.data());
        }
    }
public:
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
