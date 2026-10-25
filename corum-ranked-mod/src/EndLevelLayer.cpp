#include "RankedRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "SurvivalSceneBridge.hpp"
#include "spectator/SpectatorSceneBridge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace geode::prelude;

namespace {

// Deliberately process-lived slots: Cocos must never be touched by a DLL static destructor.
// Normal onExit below releases controllers while their scene is still alive.
auto& rankedCompletionOverlay = *new WeakRef<EndLevelLayer>;
auto& survivalCompletionOverlay = *new WeakRef<EndLevelLayer>;

bool authoritativeExitState(std::string const& state) {
    return
        state == "ROUND_RESULT" ||
        state == "DEATHMATCH_RESULT" ||
        state == "MATCH_RESULT" ||
        state == "MAP_RESULT" ||
        state == "STAGE_RESULT" ||
        state == "PLACEMENT_RESULT" ||
        state == "CANCELLED";
}

} // namespace

class $modify(CorumRankedEndLevelLayer, EndLevelLayer) {
    struct Fields {
        bool rankedLevel = false;
        bool rankedExitRequested = false;
        bool survivalLevel = false;
    };

    void customSetup() {
        EndLevelLayer::customSetup();

        // EndLevelLayer::init(PlayLayer*) is inline on Windows and iOS, so
        // Geode deliberately rejects hooks for it on those targets. By the
        // time customSetup() runs, init has already assigned m_playLayer.
        auto* playLayer = m_playLayer;
        m_fields->survivalLevel=corum::ranked::isOfficialSurvivalPlayLayer(playLayer);
        if(m_fields->survivalLevel || (playLayer && corum::ranked::SurvivalRuntime::get().freePlaying() &&
            playLayer->getChildByIDRecursive("survival-play-hud"_spr))){
            survivalCompletionOverlay=WeakRef<EndLevelLayer>(this);
            if(m_fields->survivalLevel)schedule(schedule_selector(CorumRankedEndLevelLayer::watchSurvivalExit),.1f);
        }

        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const levelId =
            playLayer && playLayer->m_level
                ? static_cast<int>(playLayer->m_level->m_levelID)
                : 0;
        auto const hasRankedHud =
            playLayer && playLayer->getChildByID("ranked-play-hud"_spr) != nullptr;
        m_fields->rankedLevel =
            hasRankedHud ||
            (
                levelId > 0 &&
                runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
                runtime.isGameplayLevel(levelId)
            );
        if (m_fields->rankedLevel) {
            rankedCompletionOverlay = WeakRef<EndLevelLayer>(this);
            schedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit), 0.10f);
        }
    }

    void watchRankedExit(float) {
        if (!m_fields->rankedLevel || m_fields->rankedExitRequested) return;

        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        // The PlayLayer heartbeat and this native completion overlay share the
        // same terminal journal. Never pop/convert before its Start/End FIFO has
        // drained; spectator permission alone does not prove local End ACK.
        if (runtime.hasLocalAttemptInFlight()) return;
        auto const& state = runtime.view().match.state;
        if (runtime.isSpectating() && !runtime.view().match.spectatorLiveEnabled) {
            m_fields->rankedExitRequested = true;
            unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
            EndLevelLayer::onMenu(nullptr); return;
        }
        if (runtime.isSpectating()) {
            // Reset may remove the overlay: retain this callback's owner first.
            Ref<EndLevelLayer> retained(this);
            if (!corum::ranked::prepareSpectatorScene(m_playLayer)) return;
            m_fields->rankedExitRequested = true;
            unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
            log::info(
                "Closing Ranked completion overlay for live spectator handoff: state={}",
                state
            );
            // Same canonical PlayLayer: hidden frozen dummy, dedicated remote
            // renderer and scene-owned camera heartbeat. No Attempt is armed.
            removeFromParentAndCleanup(true);
            return;
        }
        if (!authoritativeExitState(state) &&
            (runtime.hasLocalGameplayAuthority() || runtime.canEnterCurrentLevel())) return;

        m_fields->rankedExitRequested = true;
        unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
        log::info(
            "Leaving Ranked completion overlay for authoritative state: state={} spectator={}",
            state,
            runtime.isSpectating()
        );
        // Use Geometry Dash's own completion-menu exit path. Calling
        // PlayLayer::onQuit() underneath a live EndLevelLayer leaves an orphaned
        // overlay whose Replay/Menu buttons no longer receive input.
        EndLevelLayer::onMenu(nullptr);
    }
    void watchSurvivalExit(float) {
        auto& runtime=corum::ranked::SurvivalRuntime::get();runtime.tick();
        if(runtime.normalRoundCleared() || runtime.view().state=="ROUND_RESULT" || runtime.view().state=="FINAL_RESULT" || runtime.view().state=="CANCELLED")
            corum::ranked::requestSurvivalGameplayReturn(m_playLayer);
    }

    void onReplay(CCObject* sender) {
        if(m_fields->survivalLevel && corum::ranked::SurvivalRuntime::get().view().phase!="TIEBREAK"){
            // Keep the native completion visible until the terminal event ACK;
            // replay must not reopen a normal-round 100% attempt in that gap.
            if(!corum::ranked::SurvivalRuntime::get().normalRoundCleared())return;
            corum::ranked::requestSurvivalGameplayReturn(m_playLayer);
            return;
        }
        if (m_fields->rankedLevel) {
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (runtime.isSpectating() && runtime.view().match.spectatorLiveEnabled) {
                Ref<EndLevelLayer> retained(this);
                if (!corum::ranked::prepareSpectatorScene(m_playLayer)) return;
                m_fields->rankedExitRequested = true;
                unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
                removeFromParentAndCleanup(true);
                return;
            }
            if (!runtime.canEnterCurrentLevel()) {
                log::info("Ranked Replay redirected to Menu because a new attempt is closed");
                m_fields->rankedExitRequested = true;
                unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
                EndLevelLayer::onMenu(sender);
                return;
            }
        }
        EndLevelLayer::onReplay(sender);
    }

    void onExit() {
        Ref<EndLevelLayer> retained(this);
        if(auto overlay=rankedCompletionOverlay.lock();overlay && overlay.data()==this)rankedCompletionOverlay={};
        if(auto overlay=survivalCompletionOverlay.lock();overlay && overlay.data()==this)survivalCompletionOverlay={};
        unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
        unschedule(schedule_selector(CorumRankedEndLevelLayer::watchSurvivalExit));
        EndLevelLayer::onExit();
    }

    void onMenu(CCObject* sender) {
        if (m_fields->rankedLevel) {
            m_fields->rankedExitRequested = true;
            unschedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit));
        }
        EndLevelLayer::onMenu(sender);
    }
};

bool corum::ranked::quitThroughRankedCompletionOverlay(PlayLayer* layer) {
    auto overlay = rankedCompletionOverlay.lock();
    if (!overlay || !overlay->getParent() || overlay->m_playLayer != layer) return false;
    overlay->onMenu(nullptr);
    return true;
}
bool corum::ranked::quitThroughSurvivalCompletionOverlay(PlayLayer* layer) {
    auto overlay=survivalCompletionOverlay.lock();
    if(!overlay || !overlay->getParent() || overlay->m_playLayer!=layer)return false;
    overlay->onMenu(nullptr);return true;
}
