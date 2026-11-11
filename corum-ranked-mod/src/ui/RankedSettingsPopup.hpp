#pragma once

#include "../RankedVolumeControl.hpp"
#include "../RankedRuntime.hpp"
#include "../SurvivalOptionsPreferences.hpp"
#include "SurvivalOptionSwitch.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankedCreditsPopup.hpp"
#include "RankedPopupLayout.hpp"
#include "RankedConfirmPopup.hpp"
#include "../replay/SurvivalReplaySeekSpeed.hpp"
#include "../debug/DebugLogUploader.hpp"
#include <Geode/binding/Slider.hpp>
#include <Geode/ui/Popup.hpp>
#include <array>
#include <functional>
#include <cmath>

namespace corum::ranked::ui {
using namespace geode::prelude;

// Actions the Settings popup hands back to the Ranked screen that opened it.
struct RankedSettingsHooks {
    std::function<void()> languageChanged;
    std::function<void()> updateChannel;
};

// Q1 board: title and X, language, menu BGM, replay seek speed, the four
// Survival defaults as switches, and a row of ghost buttons.
class RankedSettingsPopup final : public Popup {
    static constexpr float kWidth = 340, kHeight = 296, kPad = 12;
    RankedSettingsHooks m_hooks;
    Slider* m_volume = nullptr;
    CCLabelBMFont* m_percent = nullptr;
    CCLabelBMFont* m_forumStatus = nullptr;
    CCLabelBMFont* m_forumCooldown = nullptr;
    CCMenuItemSpriteExtra* m_refreshRecords = nullptr;
    CCMenuItemSpriteExtra *m_defaultLdm=nullptr,*m_defaultShake=nullptr,*m_thumbnails=nullptr,*m_selectionMotion=nullptr;
    std::array<CCMenuItemSpriteExtra*,4> m_seekOptions{};
    bool m_refreshDisabled = false;
    bool m_closed = false, m_exited = false, m_dirty = false;

    static CCNode* ghostFace(std::string const& text, float width, ccColor3B color = Text) {
        auto* face = surface({width, 20}, {}, Steel, SurfaceRaised, 250, true);
        auto* caption = menuLabel(text, .22f, {width / 2, 10}, color, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(caption, width - 10, 14, .22f); face->addChild(caption, 5);
        return face;
    }
    CCNode* refreshButtonFace(bool disabled) {
        auto* face = ghostFace(i18n::text("forum-refresh", "Refresh forum records"), 92);
        if (disabled) grayButtonVisual(face);
        return face;
    }
    // Board .xbtn: a square ghost button with an X drawn from two bars.
    static CCNode* closeFace() {
        auto* face = surface({20, 20}, {}, Steel, SurfaceRaised, 250, true);
        for (int i = 0; i < 2; ++i) {
            auto* bar = CCLayerColor::create({Text.r, Text.g, Text.b, 255});
            bar->setContentSize({11, 2}); bar->setPosition({10 - 5.5f, 10 - 1}); bar->setRotation(i ? -45.f : 45.f);
            face->addChild(bar, 5);
        }
        return face;
    }
    // Board .toggle: a full-width row with its caption on the left and a
    // switch on the right; the whole row is the touch target.
    static CCNode* toggleFace(std::string const& caption, bool on) {
        auto* face = CCNode::create(); face->setAnchorPoint({.5f, .5f}); face->setContentSize({kWidth - kPad * 2, 18});
        auto* name = menuLabel(caption, .24f, {0, 9}, Text, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(name, kWidth - kPad * 2 - 40, 14, .24f); name->setAnchorPoint({0, .5f}); face->addChild(name, 5);
        auto const track = on ? Accent : SurfaceSoft;
        auto* rail = CCLayerColor::create({track.r, track.g, track.b, 255}, 26, 12);
        rail->setPosition({kWidth - kPad * 2 - 26, 3}); face->addChild(rail, 4);
        auto const knob = on ? AccentInk : TextMuted;
        auto* thumb = CCLayerColor::create({knob.r, knob.g, knob.b, 255}, 8, 8);
        thumb->setPosition({on ? 16.f : 2.f, 2}); rail->addChild(thumb, 1);
        return face;
    }
    // Board .lang / .seg: the chosen cell is filled, the rest are ghosts.
    static CCNode* segmentFace(std::string const& text, float width, bool on, ccColor3B color = Accent) {
        auto* face = surface({width, 18}, {}, on ? color : Steel, on ? color : SurfaceRaised, 255, true);
        auto* caption = menuLabel(text, .21f, {width / 2, 9}, on ? AccentInk : TextMuted, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(caption, width - 6, 13, .21f); face->addChild(caption, 5);
        return face;
    }

    bool init(RankedSettingsHooks hooks) {
        auto const layout=rankedPopupLayout({kWidth,kHeight},CCDirector::sharedDirector()->getWinSize());
        if (!Popup::init(layout.size.width,layout.size.height)) return false;
        m_noElasticity=true;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-settings-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        auto* panel=surface({kWidth,kHeight},{kWidth/2,kHeight/2},Accent,Surface,255,true);content->addChild(panel,0);
        auto add=[&](std::string const& text,CCPoint position,CCSize bounds,float scale,ccColor3B color,FontWeight weight=FontWeight::ExtraBold,float anchor=0.f){
            auto* label=menuLabel(text,scale,position,color,weight);fitSingleLineMenuLabel(label,bounds.width,bounds.height,scale);
            label->setAnchorPoint({anchor,.5f});panel->addChild(label,10);return label;
        };
        m_hooks=std::move(hooks);
        // Use Popup's own menu: it already owns the modal input priority.
        auto* menu=m_buttonMenu;menu->setZOrder(20);
        auto button=[&](CCNode* face,SEL_MenuHandler handler,CCPoint at,int tag=0){
            auto* item=CCMenuItemSpriteExtra::create(face,this,handler);item->setPosition(at);item->setTag(tag);menu->addChild(item);return item;
        };
        auto const right=kWidth-kPad;
        // Title row: the leaning 28 px title and the X.
        if(auto* title=add(i18n::text("settings","Settings"),{kPad,276},{240,24},28.f/64.f,Text,FontWeight::Black))leanLabel(title);
        button(closeFace(),menu_selector(RankedSettingsPopup::onClose),{right-10,276})->setID("ranked-settings-close"_spr);
        // Language: KO / EN.
        add(i18n::text("settings-language","Language"),{kPad,248},{180,14},.27f,Text);
        add(i18n::text("settings-language-scope","Ranked screen language"),{kPad,237},{180,11},.19f,TextMuted,FontWeight::SemiBold);
        auto const korean=i18n::isKorean();
        button(segmentFace("KO",34,korean),menu_selector(RankedSettingsPopup::onLanguage),{right-53,242},1)->setID("ranked-settings-language-ko"_spr);
        button(segmentFace("EN",34,!korean),menu_selector(RankedSettingsPopup::onLanguage),{right-17,242},0)->setID("ranked-settings-language-en"_spr);
        // Menu BGM with its value on the right and the slider below.
        add(i18n::text("ranked-volume","Ranked menu BGM"),{kPad,215},{220,14},.27f,Text);
        add(i18n::text("ranked-volume-scope","Lobby, matchmaking and results BGM"),{kPad,204},{220,11},.19f,TextMuted,FontWeight::SemiBold);
        m_percent=add("",{right,210},{60,16},.30f,Accent,FontWeight::ExtraBold,1.f);
        m_volume=Slider::create(this,menu_selector(RankedSettingsPopup::onVolume),1.2f);if(!m_volume)return false;
        m_volume->setID("ranked-volume-slider"_spr);m_volume->setPosition({kWidth/2,186});m_volume->setValue(RankedVolumeControl::get().volume());panel->addChild(m_volume,20);
        updatePercent();
        // Replay & spectate seek speed: a four-cell segmented control.
        add(i18n::text("replay-seek-speed","Replay & spectate seek speed"),{kPad,163},{156,14},.27f,Text);
        add(i18n::text("replay-seek-scope","Faster seeking works the device harder"),{kPad,152},{156,11},.19f,TextMuted,FontWeight::SemiBold);
        for(std::size_t i=0;i<m_seekOptions.size();++i){
            auto const speed=replay::kReplaySeekSpeeds[i];
            m_seekOptions[i]=button(seekOptionFace(speed,false),menu_selector(RankedSettingsPopup::onSeekOption),
                {right-148+19+37.f*static_cast<float>(i),157},replay::replaySeekSpeedToInt(speed));
        }
        m_seekOptions[0]->setID("replay-seek-speed-button"_spr);updateSeekOptions();
        // Survival defaults.
        add(i18n::text("survival-default-options","SURVIVAL DEFAULTS"),{kPad,131},{220,12},.22f,Accent);
        auto const defaults=SurvivalOptionsPreferences::get().defaults();
        auto& preferences=SurvivalOptionsPreferences::get();
        auto toggle=[&](std::string const& caption,bool value,float y,int tag){
            return button(toggleFace(caption,value),menu_selector(RankedSettingsPopup::onDefaults),{kWidth/2,y},tag);
        };
        m_defaultLdm=toggle(i18n::text("survival-default-ldm","Default LDM"),defaults.lowDetail,113,0);m_defaultLdm->setID("survival-default-ldm-switch"_spr);
        m_defaultShake=toggle(i18n::text("survival-default-shake","Default Disable Shake"),defaults.disableShake,93,1);m_defaultShake->setID("survival-default-shake-switch"_spr);
        m_thumbnails=toggle(i18n::text("survival-image-thumbnails","Level Thumbnails"),preferences.thumbnails(),73,2);
        m_thumbnails->setID("survival-thumbnails-switch"_spr);
        m_selectionMotion=toggle(i18n::text("survival-selection-motion","Fancy Level Selection Animation"),preferences.selectionMotion(),53,3);
        m_selectionMotion->setID("survival-selection-motion-switch"_spr);
        // Forum record status above the ghost button row.
        m_forumStatus=add("",{kPad,34},{200,12},.19f,TextMuted,FontWeight::SemiBold);
        m_forumCooldown=add("",{right,34},{110,12},.19f,TextMuted,FontWeight::SemiBold,1.f);m_forumCooldown->setID("ranked-forum-refresh-cooldown"_spr);
        m_refreshRecords=button(refreshButtonFace(false),menu_selector(RankedSettingsPopup::onRefreshRecords),{kPad+46,16});
        m_refreshRecords->setID("ranked-forum-refresh-button"_spr);
        float x=kPad+92+6;
        if(m_hooks.updateChannel){
            button(ghostFace(i18n::text("update-channel","UPDATE CHANNEL"),76),menu_selector(RankedSettingsPopup::onUpdateChannel),{x+38,16})
                ->setID("ranked-settings-update-channel"_spr);
            x+=76+6;
        }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        button(ghostFace(i18n::text("debug-send-logs","SEND LOGS"),64),menu_selector(RankedSettingsPopup::onSendLogs),{x+32,16})
            ->setID("ranked-settings-send-logs"_spr);
#endif
        (void)x;
        button(ghostFace("CREDITS",64),menu_selector(RankedSettingsPopup::onCredits),{right-32,16})->setID("ranked-credits-button"_spr);
        RankedRuntime::get().fetchForumRecords();schedule(schedule_selector(RankedSettingsPopup::updateForumStatus),.15f);updateForumStatus(0);
        // Includes the one-time migration if this is the first Settings visit.
        m_dirty=true;return true;
    }
    static replay::ReplaySeekSpeed seekSpeed(){return replay::replaySeekSpeedFromInt(static_cast<int>(Mod::get()->getSavedValue<int64_t>("replay-seek-speed",1)));}
    static ccColor3B seekColor(replay::ReplaySeekSpeed speed){
        switch(speed){case replay::ReplaySeekSpeed::Low:return Steel;case replay::ReplaySeekSpeed::Fast:return Gold;
            case replay::ReplaySeekSpeed::UltraFast:return Red;case replay::ReplaySeekSpeed::Normal:break;}
        return Accent;
    }
    static std::string seekName(replay::ReplaySeekSpeed speed){
        switch(speed){case replay::ReplaySeekSpeed::Low:return i18n::text("replay-seek-low","Low");
            case replay::ReplaySeekSpeed::Fast:return i18n::text("replay-seek-fast","Fast");
            case replay::ReplaySeekSpeed::UltraFast:return i18n::text("replay-seek-ultra","Ultra fast");
            case replay::ReplaySeekSpeed::Normal:break;}
        return i18n::text("replay-seek-normal","Normal");
    }
    CCNode* seekOptionFace(replay::ReplaySeekSpeed speed,bool selected){
        return segmentFace(seekName(speed),35,selected,seekColor(speed));
    }
    // Fast is filled yellow and UltraFast red when chosen; the rest lime.
    void updateSeekOptions(){
        auto const current=seekSpeed();
        for(std::size_t i=0;i<m_seekOptions.size();++i)if(auto* item=m_seekOptions[i]){
            auto const speed=replay::kReplaySeekSpeeds[i];item->setNormalImage(seekOptionFace(speed,speed==current));
        }
    }
    void setSeekSpeed(replay::ReplaySeekSpeed speed){
        Mod::get()->setSavedValue<int64_t>("replay-seek-speed",replay::replaySeekSpeedToInt(speed));m_dirty=true;
        if(!m_closed)updateSeekOptions();
    }
    void onSeekOption(CCObject* sender){
        if(m_closed)return;auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);if(!item)return;
        auto const chosen=replay::replaySeekSpeedFromInt(item->getTag());
        if(chosen==seekSpeed()||!replay::replaySeekSpeedNeedsWarning(chosen)){setSeekSpeed(chosen);return;}
        // Fast/UltraFast are opt-in through the Corum confirm UI; UltraFast has a
        // red, stronger warning.
        Ref<RankedSettingsPopup> keep(this);
        auto const ultra=chosen==replay::ReplaySeekSpeed::UltraFast;
        auto* warning=ultra?
            RankedConfirmPopup::create(i18n::text("replay-seek-ultra-title","ULTRA FAST SEEK WARNING"),
                i18n::text("replay-seek-ultra-warning","Ultra fast seek simulates several times more native frames per screen frame than Fast. Use it only on high-end devices. On lower-end devices the screen may freeze for a long time, the device may overheat, or the game may be force-closed. Use it anyway?"),
                [keep,chosen](bool accepted){if(accepted)keep->setSeekSpeed(chosen);},i18n::text("replay-seek-ultra-confirm","USE ULTRA FAST"),Red):
            RankedConfirmPopup::create(i18n::text("replay-seek-fast-title","FAST REPLAY SEEK"),
                i18n::text("replay-seek-fast-warning","Fast seek simulates many native frames per screen frame. Low-end devices may stutter, heat up or close the game while seeking. Use it?"),
                [keep,chosen](bool accepted){if(accepted)keep->setSeekSpeed(chosen);},i18n::text("replay-seek-fast-confirm","USE FAST"));
        if(warning)warning->show();
    }
    // The popup is rebuilt in the new language by the screen that owns it.
    void onLanguage(CCObject* sender){
        if(m_closed||!sender)return;
        auto const korean=static_cast<CCNode*>(sender)->getTag()==1;
        if(korean==i18n::isKorean())return;
        i18n::setLanguage(korean?i18n::Language::Korean:i18n::Language::English);m_dirty=true;
        [[maybe_unused]] Ref<RankedSettingsPopup> keepAlive(this);
        auto hook=m_hooks.languageChanged;dismiss();if(hook)hook();
    }
    void onUpdateChannel(CCObject*){
        if(m_closed)return;
        [[maybe_unused]] Ref<RankedSettingsPopup> keepAlive(this);
        auto hook=m_hooks.updateChannel;dismiss();if(hook)hook();
    }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void onSendLogs(CCObject*){if(!m_closed)showDebugLogPopup();}
#endif
    void onDefaults(CCObject* sender) {
        if(m_closed)return;auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(sender);if(!item)return;
        auto& preferences=SurvivalOptionsPreferences::get();auto const before=preferences.defaults();
        if(item->getTag()==0)preferences.setDefaultLowDetail(!before.lowDetail);
        else if(item->getTag()==1)preferences.setDefaultDisableShake(!before.disableShake);
        else if(item->getTag()==2)preferences.setThumbnails(!preferences.thumbnails());
        else if(item->getTag()==3)preferences.setSelectionMotion(!preferences.selectionMotion());
        auto const value=preferences.defaults();
        m_defaultLdm->setNormalImage(toggleFace(i18n::text("survival-default-ldm","Default LDM"),value.lowDetail));
        m_defaultShake->setNormalImage(toggleFace(i18n::text("survival-default-shake","Default Disable Shake"),value.disableShake));
        m_thumbnails->setNormalImage(toggleFace(i18n::text("survival-image-thumbnails","Level Thumbnails"),preferences.thumbnails()));
        m_selectionMotion->setNormalImage(toggleFace(i18n::text("survival-selection-motion","Fancy Level Selection Animation"),preferences.selectionMotion()));
        m_dirty=true;
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
        fitSingleLineMenuLabel(m_forumCooldown, 110, 12, .19f);
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
        m_forumStatus->setString(text.c_str()); fitFullMenuLabel(m_forumStatus, 200, 14, 0.19f);
    }
public:
    static RankedSettingsPopup* create(RankedSettingsHooks hooks = {}) {
        auto* popup = new RankedSettingsPopup;
        if (popup->init(std::move(hooks))) { popup->autorelease(); return popup; }
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
