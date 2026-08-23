#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace geode::prelude;

namespace {

bool authoritativeExitState(std::string const& state) {
    return
        state == "ROUND_RESULT" ||
        state == "DEATHMATCH_RESULT" ||
        state == "MATCH_RESULT" ||
        state == "CANCELLED";
}

} // namespace

class $modify(CorumRankedEndLevelLayer, EndLevelLayer) {
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
            schedule(schedule_selector(CorumRankedEndLevelLayer::watchRankedExit), 0.10f);
        }
    }

    void watchRankedExit(float) {
        if (!m_fields->rankedLevel || m_fields->rankedExitRequested) return;

        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        auto const& state = runtime.view().match.state;
        if (!runtime.isSpectating() && !authoritativeExitState(state)) return;

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
