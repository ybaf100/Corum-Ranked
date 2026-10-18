#include "RankedRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "SurvivalSceneBridge.hpp"
#include "ui/RankedConfirmPopup.hpp"
#include "ui/SurvivalPauseActionInput.hpp"
#include "ui/RankedSurrenderPopup.hpp"
#include "ui/RankedExitConfirmation.hpp"
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
using corum::ranked::SurvivalRuntime;
namespace ui = corum::ranked::ui;
namespace i18n = corum::ranked::i18n;
std::optional<WeakRef<PauseLayer>> activeRankedPause;
void dismissNativeRankedExitConfirmation(PauseLayer* pause) {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    auto* delegate = static_cast<FLAlertLayerProtocol*>(pause);
    std::vector<Ref<CCNode>> pending;
    std::vector<Ref<FLAlertLayer>> dialogs;
    pending.emplace_back(scene);
    while (!pending.empty()) {
        auto node = std::move(pending.back()); pending.pop_back();
        if (auto* alert = typeinfo_cast<FLAlertLayer*>(node.data()); alert && alert->m_alertProtocol == delegate)
            dialogs.emplace_back(alert);
        for (auto* child : node->getChildrenExt()) pending.emplace_back(child);
    }
    for (auto const& alert : dialogs) {
        if (!alert->getParent() || alert->m_alertProtocol != delegate) continue;
        if (alert->m_forcePrioRegistered) {
            CCTouchDispatcher::get()->unregisterForcePrio(alert.data());
            alert->m_forcePrioRegistered = false;
        }
        // Cancel the original GD Confirm Exit while its exact Pause is still
        // alive. A budget timeout must not leave its raw delegate behind.
        alert->onBtn1(nullptr);
    }
}
bool ownsRankedPlayerPause() {
    auto* layer = PlayLayer::get(); auto const& ranked = RankedRuntime::get();
    return layer && layer->m_level && !ranked.isPlacement() && !ranked.isSpectating() &&
        ranked.view().stage == corum::ranked::RuntimeStage::Matched &&
        ranked.isGameplayLevel(static_cast<int>(layer->m_level->m_levelID));
}
bool ownsPlacementPlayerPause() {
    auto* layer = PlayLayer::get(); auto const& ranked = RankedRuntime::get();
    auto const& state = ranked.view().placement.state;
    return layer && layer->m_level && !layer->m_isPracticeMode && !layer->m_isTestMode &&
        ranked.isPlacement() && !ranked.isSpectating() &&
        ranked.view().stage == corum::ranked::RuntimeStage::Matched &&
        (state == "MAP_PLAYING" || state == "MAP_SETTLING") &&
        ranked.isGameplayLevel(static_cast<int>(layer->m_level->m_levelID));
}
bool ownsSurvivalPlayerPause() {
    return corum::ranked::isOfficialSurvivalPlayLayer(PlayLayer::get());
}
bool ownsManagedRankedPause() { return ownsRankedPlayerPause() || ownsPlacementPlayerPause() || ownsSurvivalPlayerPause(); }
}

class $modify(CorumRankedPauseLayer, PauseLayer) {
public:
    struct Fields {
        std::string leaseId, matchId;
        int round = 0, deathmatch = 0;
        int placementMapIndex = 0;
        bool placement = false;
        bool survival = false;
        std::string survivalRoundKey;
        int survivalMapGeneration = 0;
        CCLabelBMFont* budget = nullptr;
        CCLabelBMFont* survivalStatus = nullptr;
        CCMenuItemSpriteExtra* surrender = nullptr;
        CCMenu* survivalActions = nullptr;
        ui::SurvivalPauseActionInput* survivalInput = nullptr;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        CCMenuItemSpriteExtra* forceClear = nullptr;
        CCMenu* forceClearMenu = nullptr;
#endif
        std::optional<WeakRef<ui::RankedSurrenderPopup>> popup;
        std::optional<WeakRef<ui::RankedExitConfirmation>> exitConfirmation;
        std::optional<WeakRef<ui::RankedConfirmPopup>> survivalConfirmation;
        std::optional<WeakRef<PlayLayer>> owner;
        bool concessionQuit = false, nativeQuitInProgress = false, nativeQuitReachedPlay = false;
        bool timeoutSurrenderRequested = false;
        ~Fields() {
            if(survivalConfirmation)if(auto dialog=survivalConfirmation->lock())dialog->cancel();
            if (popup) if (auto dialog = popup->lock()) dialog->dismiss();
            if (exitConfirmation) if (auto dialog = exitConfirmation->lock()) dialog->dismiss();
            popup.reset();
            RankedRuntime::get().endMatchGameplayPause(leaseId);
        }
    };
    void customSetup() {
        PauseLayer::customSetup();
        if (!ownsManagedRankedPause()) return;
        auto& ranked = RankedRuntime::get(); auto* layer = PlayLayer::get();
        m_fields->owner.emplace(layer);
        activeRankedPause.emplace(this);
        m_fields->survival=ownsSurvivalPlayerPause();
        m_fields->placement = !m_fields->survival && ranked.isPlacement();
        m_fields->placementMapIndex = m_fields->placement ? ranked.view().placement.currentMapIndex : 0;
        if (!m_fields->placement && !m_fields->survival)
            m_fields->leaseId = ranked.beginMatchGameplayPause(static_cast<int>(layer->m_level->m_levelID));
        log::info("[RankedPause] opened level={} round={} lease={}",
            static_cast<int>(layer->m_level->m_levelID), ranked.view().match.roundNumber,
            !m_fields->leaseId.empty());
        auto const& match = ranked.view().match;
        m_fields->matchId = match.matchId; m_fields->round = match.roundNumber; m_fields->deathmatch = match.deathmatchSequence;
        if(m_fields->survival){auto const& survival=corum::ranked::SurvivalRuntime::get().view();
            m_fields->matchId=survival.id;m_fields->survivalRoundKey=survival.roundKey;m_fields->survivalMapGeneration=survival.mapGeneration;}
        auto* center = typeinfo_cast<CCMenu*>(getChildByIDRecursive("center-button-menu"));
        if (!center) for(auto* child:getChildrenExt()){
            auto* menu=typeinfo_cast<CCMenu*>(child);
            // getChildBySpriteFrameName is not recursive. Search menu items,
            // not the Pause's direct children, before Node IDs has run.
            if(menu && getChildBySpriteFrameName(menu,"GJ_playBtn2_001.png")){center=menu;break;}
        }
        auto* exit = typeinfo_cast<CCMenuItemSpriteExtra*>(getChildByIDRecursive("exit-button"));
        if (!exit && center) exit = typeinfo_cast<CCMenuItemSpriteExtra*>(getChildBySpriteFrameName(center, "GJ_menuBtn_001.png"));
        if (exit && !m_fields->placement && !m_fields->survival) {
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
        CCNode* surrenderFace=button;
        if(m_fields->survival){
            surrenderFace=ui::surface({96,36},{},ui::Red,ui::SurfaceRaised,255,true);
            auto* caption=ui::menuLabel(i18n::text("surrender","SURRENDER"),.26f,{48,18},ui::Red,ui::FontWeight::ExtraBold);
            ui::fitFullMenuLabel(caption,84,24,.26f);surrenderFace->addChild(caption,5);
        }
        m_fields->surrender = CCMenuItemSpriteExtra::create(surrenderFace, this, menu_selector(CorumRankedPauseLayer::onSurrender));
        m_fields->surrender->setID("ranked-surrender-button"_spr);
        if(m_fields->survival){
            // beta.72's native menu owns the working Pause input route. Keep
            // the caption INSIDE its item's hit rectangle, not below an X in
            // a separate bottom-corner menu. Node IDs is optional.
            m_fields->survivalActions=center;
            if(!center->getLayout()){
                center->setContentSize({std::min(size.width-32,410.f),74});
                center->setPosition({size.width/2,size.height/2});
                center->setLayout(RowLayout::create()->setGap(14)->setAutoScale(true)->setAxisAlignment(AxisAlignment::Center));
            }
        }
        center->addChild(m_fields->surrender,-1);center->sortAllChildren();center->updateLayout();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        setupSurvivalForceClear(size);
#endif
        if(m_fields->survival){
            log::info("[SurvivalPauseInput] permissions state={} surrender={} assets={} entered={} eliminated={} cleared={}",
                SurvivalRuntime::get().view().state,SurvivalRuntime::get().canSurrender(),SurvivalRuntime::get().view().ownAssetsReady,
                SurvivalRuntime::get().view().ownEntered,SurvivalRuntime::get().view().ownEliminated,SurvivalRuntime::get().normalRoundCleared());
            auto weak=WeakRef(this);
            auto* input=ui::SurvivalPauseActionInput::create(this,layer,[weak]{
                auto pause=weak.lock();if(!pause || !pause->getParent())return false;
                auto const& fields=*pause->m_fields.self();auto const& runtime=SurvivalRuntime::get();auto const& view=runtime.view();
                return fields.survival && !fields.survivalConfirmation && !runtime.surrenderPending() &&
                    fields.matchId==view.id && fields.survivalRoundKey==view.roundKey && fields.survivalMapGeneration==view.mapGeneration;
            });
            if(input){
                input->bind(m_fields->surrender,[weak]{if(auto pause=weak.lock())pause->onSurrender(nullptr);});
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
                input->bind(m_fields->forceClear,[weak]{if(auto pause=weak.lock())pause->onForceClear(nullptr);});
#endif
                m_fields->survivalInput=input;addChild(input,1200);
                log::info("[SurvivalPauseInput] attached observer to Pause, actions registered");
            }
        }
        if (m_fields->placement) {
            auto* caption = ui::label(i18n::text("placement-skip-map", "SKIP MAP"), 0.22f,
                {button->getContentSize().width / 2, -10}, ui::Gold, ui::FontWeight::ExtraBold);
            button->addChild(caption);
            schedule(schedule_selector(CorumRankedPauseLayer::watchBudget), 0.05f); watchBudget(0);
            return;
        }
        auto const plateWidth = std::min(300.0f, size.width * 0.56f);
        auto* budgetPlate = CCLayerColor::create({0, 0, 0, 215}, plateWidth, 48);
        budgetPlate->setPosition({8, size.height - 56}); addChild(budgetPlate, 1000);
        m_fields->budget = ui::label("", 0.31f, {8, 42}, ui::Gold, ui::FontWeight::ExtraBold);
        m_fields->budget->setAnchorPoint({0, 1});
        m_fields->budget->setID("ranked-pause-budget"_spr); budgetPlate->addChild(m_fields->budget, 1);
        auto* warning = ui::label(m_fields->survival?i18n::text("survival-pause-clock","The server round timer continues while paused."):
            i18n::text("pause-timeout-warning", "At 0s, you forfeit the entire match."),
            0.26f, {8, 18}, ui::TextMuted, ui::FontWeight::SemiBold);
        warning->setAnchorPoint({0, 1});
        ui::fitLabelWidth(warning, plateWidth - 16, 0.26f, 0.22f);
        warning->setID("ranked-pause-timeout-warning"_spr); budgetPlate->addChild(warning, 1);
        if(m_fields->survival)m_fields->survivalStatus=warning;
        schedule(schedule_selector(CorumRankedPauseLayer::watchBudget), 0.05f); watchBudget(0);
    }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void setupSurvivalForceClear(CCSize size) {
        auto const& view=SurvivalRuntime::get().view();
        if(!m_fields->forceClear && m_fields->survival && view.debug && !view.online && view.phase!="TIEBREAK"){
            auto* face=ui::surface({112,30},{},ui::Gold,ui::SurfaceRaised,255,true);
            face->addChild(ui::menuLabel(i18n::text("survival-force-clear","FORCE CLEAR"),.28f,{56,15},ui::Gold,ui::FontWeight::ExtraBold),5);
            m_fields->forceClear=CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumRankedPauseLayer::onForceClear));
            m_fields->forceClear->setID("survival-force-clear-button"_spr);
            // A text action must not enter the native icon row's auto-scaling
            // layout/hit regions. Keep it in its own non-overlapping menu.
            m_fields->forceClearMenu=CCMenu::create();
            m_fields->forceClearMenu->setID("survival-force-clear-menu"_spr);
            m_fields->forceClearMenu->setPosition(CCPointZero);
            m_fields->forceClear->setPosition({size.width-76,size.height-38});
            m_fields->forceClearMenu->addChild(m_fields->forceClear);
            addChild(m_fields->forceClearMenu,1100);
        }
    }
#endif
    void synchronizeSurvivalPauseInput() {
        if(m_fields->survivalConfirmation){
            if(auto modal=m_fields->survivalConfirmation->lock();modal && modal->getParent())return;
        }
        auto* dispatcher=CCDirector::sharedDirector()->getTouchDispatcher();
        auto* pauseHandler=dispatcher->findHandler(this);
        auto* nativeHandler=m_fields->survivalActions?dispatcher->findHandler(m_fields->survivalActions):nullptr;
        for(auto* menu:{m_fields->survivalActions
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            ,m_fields->forceClearMenu
#endif
        }){
            auto* own=menu?dispatcher->findHandler(menu):nullptr;
            if(!own)continue;
            auto priority=own->m_nPriority;
            if(pauseHandler && pauseHandler->m_nPriority>std::numeric_limits<int>::min())priority=std::min(priority,pauseHandler->m_nPriority-1);
            // Pause itself need not have a touch handler on every platform.
            // Force Clear must also inherit the actual native menu's priority.
            if(nativeHandler)priority=std::min(priority,nativeHandler->m_nPriority);
            if(priority<own->m_nPriority)dispatcher->setPriority(priority,menu);
        }
    }
    void watchBudget(float) {
        if(m_fields->survival){
            auto& runtime=corum::ranked::SurvivalRuntime::get();runtime.tick();auto const& view=runtime.view();
            if(m_fields->survivalConfirmation){
                auto dialog=m_fields->survivalConfirmation->lock();
                if(!dialog || !dialog->getParent())m_fields->survivalConfirmation.reset();
            }
            synchronizeSurvivalPauseInput();
            auto const remaining=runtime.roundRemainingMs();
            auto const round=view.phase=="ROUND_1"?"R1":view.phase=="ROUND_2"?"R2":view.phase=="ROUND_3"?"R3":
                view.phase=="FINAL"?"FINAL":view.phase=="TIEBREAK"?"DEATH MATCH":"ROUND";
            auto const seconds=(remaining.value_or(0)+999)/1000;
            m_fields->budget->setString((remaining?fmt::format("{} · {:02}:{:02}",round,seconds/60,seconds%60):fmt::format("{} · --:--",round)).c_str());
            m_fields->surrender->setEnabled(runtime.canSurrender() && !m_fields->survivalConfirmation);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            setupSurvivalForceClear(CCDirector::sharedDirector()->getWinSize());
            if(m_fields->forceClear){m_fields->forceClear->setVisible(view.debug && !view.online && view.phase!="TIEBREAK" && !runtime.normalRoundCleared() && !view.ownEliminated);m_fields->forceClear->setEnabled(runtime.canForceClear() && !runtime.forceClearPending() && !m_fields->survivalConfirmation);}
            if(runtime.forceClearPending())m_fields->surrender->setEnabled(false);
#endif
            if(m_fields->survivalStatus){
                auto const status=runtime.surrenderPending()?i18n::text("survival-surrender-pending","Surrender submitted · waiting for server"):!view.error.empty()?i18n::text(view.error,view.error):
                    i18n::text("survival-pause-clock","The server round timer continues while paused.");
                m_fields->survivalStatus->setString(status.c_str());
                ui::fitLabelWidth(m_fields->survivalStatus,std::min(300.0f,CCDirector::sharedDirector()->getWinSize().width*.56f)-16,.26f,.18f);
            }
            if(view.id!=m_fields->matchId || view.roundKey!=m_fields->survivalRoundKey || view.mapGeneration!=m_fields->survivalMapGeneration ||
                runtime.normalRoundCleared() || view.ownEliminated || view.state=="ROUND_RESULT" || view.state=="FINAL_RESULT" || view.state=="CANCELLED") {
                if(m_fields->owner)if(auto owner=m_fields->owner->lock())
                    corum::ranked::requestSurvivalGameplayReturn(owner.data());
            }return;
        }
        auto& ranked = RankedRuntime::get(); ranked.tick();
        if (m_fields->matchId.empty() || !getParent()) return;
        if (m_fields->placement) {
            if (hasAcknowledgedGameplayExit()) {
                auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
                if (owner) corum::ranked::requestConcededGameplayReturn(owner.data());
            } else if (!ranked.isPlacement() || ranked.view().placement.runId != m_fields->matchId ||
                ranked.view().placement.currentStageIndex != m_fields->round ||
                ranked.view().placement.currentMapIndex != m_fields->placementMapIndex) {
                resumeRankedPause(nullptr);
            }
            return;
        }
        auto const remaining = ranked.matchPauseRemainingMs();
        auto const text = fmt::format("{}: {:.1f} / 30{}", i18n::text("match-pause", "MATCH PAUSE"),
            std::ceil(remaining / 100) / 10, i18n::isKorean() ? "초" : "s");
        m_fields->budget->setString(text.c_str());
        m_fields->budget->setColor(remaining <= 5000 ? ui::Red : ui::Gold);
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
        if (ranked.view().match.matchId != m_fields->matchId ||
            ranked.view().match.roundNumber != m_fields->round || ranked.view().match.deathmatchSequence != m_fields->deathmatch ||
            ranked.view().stage != corum::ranked::RuntimeStage::Matched) {
            resumeRankedPause(nullptr); return;
        }
        if (!m_fields->leaseId.empty() && remaining <= 0) {
            // Keep the exact native Pause attached until the authenticated
            // concession ACK/poll invokes the existing native Quit handoff.
            if (!m_fields->timeoutSurrenderRequested && !ranked.surrenderPending()) {
                m_fields->timeoutSurrenderRequested = true;
                ranked.surrenderCurrentMatch(true);
                geode::log::info("[RankedPause] budget exhausted; full match surrender requested round={}", m_fields->round);
            }
        }
    }
    void retireRankedPause() {
        if(m_fields->survivalInput)m_fields->survivalInput->suspend();
        if(m_fields->survivalConfirmation){if(auto dialog=m_fields->survivalConfirmation->lock())dialog->cancel();m_fields->survivalConfirmation.reset();}
        if (m_fields->owner) dismissNativeRankedExitConfirmation(this);
        if (m_fields->exitConfirmation) if (auto dialog = m_fields->exitConfirmation->lock()) dialog->dismiss();
        m_fields->exitConfirmation.reset();
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
        if(m_fields->survival && SurvivalRuntime::get().surrenderPending())return;
        // Native pause state/menu teardown must not wait for a retained
        // surrender dialog or Fields destructor in a later round.
        // Vanilla Resume removes the Pause from its parent. Keep it alive
        // through our post-call ownership cleanup even for native menu callers.
        [[maybe_unused]] Ref<PauseLayer> keepAlive(this);
        auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
        retireRankedPause(); PauseLayer::onResume(sender);
        // Keep the exact ownership if a native/other-hook Resume refuses. A
        // second quit must not bypass the refusal just because the lease ended.
        // Keep the exact owner across our Resume -> native Quit transaction,
        // including after Resume detaches the window. Both native pause states
        // must be released before an independent PlayLayer quit can proceed.
        if (!m_fields->nativeQuitInProgress &&
            (!owner || (!owner->m_isPaused && !AppDelegate::get()->m_gamePaused)))
            releaseRankedPauseOwnership();
    }
    void resumeRankedPause(CCObject* sender) {
        PauseLayer::onResume(sender);
    }
    void onBlockedQuit(CCObject*) {}
    bool hasAcknowledgedGameplayExit() {
        auto const& ranked = RankedRuntime::get();
        return ranked.view().match.matchId == m_fields->matchId && (m_fields->placement
            ? ranked.placementMapSkipped(m_fields->matchId, m_fields->round, m_fields->placementMapIndex)
            : ranked.gameplayConceded(m_fields->round, m_fields->deathmatch));
    }
    void onQuit(CCObject* sender) {
        // The Windows confirmation crash reaches this hook's Fields lookup.
        // Do not trust the native callback's incoming receiver for Fields;
        // the tracked WeakRef is the only Ranked Pause authority.
        auto pause = activeRankedPause ? activeRankedPause->lock() : Ref<PauseLayer>();
        if (!pause) {
            if (!ownsManagedRankedPause()) PauseLayer::onQuit(sender);
            return;
        }
        auto* rankedPause = static_cast<CorumRankedPauseLayer*>(pause.data());
        auto& fields = rankedPause->m_fields;
        auto owner = fields->owner ? fields->owner->lock() : Ref<PlayLayer>();
        if(fields->survival){
            if(SurvivalRuntime::get().surrenderPending() || fields->nativeQuitInProgress || !owner || owner.data()!=PlayLayer::get())return;
            [[maybe_unused]] Ref<PauseLayer> keepAlive(pause.data());
            rankedPause->retireRankedPause();fields->nativeQuitInProgress=true;
            if(owner && owner.data()==PlayLayer::get())owner->resume();
            PauseLayer::onQuit(sender);fields->nativeQuitInProgress=false;rankedPause->releaseRankedPauseOwnership();return;
        }
        auto& ranked = RankedRuntime::get();
        auto const authenticatedConcession = fields->concessionQuit && owner &&
            owner.data() == PlayLayer::get() && rankedPause->hasAcknowledgedGameplayExit();
        if (!authenticatedConcession && !ownsManagedRankedPause()) { PauseLayer::onQuit(sender); return; }
        if (!owner || owner.data() != PlayLayer::get() || !pause->getParent() ||
            ranked.view().match.matchId != fields->matchId) return;
        if (!authenticatedConcession && !fields->placement) { rankedPause->confirmRankedExit(); return; }
        // The nested PlayLayer quit may resume/remove this Pause before the
        // vanilla Pause quit caller finishes returning.
        [[maybe_unused]] Ref<PauseLayer> keepAlive(pause.data());
        if (authenticatedConcession || fields->placement) {
            rankedPause->retireRankedPause();
            fields->nativeQuitInProgress = true;
            fields->nativeQuitReachedPlay = false;
            // Restore this exact PlayLayer through its native Resume binding.
            // Keep Pause attached for its original Quit handler to close; never
            // invoke that handler on a window we already removed via onResume.
            if (authenticatedConcession) owner->resume();
            geode::log::info("[RankedPause] concession resume paused={} appPaused={} attached={}",
                owner->m_isPaused, AppDelegate::get()->m_gamePaused, pause->getParent() != nullptr);
        }
        // A Resume hook can depart this scene. Never let native Pause Quit
        // resolve a different PlayLayer after that callback has returned.
        if (!authenticatedConcession || owner.data() == PlayLayer::get())
            PauseLayer::onQuit(sender);
        if (authenticatedConcession || fields->placement) {
            fields->nativeQuitInProgress = false;
            fields->concessionQuit = false;
            geode::log::info("[RankedPause] native concession quit reachedPlay={} paused={} appPaused={}",
                fields->nativeQuitReachedPlay, owner->m_isPaused, AppDelegate::get()->m_gamePaused);
            if (fields->nativeQuitReachedPlay) rankedPause->releaseRankedPauseOwnership();
        }
    }
    void confirmRankedExit() {
        if (m_fields->leaseId.empty() || RankedRuntime::get().surrenderPending()) return;
        if (m_fields->exitConfirmation) {
            if (auto dialog = m_fields->exitConfirmation->lock(); dialog && dialog->getParent()) return;
            m_fields->exitConfirmation.reset();
        }
        if (m_fields->popup) if (auto dialog = m_fields->popup->lock()) dialog->dismiss();
        m_fields->popup.reset();
        auto owner = m_fields->owner ? m_fields->owner->lock() : Ref<PlayLayer>();
        if (!owner || owner.data() != PlayLayer::get()) return;
        auto* dialog = ui::RankedExitConfirmation::create(this, owner.data());
        if (dialog) { m_fields->exitConfirmation.emplace(dialog); addChild(dialog); dialog->show(); }
    }
    void onSurrender(CCObject*) {
        if(m_fields->survival){
            auto& runtime=corum::ranked::SurvivalRuntime::get();
            if(m_fields->survivalConfirmation){
                auto dialog=m_fields->survivalConfirmation->lock();
                if(dialog && dialog->getParent())return;
                m_fields->survivalConfirmation.reset();
            }
            if(!runtime.canSurrender())return;
            log::info("[SurvivalPause] surrender confirmation match={} round={}",m_fields->matchId,m_fields->survivalRoundKey);
            auto const id=m_fields->matchId,key=m_fields->survivalRoundKey;auto const generation=m_fields->survivalMapGeneration;
            auto* popup=ui::RankedConfirmPopup::create(i18n::text("surrender-confirm","CONFIRM SURRENDER"),
                runtime.surrenderMessage(),
                [weak=WeakRef(this),id,key,generation](bool accepted){
                    auto owner=weak.lock();if(!owner || !owner->getParent())return;owner->m_fields->survivalConfirmation.reset();
                    auto play=owner->m_fields->owner?owner->m_fields->owner->lock():Ref<PlayLayer>();
                    if(!play || play.data()!=PlayLayer::get() || !play->isRunning() || play->getParent()!=CCDirector::sharedDirector()->getRunningScene())return;
                    auto& current=corum::ranked::SurvivalRuntime::get();
                    if(accepted && current.canSurrender() && current.view().id==id && current.view().roundKey==key && current.view().mapGeneration==generation){
                        current.surrender();}
                });
            if(popup){m_fields->survivalConfirmation.emplace(popup);popup->show();}return;
        }
        if (!m_fields->placement && m_fields->leaseId.empty()) return;
        if (m_fields->popup) {
            auto dialog = m_fields->popup->lock();
            if (dialog && dialog->getParent()) return;
            m_fields->popup.reset();
        }
        log::info("[RankedSurrender] pause button round={}", m_fields->round);
        auto* popup = ui::RankedSurrenderPopup::create(this, m_fields->matchId, m_fields->round,
            m_fields->deathmatch, m_fields->placement, m_fields->placementMapIndex);
        if (popup) { m_fields->popup.emplace(popup); popup->show(); }
    }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void onForceClear(CCObject*) {
        auto& runtime=SurvivalRuntime::get();
        if(!m_fields->survival || !runtime.canForceClear() || runtime.forceClearPending() || m_fields->survivalConfirmation)return;
        log::debug("[SurvivalPause] Force Clear confirmation match={} round={} generation={}",m_fields->matchId,m_fields->survivalRoundKey,m_fields->survivalMapGeneration);
        auto const id=m_fields->matchId,key=m_fields->survivalRoundKey;auto const generation=m_fields->survivalMapGeneration;
        auto* popup=ui::RankedConfirmPopup::create(i18n::text("survival-force-clear","FORCE CLEAR"),
            i18n::text("survival-force-clear-confirm","Mark the current Survival round\nas 100% completed?"),
            [weak=WeakRef(this),id,key,generation](bool accepted){
                auto owner=weak.lock();if(!owner || !owner->getParent())return;owner->m_fields->survivalConfirmation.reset();
                auto play=owner->m_fields->owner?owner->m_fields->owner->lock():Ref<PlayLayer>();
                if(!play || play.data()!=PlayLayer::get() || !play->isRunning() || play->getParent()!=CCDirector::sharedDirector()->getRunningScene())return;
                auto& current=SurvivalRuntime::get();
                if(accepted && current.canForceClear() && current.view().id==id && current.view().roundKey==key && current.view().mapGeneration==generation)current.forceClear();
            },i18n::text("survival-force-clear","FORCE CLEAR"));
        if(popup){m_fields->survivalConfirmation.emplace(popup);popup->show();}
    }
#endif
};

namespace corum::ranked {
bool resumeSurvivalFreePlayPauseBeforeQuit(PlayLayer* layer) {
    auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(!layer || layer!=PlayLayer::get() || !scene || layer->getParent()!=scene)return false;
    std::vector<Ref<CCNode>> pending{Ref<CCNode>(scene)};
    Ref<PauseLayer> pause;
    while(!pending.empty()){
        auto node=std::move(pending.back());pending.pop_back();
        if(auto* native=typeinfo_cast<PauseLayer*>(node.data())){pause=native;break;}
        for(auto* child:node->getChildrenExt())pending.emplace_back(child);
    }
    if(pause && pause->getParent()){
        dismissNativeRankedExitConfirmation(pause.data());pause->onResume(nullptr);
    }
    return layer==PlayLayer::get() && layer->getParent()==scene && !layer->m_isPaused && !AppDelegate::get()->m_gamePaused;
}
bool quitRankedPauseForConcession(PlayLayer* layer) {
    if (!layer || layer != PlayLayer::get()) return false;
    auto pause = activeRankedPause ? activeRankedPause->lock() : Ref<PauseLayer>();
    if (!pause || !pause->getParent()) return false;
    auto* rankedPause = static_cast<CorumRankedPauseLayer*>(pause.data());
    auto owner = rankedPause->m_fields->owner ? rankedPause->m_fields->owner->lock() : Ref<PlayLayer>();
    auto const& ranked = RankedRuntime::get();
    if (!owner || owner.data() != layer || ranked.view().match.matchId != rankedPause->m_fields->matchId ||
        !rankedPause->hasAcknowledgedGameplayExit()) return false;
    rankedPause->m_fields->concessionQuit = true;
    // Public binding entry, with the installed native Pause quit hooks. The
    // Corum hook's qualified call below it advances to next-original once.
    pause->onQuit(nullptr);
    // Invoking a native route is not success: the device log shows the old
    // Play quit guard refusing it. Allow the existing deferred fallback when
    // the native Pause route never reached the PlayLayer quit boundary.
    return rankedPause->m_fields->nativeQuitReachedPlay;
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
                // The exact authenticated native Pause Quit owns this stack.
                // Its PlayLayer quit can be entered while native flags are
                // still paused; those flags are not a precondition for native
                // teardown. Do not block that teardown or re-enter Resume.
                rankedPause->m_fields->nativeQuitReachedPlay = true;
                return true;
            }
            if (!pause->getParent()) {
                if (layer->m_isPaused || AppDelegate::get()->m_gamePaused) return false;
                rankedPause->retireRankedPause();
                rankedPause->releaseRankedPauseOwnership();
                return true;
            }
            // The presentation heartbeat may observe an ACK before watchBudget.
            // Native Resume must complete even on that ordering, before native Quit.
            // Enter through the binding so installed Resume hooks run once in
            // their normal Tulip chain, rather than directly calling our detour.
            pause->onResume(nullptr);
            geode::log::info("[RankedPause] quit resume result paused={} appPaused={}",
                layer->m_isPaused, AppDelegate::get()->m_gamePaused);
            return !layer->m_isPaused && !AppDelegate::get()->m_gamePaused;
        }
    }
    // Unowned/ordinary/Placement Pause keeps its existing vanilla quit path.
    return true;
}
} // namespace corum::ranked
