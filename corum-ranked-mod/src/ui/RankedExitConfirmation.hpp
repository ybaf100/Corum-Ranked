#pragma once

#include "../RankedRuntime.hpp"
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/FLAlertLayerProtocol.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;

// The native Confirm Exit callback can reach onQuit without a usable Pause
// receiver on Windows. This delegate belongs to the tracked Pause instead.
class RankedExitConfirmation final : public CCNode, public FLAlertLayerProtocol {
    std::optional<WeakRef<PauseLayer>> m_pause;
    std::optional<WeakRef<PlayLayer>> m_play;
    std::optional<WeakRef<FLAlertLayer>> m_alert;
    std::string m_matchId;
    int m_round = 0, m_deathmatch = 0;
    bool m_closed = false;

    bool sameContext() const {
        auto pause = m_pause ? m_pause->lock() : Ref<PauseLayer>();
        auto play = m_play ? m_play->lock() : Ref<PlayLayer>();
        auto const& ranked = RankedRuntime::get();
        auto const& match = ranked.view().match;
        return pause && pause->getParent() && play && play.data() == PlayLayer::get() &&
            ranked.view().stage == RuntimeStage::Matched && !ranked.isSpectating() &&
            match.matchId == m_matchId && match.roundNumber == m_round &&
            match.deathmatchSequence == m_deathmatch && ranked.matchPauseRemainingMs() > 0 &&
            !ranked.gameplayConceded(m_round, m_deathmatch);
    }
    void watchContext(float) {
        if (!sameContext()) dismiss();
    }
    static void releaseAlertInput(FLAlertLayer* alert) {
        if (alert->m_forcePrioRegistered) {
            CCTouchDispatcher::get()->unregisterForcePrio(alert);
            alert->m_forcePrioRegistered = false;
        }
    }
    void closeAlert() {
        unschedule(schedule_selector(RankedExitConfirmation::watchContext));
        if (m_alert) if (auto alert = m_alert->lock()) {
            alert->m_alertProtocol = nullptr;
            releaseAlertInput(alert.data());
            if (alert->getParent()) alert->onBtn1(nullptr);
        }
        m_alert.reset(); m_pause.reset(); m_play.reset();
    }
    bool init(PauseLayer* pause, PlayLayer* play) {
        if (!CCNode::init()) return false;
        auto const& match = RankedRuntime::get().view().match;
        m_pause.emplace(pause); m_play.emplace(play);
        m_matchId = match.matchId; m_round = match.roundNumber; m_deathmatch = match.deathmatchSequence;
        schedule(schedule_selector(RankedExitConfirmation::watchContext), 0.05f);
        return true;
    }
public:
    static RankedExitConfirmation* create(PauseLayer* pause, PlayLayer* play) {
        auto* result = new RankedExitConfirmation;
        if (result->init(pause, play)) { result->autorelease(); return result; }
        delete result; return nullptr;
    }
    void show() {
        if (m_closed || !sameContext()) return;
        auto* alert = FLAlertLayer::create(this, "Surrender Match?",
            "Leaving will <cr>surrender this match</c> and count as a loss.\nDo you want to leave?",
            "Stay", "Surrender", 360);
        if (!alert) { dismiss(); return; }
        m_alert.emplace(alert);
        alert->setID("ranked-exit-confirmation"_spr);
        alert->show();
    }
    void FLAlert_Clicked(FLAlertLayer* alert, bool confirmed) override {
        [[maybe_unused]] Ref<RankedExitConfirmation> keepAlive(this);
        if (m_closed) return;
        auto const valid = sameContext();
        if (!m_alert) return;
        auto expected = m_alert->lock();
        if (!expected || expected.data() != alert) return;
        alert->m_alertProtocol = nullptr;
        releaseAlertInput(alert);
        // Native onBtn2 owns removal of this alert. Stop holding its delegate
        // before requesting an asynchronous, ACK-bound gameplay handoff.
        m_alert.reset();
        if (confirmed && valid) RankedRuntime::get().surrenderCurrentMatch(true);
        dismiss();
    }
    void dismiss() {
        if (m_closed) return;
        [[maybe_unused]] Ref<RankedExitConfirmation> keepAlive(this);
        m_closed = true;
        closeAlert();
        removeFromParentAndCleanup(true);
    }
    void onExit() override {
        m_closed = true;
        closeAlert();
        CCNode::onExit();
    }
};
} // namespace corum::ranked::ui
