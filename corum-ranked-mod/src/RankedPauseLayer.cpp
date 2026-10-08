#include "RankedRuntime.hpp"
#include "ui/RankedSurrenderPopup.hpp"
#include "spectator/SpectatorSceneBridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/AppDelegate.hpp>
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
        bool concessionQuit = false, nativeQuitInProgress = false, nativeQuitReachedPlay = false;
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
            // Keep the native Pause attached until its own Quit boundary.
            // Resume/removal followed by a separate PlayLayer quit is not the
            // native Pause quit lifecycle. Both watchers request the same
            // deferred handoff; neither tears down native UI in a callback.
            auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
            if (owner) corum::ranked::requestConcededGameplayReturn(owner.data());
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
        unschedule(schedule_selector(CorumRankedPauseLayer::watchBudget));
    }
    void releaseRankedPauseOwnership() {
        if (activeRankedPause) {
            auto active = activeRankedPause->lock();
            if (!active || active.data() == this) activeRankedPause.reset();
        }
        m_fields->owner.reset();
    }
    void onResume(CCObject* sender) {
        // Native pause state/menu teardown must not wait for a retained
        // surrender dialog or Fields destructor in a later round.
        // Vanilla Resume removes the Pause from its parent. Keep it alive
        // through our post-call ownership cleanup even for native menu callers.
        [[maybe_unused]] Ref<PauseLayer> keepAlive(this);
        auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
        retireRankedPause(); PauseLayer::onResume(sender);
        // Keep the exact ownership if a native/other-hook Resume refuses. A
        // second quit must not bypass the refusal just because the lease ended.
        if (!owner || !owner->m_isPaused) releaseRankedPauseOwnership();
    }
    void resumeRankedPause(CCObject* sender) {
        PauseLayer::onResume(sender);
    }
    void onBlockedQuit(CCObject*) {}
    void onQuit(CCObject* sender) {
        auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
        auto& ranked = RankedRuntime::get();
        auto const authenticatedConcession = m_fields->concessionQuit && owner &&
            owner.data() == PlayLayer::get() && ranked.view().match.matchId == m_fields->matchId &&
            ranked.gameplayConceded(m_fields->round, m_fields->deathmatch);
        if (ownsRankedPlayerPause() && !authenticatedConcession) return;
        // The nested PlayLayer quit may resume/remove this Pause before the
        // vanilla Pause quit caller finishes returning.
        [[maybe_unused]] Ref<PauseLayer> keepAlive(this);
        if (authenticatedConcession) {
            retireRankedPause();
            m_fields->nativeQuitInProgress = true;
            m_fields->nativeQuitReachedPlay = false;
        }
        PauseLayer::onQuit(sender);
        if (authenticatedConcession) {
            m_fields->nativeQuitInProgress = false;
            m_fields->concessionQuit = false;
            geode::log::info("[RankedPause] native concession quit reachedPlay={} paused={} appPaused={}",
                m_fields->nativeQuitReachedPlay, owner->m_isPaused, AppDelegate::get()->m_gamePaused);
            if (m_fields->nativeQuitReachedPlay) releaseRankedPauseOwnership();
        }
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
bool quitRankedPauseForConcession(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return false;
    auto pause = activeRankedPause ? activeRankedPause->lock() : Ref<PauseLayer>();
    if (!pause || !pause->getParent()) return false;
    auto* rankedPause = static_cast<CorumRankedPauseLayer*>(pause.data());
    auto owner = rankedPause->m_fields->owner ? rankedPause->m_fields->owner->lock() : Ref<PlayLayer>();
    auto const& ranked = RankedRuntime::get();
    if (!owner || owner.data() != layer || ranked.view().match.matchId != rankedPause->m_fields->matchId ||
        !ranked.gameplayConceded(rankedPause->m_fields->round, rankedPause->m_fields->deathmatch)) return false;
    rankedPause->m_fields->concessionQuit = true;
    // Public binding entry, with the installed native Pause quit hooks. The
    // Corum hook's qualified call below it advances to next-original once.
    pause->onQuit(nullptr);
    return true;
}

bool resumeRankedPauseBeforeQuit(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return true;
    auto pause = activeRankedPause ? activeRankedPause->lock() : Ref<PauseLayer>();
    if (pause) {
        auto* rankedPause = static_cast<CorumRankedPauseLayer*>(pause.data());
        auto owner = rankedPause->m_fields->owner
            ? rankedPause->m_fields->owner->lock() : Ref<PlayLayer>();
        if (owner && owner.data() == layer) {
            if (rankedPause->m_fields->nativeQuitInProgress) {
                // Native Pause Quit already owns Resume -> PlayLayer Quit.
                // Re-entering onResume here would dismantle its caller while
                // that native Quit stack is still active.
                if (layer->m_isPaused) return false;
                rankedPause->m_fields->nativeQuitReachedPlay = true;
                return true;
            }
            if (!pause->getParent()) {
                if (layer->m_isPaused) return false;
                rankedPause->retireRankedPause();
                rankedPause->releaseRankedPauseOwnership();
                return true;
            }
            // The presentation heartbeat may observe an ACK before watchBudget.
            // Native Resume must complete even on that ordering, before native Quit.
            // Enter through the binding so installed Resume hooks run once in
            // their normal Tulip chain, rather than directly calling our detour.
            pause->onResume(nullptr);
            geode::log::info("[RankedPause] quit resume result paused={}", layer->m_isPaused);
            return !layer->m_isPaused;
        }
    }
    // Unowned/ordinary/Placement Pause keeps its existing vanilla quit path.
    return true;
}
} // namespace corum::ranked
