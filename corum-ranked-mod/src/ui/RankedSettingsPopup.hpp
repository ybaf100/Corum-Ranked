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
#include "RankedBoardSlider.hpp"
#include "../replay/SurvivalReplaySeekSpeed.hpp"
#include "../debug/DebugLogUploader.hpp"
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
    // Board px / 2: the 680 x 592 .pop with 20 x 24 px padding.
    static constexpr float kWidth = 340, kHeight = 296, kPad = 12, kRight = kWidth - kPad;
    RankedSettingsHooks m_hooks;
    RankedBoardSlider* m_volume = nullptr;
    CCLabelBMFont* m_percent = nullptr;
    CCLabelBMFont* m_forumStatus = nullptr;
    CCLabelBMFont* m_forumCooldown = nullptr;
    CCMenuItemSpriteExtra* m_refreshRecords = nullptr;
    CCMenuItemSpriteExtra *m_defaultLdm=nullptr,*m_defaultShake=nullptr,*m_thumbnails=nullptr,*m_selectionMotion=nullptr;
    std::array<CCMenuItemSpriteExtra*,4> m_seekOptions{};
    std::array<float,4> m_seekWidths{};
    bool m_refreshDisabled = false;
    bool m_closed = false, m_exited = false, m_dirty = false;

    // A 1 px (board 2 px) frame around a box; `skipLeft` joins it to the
    // box before it (.lang / .seg button+button {border-left:0}).
    static void frame(CCNode* face, CCSize size, ccColor3B color, bool skipLeft = false) {
        auto line = [&](float x, float y, float w, float h) {
            auto* edge = CCLayerColor::create({color.r, color.g, color.b, 255}, w, h); edge->setPosition({x, y}); face->addChild(edge, 2);
        };
        line(0, 0, size.width, 1); line(0, size.height - 1, size.width, 1);
        if (!skipLeft) line(0, 0, 1, size.height);
        line(size.width - 1, 0, 1, size.height);
    }
    static CCNode* box(CCSize size) {
        auto* face = CCNode::create(); face->setContentSize(size); face->setAnchorPoint({.5f, .5f}); return face;
    }
    // .btn.ghost.xs: 34 px tall, 14 px padding, a slanted #3A3F4E outline.
    static CCNode* ghostFace(std::string const& text) {
        auto* measure = menuLabel(text, 14.f / 64.f, {}, Text, FontWeight::ExtraBold);
        auto const width = measure->getScaledContentSize().width + 14.0f;
        auto* face = slab({width, 17}, {}, ccc3(58, 63, 78), true, Ink);
        auto* caption = menuLabel(text, 14.f / 64.f, {width / 2, 8.5f}, Text, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(caption, width - 6, 12, 14.f / 64.f); face->addChild(caption, 5);
        return face;
    }
    CCNode* refreshButtonFace(bool disabled) {
        auto* face = ghostFace(i18n::text("forum-refresh", "Refresh forum records"));
        if (disabled) grayButtonVisual(face);
        return face;
    }
    // .xbtn: 44 px square, a #2B2F3C frame and an 18 px X.
    static CCNode* closeFace() {
        auto* face = box({22, 22}); frame(face, {22, 22}, ccc3(43, 47, 60));
        for (int i = 0; i < 2; ++i) {
            auto* bar = CCLayerColor::create({Text.r, Text.g, Text.b, 255}, 10.0f, 1.3f);
            bar->setPosition({11 - 5.0f, 11 - 0.65f}); bar->setRotation(i ? -45.f : 45.f); face->addChild(bar, 5);
        }
        return face;
    }
    // .toggle row: the 16 px caption on the left, the slanted 58 x 30 px
    // switch on the right (lime with a dark knob when on).
    static CCNode* toggleFace(std::string const& caption, bool on) {
        constexpr float rowW = kWidth - kPad * 2, rowH = 15;
        auto* face = box({rowW, rowH});
        auto* name = menuLabel(caption, .25f, {0, rowH / 2}, Text, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(name, rowW - 40, 13, .25f); name->setAnchorPoint({0, .5f}); face->addChild(name, 5);
        auto const track = on ? AccentFill : ccc3(43, 47, 60);
        auto* rail = CCLayerColor::create({track.r, track.g, track.b, 255}, 29, 15);
        rail->setPosition({rowW - 29, 0}); rail->setSkewX(8.0f); face->addChild(rail, 4);
        auto const knob = on ? Ink : TextMuted;
        auto* thumb = CCLayerColor::create({knob.r, knob.g, knob.b, 255}, 10, 10);
        thumb->setPosition({on ? 16.5f : 2.5f, 2.5f}); rail->addChild(thumb, 1);
        return face;
    }
    // .lang / .seg cell: the chosen one filled #A6DE24, the rest framed.
    static CCNode* cellFace(std::string const& text, CCSize size, bool on, bool first, float css) {
        auto* face = box(size);
        if (on) {
            auto* fill = CCLayerColor::create({AccentFill.r, AccentFill.g, AccentFill.b, 255}, size.width, size.height); face->addChild(fill, 1);
        } else frame(face, size, ccc3(43, 47, 60), !first);
        auto* caption = menuLabel(text, css / 64.f, {size.width / 2, size.height / 2}, on ? AccentInk : TextMuted, on ? FontWeight::Black : FontWeight::ExtraBold);
        fitSingleLineMenuLabel(caption, size.width - 4, size.height - 4, css / 64.f); face->addChild(caption, 5);
        return face;
    }

    bool init(RankedSettingsHooks hooks) {
        auto const layout=rankedPopupLayout({kWidth,kHeight},CCDirector::sharedDirector()->getWinSize());
        if (!Popup::init(layout.size.width,layout.size.height)) return false;
        m_noElasticity=true;
        m_bgSprite->setVisible(false);m_closeBtn->setVisible(false);setID("ranked-settings-popup"_spr);
        auto* content=rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        auto* panel=surface({kWidth,kHeight},{kWidth/2,kHeight/2},Accent,Surface,255,true);content->addChild(panel,0);
        auto add=[&](std::string const& text,CCPoint position,float width,float scale,ccColor3B color,FontWeight weight=FontWeight::ExtraBold,float anchor=0.f){
            auto* label=menuLabel(text,scale,position,color,weight);fitSingleLineMenuLabel(label,width,scale*48,scale);
            label->setAnchorPoint({anchor,.5f});panel->addChild(label,10);return label;
        };
        m_hooks=std::move(hooks);
        // Use Popup's own menu: it already owns the modal input priority.
        auto* menu=m_buttonMenu;menu->setZOrder(20);
        auto button=[&](CCNode* face,SEL_MenuHandler handler,CCPoint at,int tag=0){
            auto* item=CCMenuItemSpriteExtra::create(face,this,handler);item->setPosition(at);item->setTag(tag);menu->addChild(item);return item;
        };
        constexpr float label = 16.f / 64.f, caption = 13.f / 64.f;
        // Title row (44 px): .pttl and .xbtn.
        if(auto* title=add(i18n::text("settings","Settings"),{kPad,275},240,28.f/64.f,Text,FontWeight::Black))leanLabel(title);
        button(closeFace(),menu_selector(RankedSettingsPopup::onClose),{kRight-11,275})->setID("ranked-settings-close"_spr);
        // Language row: caption column and .lang (two 48 x 44 px cells).
        add(i18n::text("settings-language","Language"),{kPad,251},200,label,Text);
        add(i18n::text("settings-language-scope","Ranked screen language"),{kPad,240},200,caption,TextMuted,FontWeight::SemiBold);
        auto const korean=i18n::isKorean();
        button(cellFace("KO",{24,22},korean,true,14),menu_selector(RankedSettingsPopup::onLanguage),{kRight-36,246},1)->setID("ranked-settings-language-ko"_spr);
        button(cellFace("EN",{24,22},!korean,false,14),menu_selector(RankedSettingsPopup::onLanguage),{kRight-12,246},0)->setID("ranked-settings-language-en"_spr);
        // Menu BGM: caption column, the value (.val) on the right, the slider row below.
        add(i18n::text("ranked-volume","Ranked menu BGM"),{kPad,223},250,label,Text);
        add(i18n::text("ranked-volume-scope","Lobby, matchmaking and results BGM"),{kPad,212},250,caption,TextMuted,FontWeight::SemiBold);
        m_percent=add("",{kRight,218},60,24.f/64.f,Text,FontWeight::Black,1.f);
        m_volume=RankedBoardSlider::create(kWidth-kPad*2,RankedVolumeControl::get().volume(),this,[this]{onVolume(nullptr);});
        if(!m_volume)return false;
        m_volume->setID("ranked-volume-slider"_spr);m_volume->setPosition({kPad,194});panel->addChild(m_volume,20);
        updatePercent();
        // Replay & spectate seek speed: caption column and the four-cell .seg.
        float segW=0;
        for(std::size_t i=0;i<m_seekOptions.size();++i){
            m_seekWidths[i]=menuLabel(seekName(replay::kReplaySeekSpeeds[i]),14.f/64.f,{},Text,FontWeight::ExtraBold)->getScaledContentSize().width+14;
            segW+=m_seekWidths[i];
        }
        add(i18n::text("replay-seek-speed","Replay & spectate seek speed"),{kPad,182},kRight-segW-kPad-8,label,Text);
        add(i18n::text("replay-seek-scope","Faster seeking works the device harder"),{kPad,171},kRight-segW-kPad-8,caption,TextMuted,FontWeight::SemiBold);
        float segX=kRight-segW;
        for(std::size_t i=0;i<m_seekOptions.size();++i){
            auto const speed=replay::kReplaySeekSpeeds[i];
            m_seekOptions[i]=button(seekOptionFace(i,false),menu_selector(RankedSettingsPopup::onSeekOption),{segX+m_seekWidths[i]/2,177},replay::replaySeekSpeedToInt(speed));
            segX+=m_seekWidths[i];
        }
        m_seekOptions[0]->setID("replay-seek-speed-button"_spr);updateSeekOptions();
        // Survival defaults (.sec) and the four switch rows.
        add(i18n::text("survival-default-options","SURVIVAL DEFAULTS"),{kPad,156},250,14.f/64.f,TextMuted);
        auto const defaults=SurvivalOptionsPreferences::get().defaults();
        auto& preferences=SurvivalOptionsPreferences::get();
        auto toggle=[&](std::string const& text,bool value,float y,int tag){
            return button(toggleFace(text,value),menu_selector(RankedSettingsPopup::onDefaults),{kWidth/2,y},tag);
        };
        m_defaultLdm=toggle(i18n::text("survival-default-ldm","Default LDM"),defaults.lowDetail,139,0);m_defaultLdm->setID("survival-default-ldm-switch"_spr);
        m_defaultShake=toggle(i18n::text("survival-default-shake","Default Disable Shake"),defaults.disableShake,119,1);m_defaultShake->setID("survival-default-shake-switch"_spr);
        m_thumbnails=toggle(i18n::text("survival-image-thumbnails","Level Thumbnails"),preferences.thumbnails(),99,2);
        m_thumbnails->setID("survival-thumbnails-switch"_spr);
        m_selectionMotion=toggle(i18n::text("survival-selection-motion","Fancy Level Selection Animation"),preferences.selectionMotion(),79,3);
        m_selectionMotion->setID("survival-selection-motion-switch"_spr);
        // Forum record status above the ghost button row.
        m_forumStatus=add("",{kPad,38},200,caption,TextMuted,FontWeight::SemiBold);
        m_forumCooldown=add("",{kRight,38},110,caption,TextMuted,FontWeight::SemiBold,1.f);m_forumCooldown->setID("ranked-forum-refresh-cooldown"_spr);
        // Bottom row (.btn.ghost.xs, 10 px gaps): forum refresh, update
        // channel, logs (Debug builds), and CREDITS on the right.
        float x=kPad;
        auto place=[&](CCMenuItemSpriteExtra* item){auto const w=item->getContentSize().width;item->setPosition({x+w/2,18.5f});x+=w+5;return item;};
        m_refreshRecords=place(button(refreshButtonFace(false),menu_selector(RankedSettingsPopup::onRefreshRecords),{0,0}));
        m_refreshRecords->setID("ranked-forum-refresh-button"_spr);
        if(m_hooks.updateChannel)place(button(ghostFace(i18n::text("update-channel","UPDATE CHANNEL")),menu_selector(RankedSettingsPopup::onUpdateChannel),{0,0}))
            ->setID("ranked-settings-update-channel"_spr);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        place(button(ghostFace(i18n::text("debug-send-logs","SEND LOGS")),menu_selector(RankedSettingsPopup::onSendLogs),{0,0}))->setID("ranked-settings-send-logs"_spr);
#endif
        auto* credits=button(ghostFace("CREDITS"),menu_selector(RankedSettingsPopup::onCredits),{0,0});
        credits->setPosition({kRight-credits->getContentSize().width/2,18.5f});credits->setID("ranked-credits-button"_spr);
        RankedRuntime::get().fetchForumRecords();schedule(schedule_selector(RankedSettingsPopup::updateForumStatus),.15f);updateForumStatus(0);
    static replay::ReplaySeekSpeed seekSpeed(){return replay::replaySeekSpeedFromInt(static_cast<int>(Mod::get()->getSavedValue<int64_t>("replay-seek-speed",1)));}
    static std::string seekName(replay::ReplaySeekSpeed speed){
        switch(speed){case replay::ReplaySeekSpeed::Low:return i18n::text("replay-seek-low","Low");
            case replay::ReplaySeekSpeed::Fast:return i18n::text("replay-seek-fast","Fast");
            case replay::ReplaySeekSpeed::UltraFast:return i18n::text("replay-seek-ultra","Ultra fast");
            case replay::ReplaySeekSpeed::Normal:break;}
        return i18n::text("replay-seek-normal","Normal");
    }
    CCNode* seekOptionFace(std::size_t index,bool selected){
        return cellFace(seekName(replay::kReplaySeekSpeeds[index]),{m_seekWidths[index],19},selected,index==0,14);
    }
    void updateSeekOptions(){
        auto const current=seekSpeed();
        for(std::size_t i=0;i<m_seekOptions.size();++i)if(auto* item=m_seekOptions[i])
            item->setNormalImage(seekOptionFace(i,replay::kReplaySeekSpeeds[i]==current));
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
