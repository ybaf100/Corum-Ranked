#include "DebugMenuPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../DebugBotPopup.hpp"
#include "DebugSpectateRuntime.hpp"
#include "../RankedRuntime.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedInput.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/Popup.hpp>
using namespace geode::prelude;
namespace corum::ranked::debug {
namespace {
class DebugMenuPopup final : public Popup {
    bool m_closed = false, m_exited = false;
    bool init() override {
        if (!Popup::init(300,220)) return false;
        m_bgSprite->setVisible(false); setID("ranked-debug-menu"_spr);
        auto* panel = ui::surface({300,220},{150,110},ui::Cyan,ui::Surface,255,true);
        m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel(i18n::text("debug","DEBUG"),0.55f,{150,195},ui::Text,ui::FontWeight::Black));
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu,10);
        auto* account = GJAccountManager::get();
        auto const admin = account && account->m_accountID > 0 && std::string(account->m_username) == "hwanhee1";
        for (int i = 0; i < 3; ++i) {
            auto const label = i == 0 ? i18n::text("debug-bot","BOT MATCH")
                : i == 1 ? i18n::text("debug-spectate","SPECTATE") : std::string("Admin");
            auto* face = ui::surface({220,34},{},i == 2 ? ui::Red : ui::Blue,ui::SurfaceRaised,255,true);
            auto* caption = ui::menuLabel(label,0.38f,{110,17},ui::Text,ui::FontWeight::ExtraBold);
            ui::fitMenuLabelWidth(caption,202,0.38f,0.28f); face->addChild(caption,5);
            auto* button = CCMenuItemSpriteExtra::create(face,this,menu_selector(DebugMenuPopup::onSelect));
            button->setTag(i); button->setPosition({150,151 - i * 49.0f}); menu->addChild(button);
            bool enabled = i != 2 || admin;
#if !defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            if (i == 0) enabled = false;
#endif
#if !defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (i == 1) enabled = false;
#endif
            button->setEnabled(enabled); if (!enabled) ui::grayButtonVisual(face);
        }
        return true;
    }
    void onSelect(CCObject* sender) {
        if (m_closed) return;
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender); if (!item) return;
        auto const choice = item->getTag(); m_closed = true;
        auto const keep = Ref<DebugMenuPopup>(this);
        queueInMainThread([keep,choice] {
            if (!keep->isRunning()) return;
            keep->Popup::onClose(nullptr);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            if (choice == 0) corum::ranked::showDebugBotPasswordPopup();
#endif
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (choice == 1) showDebugSpectatePopup();
#endif
            if (choice == 2) showDebugAdminPopup();
        });
    }
public:
    static DebugMenuPopup* create() {
        auto* popup = new DebugMenuPopup;
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onExit() override {
        m_closed = true;
        if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
}
void showDebugMenuPopup() { if (auto* popup = DebugMenuPopup::create()) popup->show(); }
}
#endif
