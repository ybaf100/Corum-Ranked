#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

namespace {

bool rankedPlayingState(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "DEATHMATCH_PLAYING";
}

} // namespace

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_level || runtime.view().stage != corum::ranked::RuntimeStage::Matched) return;
        if (runtime.currentLevelId() != static_cast<int>(m_level->m_levelID)) return;

        if (rankedPlayingState(runtime.view().match.state)) {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked), 0.18f);
        } else {
            // The round/match ended while PlayLayer was active. Return through GD's
            // normal LevelInfoLayer stack instead of constructing or replacing a
            // gameplay scene from the Ranked UI.
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.12f);
        }
    }

    void autoPlayRanked(float) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_level || runtime.view().stage != corum::ranked::RuntimeStage::Matched) return;
        if (runtime.currentLevelId() != static_cast<int>(m_level->m_levelID)) return;
        if (!rankedPlayingState(runtime.view().match.state)) return;
        LevelInfoLayer::onPlay(nullptr);
    }

    void returnToRanked(float) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_level || runtime.view().stage != corum::ranked::RuntimeStage::Matched) return;
        if (runtime.currentLevelId() != static_cast<int>(m_level->m_levelID)) return;
        if (rankedPlayingState(runtime.view().match.state)) return;
        CCDirector::sharedDirector()->popScene();
    }
};
