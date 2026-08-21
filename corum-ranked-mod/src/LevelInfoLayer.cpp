#include "RankedPopup.hpp"
#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (corum::ranked::RankedRuntime::get().view().stage == corum::ranked::RuntimeStage::Matched) {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::reopenRanked), 0.35f);
        }
        return true;
    }

    void reopenRanked(float) {
        auto const& view = corum::ranked::RankedRuntime::get().view();
        if (view.stage == corum::ranked::RuntimeStage::Matched) corum::ranked::showRankedPopup();
    }
};
