#include "DebugSpectateRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../ui/RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
using namespace geode::prelude;
namespace {
class DebugSpectatePopup final : public Popup {
    TextInput* m_password = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* action(char const* name, SEL_MenuHandler selector) {
        auto* text = corum::ranked::ui::label(name, 0.35f, {}, corum::ranked::ui::Text,
            corum::ranked::ui::FontWeight::ExtraBold);
        return CCMenuItemSpriteExtra::create(text, this, selector);
    }
protected:
    bool init() override {
        if (!Popup::init(420.0f, 270.0f)) return false;
        setTitle("SERVER DEBUG SPECTATE");
        auto* map = corum::ranked::ui::label("VOICE / LiteN1ng / EASY DEMON", 0.33f, {210, 212}, corum::ranked::ui::Text);
        m_mainLayer->addChild(map);
        auto* note = corum::ranked::ui::label("Debug storage only - NO RANKED RESULTS", 0.25f, {210, 187}, corum::ranked::ui::TextMuted);
        m_mainLayer->addChild(note);
        m_password = TextInput::create(260, "Server debug password");
        m_password->setPasswordMode(true); m_password->setMaxCharCount(128); m_password->setPosition({210, 150});
        m_mainLayer->addChild(m_password);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_mainLayer->addChild(menu);
        auto* record = action("RECORD SPECTATE", menu_selector(DebugSpectatePopup::onRecord));
        record->setPosition({110, 105}); menu->addChild(record);
        auto* playback = action("TEST SPECTATE", menu_selector(DebugSpectatePopup::onTest));
        playback->setPosition({310, 105}); menu->addChild(playback);
        m_status = corum::ranked::ui::label("Requires ENABLE_DEBUG_SPECTATE server opt-in", 0.25f, {210, 65}, corum::ranked::ui::TextMuted);
        m_mainLayer->addChild(m_status);
        schedule(schedule_selector(DebugSpectatePopup::refresh), 0.1f);
        return true;
    }
    void refresh(float) {
        auto const& text = corum::ranked::debug::DebugSpectateRuntime::get().status();
        if (!text.empty()) { m_status->setString(text.c_str()); m_status->limitLabelWidth(380, 0.25f, 0.14f); }
    }
    void run(bool test) {
        auto password = m_password->getString(); m_password->setString("");
        corum::ranked::debug::DebugSpectateRuntime::get().start(test, std::move(password));
    }
    void onRecord(CCObject*) { run(false); }
    void onTest(CCObject*) { run(true); }
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
