#include "RankedRuntime.hpp"
#include "ui/RankedSurrenderPopup.hpp"
#include "spectator/SpectatorSceneBridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;

namespace {
using corum::ranked::RankedRuntime;
namespace ui = corum::ranked::ui;
namespace i18n = corum::ranked::i18n;
std::optional<WeakRef<PauseLayer>> activeRankedPause;
bool ownsRankedPlayerPause() {
    auto* layer = PlayLayer::get(); auto const& ranked = RankedRuntime::get();
    return layer && layer->m_level && !ranked.isPlacement() && !ranked.isSpectating() &&
        ranked.view().stage == corum::ranked::RuntimeStage::Matched &&
        ranked.isGameplayLevel(static_cast<int>(layer->m_level->m_levelID));
}
}

class $modify(CorumRankedPauseLayer, PauseLayer) {
public:
    struct Fields {
        std::string leaseId, matchId;
        int round = 0, deathmatch = 0;
        CCLabelBMFont* budget = nullptr;
        CCMenuItemSpriteExtra* surrender = nullptr;
        std::optional<WeakRef<ui::RankedSurrenderPopup>> popup;
        std::optional<WeakRef<PlayLayer>> owner;
        ~Fields() {
            if (popup) if (auto dialog = popup->lock()) dialog->dismiss();
            popup.reset();
            RankedRuntime::get().endMatchGameplayPause(leaseId);
        }
    };
    void customSetup() {
        PauseLayer::customSetup();
        if (!ownsRankedPlayerPause()) return;
        auto& ranked = RankedRuntime::get(); auto* layer = PlayLayer::get();
        m_fields->owner.emplace(layer);
        activeRankedPause.emplace(this);
        m_fields->leaseId = ranked.beginMatchGameplayPause(static_cast<int>(layer->m_level->m_levelID));
        log::info("[RankedPause] opened level={} round={} lease={}",
            static_cast<int>(layer->m_level->m_levelID), ranked.view().match.roundNumber,
            !m_fields->leaseId.empty());
        auto const& match = ranked.view().match;
        m_fields->matchId = match.matchId; m_fields->round = match.roundNumber; m_fields->deathmatch = match.deathmatchSequence;
        auto* center = typeinfo_cast<CCMenu*>(getChildByIDRecursive("center-button-menu"));
        if (!center) if (auto* play = getChildBySpriteFrameName(this, "GJ_playBtn2_001.png"))
            center = typeinfo_cast<CCMenu*>(play->getParent());
        auto* exit = typeinfo_cast<CCMenuItemSpriteExtra*>(getChildByIDRecursive("exit-button"));
        if (!exit && center) exit = typeinfo_cast<CCMenuItemSpriteExtra*>(getChildBySpriteFrameName(center, "GJ_menuBtn_001.png"));
        if (exit) {
            exit->setEnabled(false); exit->setOpacity(75);
            exit->setTarget(this, menu_selector(CorumRankedPauseLayer::onBlockedQuit));
        }
        auto const size = CCDirector::sharedDirector()->getWinSize();
        if (!center) {
            center = CCMenu::create(); center->setID("ranked-surrender-menu"_spr);
            center->setPosition({size.width / 2 - 130, size.height / 2 - 38}); addChild(center, 100);
        }
        auto* button = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        button->setScale(0.95f);
        m_fields->surrender = CCMenuItemSpriteExtra::create(button, this, menu_selector(CorumRankedPauseLayer::onSurrender));
        m_fields->surrender->setID("ranked-surrender-button"_spr);
        center->addChild(m_fields->surrender, -1); center->sortAllChildren(); center->updateLayout();
        auto* budgetPlate = CCLayerColor::create({0, 0, 0, 215}, std::min(260.0f, size.width * 0.48f), 29);
        budgetPlate->setPosition({8, size.height - 37}); addChild(budgetPlate, 1000);
        m_fields->budget = ui::label("", 0.31f, {8, 23}, ui::Gold, ui::FontWeight::ExtraBold);
        m_fields->budget->setAnchorPoint({0, 1});
        m_fields->budget->setID("ranked-pause-budget"_spr); budgetPlate->addChild(m_fields->budget, 1);
        schedule(schedule_selector(CorumRankedPauseLayer::watchBudget), 0.05f); watchBudget(0);
    }
    void watchBudget(float) {
        auto& ranked = RankedRuntime::get(); ranked.tick();
        if (m_fields->matchId.empty() || !getParent()) return;
        auto const remaining = ranked.matchPauseRemainingMs();
        auto const text = fmt::format("{}: {:.1f} / 30{}", i18n::text("match-pause", "MATCH PAUSE"),
            std::ceil(remaining / 100) / 10, i18n::isKorean() ? "초" : "s");
        m_fields->budget->setString(text.c_str());
        m_fields->surrender->setEnabled(!m_fields->leaseId.empty());
        if (ranked.gameplayConceded(m_fields->round, m_fields->deathmatch)) {
            auto play = geode::WeakRef<PlayLayer>(PlayLayer::get());
            resumeRankedPause(nullptr);
            if (auto scene = play.lock()) corum::ranked::requestConcededGameplayReturn(scene.data());
            return;
        }
        if ((!m_fields->leaseId.empty() && remaining <= 0) || ranked.view().match.matchId != m_fields->matchId ||
            ranked.view().match.roundNumber != m_fields->round || ranked.view().match.deathmatchSequence != m_fields->deathmatch ||
            ranked.view().stage != corum::ranked::RuntimeStage::Matched) resumeRankedPause(nullptr);
    }
    void retireRankedPause() {
        if (m_fields->popup) if (auto dialog = m_fields->popup->lock()) dialog->dismiss();
        m_fields->popup.reset();
        RankedRuntime::get().endMatchGameplayPause(m_fields->leaseId);
        m_fields->leaseId.clear();
        if (activeRankedPause) {
            auto active = activeRankedPause->lock();
            if (!active || active.data() == this) activeRankedPause.reset();
        }
        m_fields->owner.reset();
        unschedule(schedule_selector(CorumRankedPauseLayer::watchBudget));
    }
    void onResume(CCObject* sender) {
        // Native pause state/menu teardown must not wait for a retained
        // surrender dialog or Fields destructor in a later round.
        retireRankedPause(); PauseLayer::onResume(sender);
    }
    void resumeRankedPause(CCObject* sender) {
        onResume(sender);
    }
    void onBlockedQuit(CCObject*) {}
    void onQuit(CCObject* sender) {
        // Server-driven return uses PlayLayer::onQuit, not this manual route.
        if (ownsRankedPlayerPause()) return;
        PauseLayer::onQuit(sender);
    }
    void onSurrender(CCObject*) {
        if (m_fields->leaseId.empty()) return;
        if (m_fields->popup) {
            auto dialog = m_fields->popup->lock();
            if (dialog && dialog->getParent()) return;
            m_fields->popup.reset();
        }
        log::info("[RankedSurrender] pause button round={}", m_fields->round);
        auto* popup = ui::RankedSurrenderPopup::create(this, m_fields->matchId, m_fields->round, m_fields->deathmatch);
        if (popup) { m_fields->popup.emplace(popup); popup->show(); }
    }
};

namespace corum::ranked {
bool resumeRankedPauseBeforeQuit(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return true;
    auto pause = activeRankedPause ? activeRankedPause->lock() : Ref<PauseLayer>();
    if (pause) {
        auto* rankedPause = static_cast<CorumRankedPauseLayer*>(pause.data());
        auto owner = rankedPause->m_fields->owner
            ? rankedPause->m_fields->owner->lock() : Ref<PlayLayer>();
        if (owner && owner.data() == layer && pause->getParent()) {
            // The presentation heartbeat may observe an ACK before watchBudget.
            // Native Resume must complete even on that ordering, before native Quit.
            rankedPause->onResume(nullptr);
            return !layer->m_isPaused;
        }
    }
    // Unowned/ordinary/Placement Pause keeps its existing vanilla quit path.
    return true;
}
} // namespace corum::ranked
