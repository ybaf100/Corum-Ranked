#include "DebugSpectateRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../ui/RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
using namespace geode::prelude;
namespace {
class DebugSpectatePopup final : public Popup {
    TextInput* m_password = nullptr;
    TextInput* m_startPercent=nullptr;
    bool m_inputError=false;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_liveSetting = nullptr;
    CCMenuItemSpriteExtra* action(char const* name, SEL_MenuHandler selector) {
        auto* text = corum::ranked::ui::label(name, 0.35f, {}, corum::ranked::ui::Text,
            corum::ranked::ui::FontWeight::ExtraBold);
        return CCMenuItemSpriteExtra::create(text, this, selector);
    }
protected:
    bool init() override {
        if (!Popup::init(420.0f, 310.0f)) return false;
        setTitle("SERVER DEBUG SPECTATE");
        auto* map = corum::ranked::ui::label("VOICE / LiteN1ng / EASY DEMON", 0.33f, {210, 230}, corum::ranked::ui::Text);
        m_mainLayer->addChild(map);
        auto* note = corum::ranked::ui::label("Debug storage only - NO RANKED RESULTS", 0.25f, {210, 208}, corum::ranked::ui::TextMuted);
        m_mainLayer->addChild(note);
        m_password = TextInput::create(260, "Server debug password");
        m_password->setPasswordMode(true); m_password->setMaxCharCount(128); m_password->setPosition({210, 178});
        m_mainLayer->addChild(m_password);
        auto* percentLabel=corum::ranked::ui::label("TEST START % (0-100)",.25f,{150,135},corum::ranked::ui::Text);
        m_mainLayer->addChild(percentLabel);
        m_startPercent=TextInput::create(90,"0");m_startPercent->setString("0");m_startPercent->setFilter("0123456789.");
        m_startPercent->setMaxCharCount(6);m_startPercent->setPosition({300,135});m_mainLayer->addChild(m_startPercent);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_mainLayer->addChild(menu);
        auto* record = action("RECORD SPECTATE", menu_selector(DebugSpectatePopup::onRecord));
        record->setPosition({110, 95}); menu->addChild(record);
        auto* playback = action("TEST SPECTATE", menu_selector(DebugSpectatePopup::onTest));
        playback->setPosition({310, 95}); menu->addChild(playback);
        m_status = corum::ranked::ui::label("Requires ENABLE_DEBUG_SPECTATE server opt-in", 0.25f, {210, 60}, corum::ranked::ui::TextMuted);
        m_mainLayer->addChild(m_status);
        auto* toggle = action("SERVER LIVE SPECTATOR: LOADING...", menu_selector(DebugSpectatePopup::onToggle));
        toggle->setPosition({210, 32}); menu->addChild(toggle);
        m_liveSetting = typeinfo_cast<CCLabelBMFont*>(toggle->getNormalImage());
        corum::ranked::debug::DebugSpectateRuntime::get().refreshLiveSetting();
        schedule(schedule_selector(DebugSpectatePopup::refresh), 0.1f);
        return true;
    }
    void refresh(float) {
        auto& runtime = corum::ranked::debug::DebugSpectateRuntime::get();
        if (m_liveSetting) {
            auto const enabled = runtime.liveEnabled();
            m_liveSetting->setString(runtime.liveSettingBusy() ? "SERVER LIVE SPECTATOR: SYNCING..." :
                !enabled ? "SERVER LIVE SPECTATOR: UNKNOWN (REFRESH)" : *enabled ? "SERVER LIVE SPECTATOR: ON (DISABLE)" : "SERVER LIVE SPECTATOR: OFF (ENABLE)");
            m_liveSetting->setColor(enabled && *enabled ? corum::ranked::ui::Green : corum::ranked::ui::Gold);
            corum::ranked::ui::fitLabelWidth(m_liveSetting, 380, 0.35f, 0.20f);
        }
        auto const& text = runtime.status();
        if (!m_inputError && !text.empty()) { m_status->setString(text.c_str()); corum::ranked::ui::fitLabelWidth(m_status, 380, 0.25f, 0.14f); }
    }
    void run(bool test) {
        auto start=corum::ranked::debug::debugSpectateStartPercent(m_startPercent->getString());
        if(test && !start){m_inputError=true;m_status->setString("ENTER A START PERCENT FROM 0 TO 100");return;}
        m_inputError=false;
        auto password = m_password->getString(); m_password->setString("");
        corum::ranked::debug::DebugSpectateRuntime::get().start(test, std::move(password),test?start.value_or(0.f):0.f);
    }
    void onRecord(CCObject*) { run(false); }
    void onTest(CCObject*) { run(true); }
    void onToggle(CCObject*) {
        auto& runtime = corum::ranked::debug::DebugSpectateRuntime::get();
        if (!runtime.liveEnabled()) { runtime.refreshLiveSetting(); return; }
        auto password = m_password->getString(); m_password->setString("");
        runtime.toggleLiveSetting(std::move(password));
    }
    void onClose(CCObject* sender) override {
        corum::ranked::debug::DebugSpectateRuntime::get().cancelPopupPreparation(); Popup::onClose(sender);
    }
public:
    static DebugSpectatePopup* create() {
        auto* value = new DebugSpectatePopup;
        if (value->init()) { value->autorelease(); return value; }
        delete value; return nullptr;
    }
};
}
namespace corum::ranked::debug {
void showDebugSpectatePopup() { if (auto* popup = DebugSpectatePopup::create()) popup->show(); }
}
#endif
