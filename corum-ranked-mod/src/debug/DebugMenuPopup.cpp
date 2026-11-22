#include "DebugMenuPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../DebugBotPopup.hpp"
#include "DebugSpectateRuntime.hpp"
#include "../RankedRuntime.hpp"
#include "../SurvivalRuntime.hpp"
#include "../ui/RankedTranslations.hpp"
#include "../ui/RankedVisuals.hpp"
#include "../ui/RankedInput.hpp"
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/Popup.hpp>
#include <vector>
using namespace geode::prelude;
namespace corum::ranked::debug {
namespace {
class DebugMenuPopup final : public Popup {
    bool m_closed = false, m_exited = false;
    RankedMode m_mode=RankedMode::SoloRanked;
    bool init(RankedMode mode) {
        m_mode=mode;
        // BOT MATCH, SPECTATE, Admin, FIXED MAP BATTLE (the tag is the choice).
        // Survival has no spectate or fixed-map entry; the rest close up.
        std::vector<int> entries{0};
        if (mode != RankedMode::Survival) entries.push_back(1);
        entries.push_back(2);
        if (mode != RankedMode::Survival) entries.push_back(3);
        auto const count = static_cast<float>(entries.size());
        auto const height = 72.0f + count * 49.0f;
        if (!Popup::init(300,height)) return false;
        m_bgSprite->setVisible(false); setID("ranked-debug-menu"_spr);
        auto* panel = ui::surface({300,height},{150,height / 2},ui::Accent,ui::Surface,255,true);
        m_mainLayer->addChild(panel);
        panel->addChild(ui::menuLabel(i18n::text("debug","DEBUG"),0.55f,{150,height - 25},ui::Text,ui::FontWeight::Black));
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); panel->addChild(menu,10);
        auto* account = GJAccountManager::get();
        auto const admin = account && account->m_accountID > 0 && std::string(account->m_username) == "hwanhee1";
        auto const& view=RankedRuntime::get().view();
        for (std::size_t row = 0; row < entries.size(); ++row) {
            auto const i = entries[row];
            auto const label = i == 0 ? i18n::text("debug-bot","BOT MATCH")
                : i == 1 ? i18n::text("debug-spectate","SPECTATE")
                : i == 2 ? std::string("Admin") : i18n::text("debug-fixed-map","FIXED MAP BATTLE");
            auto* face = ui::surface({220,34},{},i == 2 ? ui::Red : i == 3 ? ui::Accent : ui::Steel,ui::SurfaceRaised,255,true);
            auto* caption = ui::menuLabel(label,0.38f,{110,17},ui::Text,ui::FontWeight::ExtraBold);
            ui::fitMenuLabelWidth(caption,202,0.38f,0.28f); face->addChild(caption,5);
            auto* button = CCMenuItemSpriteExtra::create(face,this,menu_selector(DebugMenuPopup::onSelect));
            button->setTag(i); button->setPosition({150,height - 69 - static_cast<float>(row) * 49.0f}); menu->addChild(button);
            bool enabled = i != 2 || admin;
            if(i==0)enabled=mode==RankedMode::Survival?(view.survivalEnabled||SurvivalRuntime::get().active()):view.soloEnabled;
            if(i==3)enabled=view.soloEnabled && view.stage==RuntimeStage::Ready;
#if !defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            if (i == 0 || i == 3) enabled = false;
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
        auto const keep = Ref<DebugMenuPopup>(this);auto const mode=m_mode;
        queueInMainThread([keep,choice,mode] {
            if (!keep->isRunning()) return;
            keep->Popup::onClose(nullptr);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            if (choice == 0) {if(mode==RankedMode::Survival)showSurvivalDebugPopup();else corum::ranked::showDebugBotPasswordPopup();}
            if (choice == 3) corum::ranked::showDebugFixedMapPasswordPopup();
#endif
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
            if (choice == 1) showDebugSpectatePopup();
#endif
            if (choice == 2) showDebugAdminPopup(mode);
        });
    }
public:
    static DebugMenuPopup* create(RankedMode mode=RankedMode::SoloRanked) {
        auto* popup = new DebugMenuPopup;
        if (popup->init(mode)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void onExit() override {
        m_closed = true;
        if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
}
void showDebugMenuPopup(RankedMode mode) { if (auto* popup = DebugMenuPopup::create(mode)) popup->show(); }
}
#endif
