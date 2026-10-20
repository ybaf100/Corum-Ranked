#pragma once

#include "../RankedVolumeControl.hpp"
#include "../RankedRuntime.hpp"
#include "../SurvivalOptionsPreferences.hpp"
#include "SurvivalOptionSwitch.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankedCreditsPopup.hpp"
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
    CCMenuItemSpriteExtra *m_defaultLdm=nullptr,*m_defaultShake=nullptr,*m_thumbnails=nullptr,*m_selectionMotion=nullptr;
    bool m_refreshDisabled = false;
    bool m_closed = false, m_exited = false, m_dirty = false;

    CCNode* refreshButtonFace(bool disabled) {
        auto* face = surface({320, 32}, {}, Cyan, SurfaceRaised, 250, true);
        auto* caption = menuLabel(i18n::text("forum-refresh", "Refresh forum records"),
            0.34f, {160, 16}, Text, FontWeight::ExtraBold);
        fitFullMenuLabel(caption, 298, 22, 0.34f); face->addChild(caption, 5);
        if (disabled) grayButtonVisual(face);
        return face;
    }

    bool init() override {
        if (!Popup::init(360, 354)) return false;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-settings-popup"_spr);
        auto const viewport=CCDirector::sharedDirector()->getWinSize();
        m_mainLayer->setScale(std::min({1.f,(viewport.width-24)/360,(viewport.height-24)/354}));
        auto* panel=surface({360,354},{180,177},Cyan,Surface,255,true);m_mainLayer->addChild(panel,0);
        auto add=[&](std::string const& text,CCPoint position,CCSize bounds,float scale,ccColor3B color,FontWeight weight=FontWeight::ExtraBold){
            auto* label=menuLabel(text,scale,position,color,weight);fitFullMenuLabel(label,bounds.width,bounds.height,scale);panel->addChild(label,10);return label;
        };
        add(i18n::text("settings","Settings"),{180,331},{330,24},.5f,Text,FontWeight::Black);
        panel->addChild(surface({160,110},{94,259},Blue,SurfaceRaised,245,false));
        panel->addChild(surface({160,110},{266,259},Blue,SurfaceRaised,245,false));
        add(i18n::text("ranked-volume","Ranked menu BGM"),{94,298},{146,16},.26f,Text);
        m_volume=Slider::create(this,menu_selector(RankedSettingsPopup::onVolume),.55f);if(!m_volume)return false;
        m_volume->setID("ranked-volume-slider"_spr);m_volume->setPosition({94,271});m_volume->setValue(RankedVolumeControl::get().volume());panel->addChild(m_volume,20);
        m_percent=add("",{94,247},{146,18},.30f,Gold);updatePercent();
        add(i18n::text("ranked-volume-scope","Lobby, matchmaking and results BGM"),{94,222},{144,26},.20f,TextMuted,FontWeight::SemiBold);
        add(i18n::text("survival-default-options","SURVIVAL DEFAULTS"),{266,298},{146,16},.24f,Cyan);
        // Use Popup's own menu: it already owns the modal input priority.
        auto* menu=m_buttonMenu;menu->setZOrder(20);
        auto const defaults=SurvivalOptionsPreferences::get().defaults();
        auto toggle=[&](std::string const& caption,bool value,CCPoint at,int tag){
            auto* item=CCMenuItemSpriteExtra::create(survivalOptionFace({146,32},caption,value),this,menu_selector(RankedSettingsPopup::onDefaults));
            item->setPosition(at);item->setTag(tag);menu->addChild(item);return item;
        };
        m_defaultLdm=toggle(i18n::text("survival-default-ldm","Default LDM"),defaults.lowDetail,{266,272},0);m_defaultLdm->setID("survival-default-ldm-switch"_spr);
        m_defaultShake=toggle(i18n::text("survival-default-shake","Default Disable Shake"),defaults.disableShake,{266,232},1);m_defaultShake->setID("survival-default-shake-switch"_spr);
        auto& preferences=SurvivalOptionsPreferences::get();
        m_thumbnails=toggle(i18n::text("survival-image-thumbnails","Level Thumbnails"),preferences.thumbnails(),{94,176},2);
        m_thumbnails->setID("survival-thumbnails-switch"_spr);
        m_selectionMotion=toggle(i18n::text("survival-selection-motion","Level selection motion"),preferences.selectionMotion(),{266,176},3);
        m_selectionMotion->setID("survival-selection-motion-switch"_spr);updateMotionSetting();
        m_refreshRecords=CCMenuItemSpriteExtra::create(refreshButtonFace(false),this,menu_selector(RankedSettingsPopup::onRefreshRecords));
        m_refreshRecords->setID("ranked-forum-refresh-button"_spr);m_refreshRecords->setPosition({180,100});menu->addChild(m_refreshRecords);
        m_forumStatus=add("",{180,73},{320,14},.20f,TextMuted,FontWeight::SemiBold);
        m_forumCooldown=add("",{180,54},{320,14},.23f,TextMuted);m_forumCooldown->setID("ranked-forum-refresh-cooldown"_spr);
        RankedRuntime::get().fetchForumRecords();schedule(schedule_selector(RankedSettingsPopup::updateForumStatus),.15f);updateForumStatus(0);
        auto* face=surface({120,32},{},Blue,SurfaceRaised,250,true);
        auto* done=menuLabel(i18n::text("close","Close"),.32f,{60,16},Text,FontWeight::ExtraBold);fitFullMenuLabel(done,100,22,.32f);face->addChild(done,5);
        auto* close=CCMenuItemSpriteExtra::create(face,this,menu_selector(RankedSettingsPopup::onClose));close->setPosition({266,25});menu->addChild(close);
        auto* creditsFace=surface({120,32},{},Cyan,SurfaceRaised,250,true);auto* creditsText=menuLabel("CREDITS",.30f,{60,16},Text,FontWeight::ExtraBold);creditsFace->addChild(creditsText,5);
        auto* credits=CCMenuItemSpriteExtra::create(creditsFace,this,menu_selector(RankedSettingsPopup::onCredits));credits->setID("ranked-credits-button"_spr);credits->setPosition({94,25});menu->addChild(credits);
        // Includes the one-time migration if this is the first Settings visit.
        m_dirty=true;return true;
    }
    void onDefaults(CCObject* sender) {
        if(m_closed)return;auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);if(!item)return;
        auto& preferences=SurvivalOptionsPreferences::get();auto const before=preferences.defaults();
        if(item->getTag()==0)preferences.setDefaultLowDetail(!before.lowDetail);
        else if(item->getTag()==1)preferences.setDefaultDisableShake(!before.disableShake);
        else if(item->getTag()==2)preferences.setThumbnails(!preferences.thumbnails());
        else if(item->getTag()==3 && preferences.thumbnails())preferences.setSelectionMotion(!preferences.selectionMotion());
        updateMotionSetting();
        auto const value=preferences.defaults();
        m_defaultLdm->setNormalImage(survivalOptionFace({146,32},i18n::text("survival-default-ldm","Default LDM"),value.lowDetail));
        m_defaultShake->setNormalImage(survivalOptionFace({146,32},i18n::text("survival-default-shake","Default Disable Shake"),value.disableShake));m_dirty=true;
    }
    void updateMotionSetting(){
        auto const& p=SurvivalOptionsPreferences::get();
        m_thumbnails->setNormalImage(survivalOptionFace({146,32},i18n::text("survival-image-thumbnails","Level Thumbnails"),p.thumbnails()));
        auto* face=survivalOptionFace({146,32},i18n::text("survival-selection-motion","Level selection motion"),p.selectionMotion());
        if(!p.thumbnails())grayButtonVisual(face);
        m_selectionMotion->setNormalImage(face);m_selectionMotion->setEnabled(p.thumbnails());
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
    void onCredits(CCObject*){if(!m_closed)showRankedCredits();}
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
        fitFullMenuLabel(m_forumCooldown, 320, 14, .23f);
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
        m_forumStatus->setString(text.c_str()); fitFullMenuLabel(m_forumStatus, 320, 14, 0.20f);
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
            if (m_dirty) (void)Mod::get()->saveData();
        }
        Popup::onExit();
    }
};

} // namespace corum::ranked::ui
