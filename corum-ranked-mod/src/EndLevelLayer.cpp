#include "diagnostic/CrashHookDiagnostics.hpp"
#include "RankedRuntime.hpp"
#include "spectator/SpectatorSceneBridge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace geode::prelude;

namespace {

WeakRef<EndLevelLayer> rankedCompletionOverlay;

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
    static void onModify(auto& self) {
#if defined(CORUM_CRASH_DIAG)
        if (corum::ranked::diagnostic::excludeGameplayGroup(self, "CorumRankedEndLevelLayer")) return;
#endif
#if defined(CORUM_CRASH_DIAG)
        CORUM_CRASH_MODIFY(self, "CorumRankedEndLevelLayer");
#endif
    }

    struct Fields {
        bool rankedLevel = false;
        bool rankedExitRequested = false;
    };

    void customSetup() {
        EndLevelLayer::customSetup();

        // EndLevelLayer::init(PlayLayer*) is inline on Windows and iOS, so
        // Geode deliberately rejects hooks for it on those targets. By the
        // time customSetup() runs, init has already assigned m_playLayer.
        auto* playLayer = m_playLayer;

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
        auto const& state = runtime.view().match.state;
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
        if (!authoritativeExitState(state)) return;

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

    void onReplay(CCObject* sender) {
        if (m_fields->rankedLevel) {
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (runtime.isSpectating()) {
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
