#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

bool rankedPlayingState(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING";
}

std::vector<int> collectSongIds(GJGameLevel* level) {
    std::vector<int> result;
    if (!level) return result;
    auto add = [&result](int id) {
        if (id <= 0) return;
        if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    };
    add(static_cast<int>(level->m_songID));
    std::stringstream stream(std::string(level->m_songIDs.c_str()));
    std::string token;
    while (std::getline(stream, token, ',')) {
        try { add(std::stoi(token)); } catch (...) {}
    }
    return result;
}

bool songsReady(GJGameLevel* level) {
    auto ids = collectSongIds(level);
    if (ids.empty()) return true;
    auto* manager = MusicDownloadManager::sharedState();
    if (!manager) return false;
    for (auto id : ids) {
        if (!manager->isResourceSong(id) && !manager->isSongDownloaded(id)) return false;
    }
    return true;
}

bool isCurrentRankedLevel(GJGameLevel* level) {
    auto& runtime = corum::ranked::RankedRuntime::get();
    return
        level &&
        runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
        runtime.currentLevelId() == static_cast<int>(level->m_levelID);
}

} // namespace

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        bool returningToRanked = false;
        bool autoPlayScheduled = false;
        bool vanillaSongKickIssued = false;
    };

    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;

        // Use the game's own CustomSongWidget as the authoritative download driver.
        // This is the same path as pressing the vanilla download button on LevelInfoLayer.
        if (!songsReady(m_level) && m_songWidget && m_songWidget->m_isNotDownloading) {
            m_songWidget->onDownload(nullptr);
            m_fields->vanillaSongKickIssued = true;
        }

        if (rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) {
            scheduleAutoPlay(0.22f);
        } else {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.10f);
        }
    }

    void scheduleAutoPlay(float delay) {
        if (m_fields->autoPlayScheduled || m_fields->returningToRanked) return;
        m_fields->autoPlayScheduled = true;
        scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked), delay);
    }

    void autoPlayRanked(float) {
        m_fields->autoPlayScheduled = false;
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;
        if (!rankedPlayingState(runtime.view().match.state)) {
            returnToRanked(0.0f);
            return;
        }
        if (!runtime.canEnterCurrentLevel()) {
            returnToRanked(0.0f);
            return;
        }

        auto const ready = songsReady(m_level);
        if (!ready && !runtime.songBypassAllowed()) {
            // If the pre-round download did not actually engage, kick the exact
            // vanilla widget again instead of showing a fake DOWNLOADING state.
            if (m_songWidget && m_songWidget->m_isNotDownloading) {
                m_songWidget->onDownload(nullptr);
                m_fields->vanillaSongKickIssued = true;
            }
            scheduleAutoPlay(0.25f);
            return;
        }

        if (!ready && runtime.songBypassAllowed()) {
            // Preserve Geometry Dash's complete onPlay setup path. alpha.14/15
            // jumped directly into playStep2, which skips state initialized by
            // onPlay on current GD builds and can fail when START WITHOUT SONG is
            // reached. Mark the vanilla warning as already acknowledged instead,
            // then let onPlay perform the normal level transition.
            m_level->m_showedSongWarning = true;
            LevelInfoLayer::onPlay(nullptr);
            return;
        }
        LevelInfoLayer::onPlay(nullptr);
    }

    void onPlay(CCObject* sender) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            isCurrentRankedLevel(m_level) &&
            rankedPlayingState(runtime.view().match.state) &&
            !songsReady(m_level) &&
            runtime.songBypassAllowed()
        ) {
            m_level->m_showedSongWarning = true;
            LevelInfoLayer::onPlay(sender);
            return;
        }
        LevelInfoLayer::onPlay(sender);
    }

    void returnToRanked(float) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;
        if (rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) return;
        if (m_fields->returningToRanked) return;
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void onBack(CCObject* sender) {
        if (!isCurrentRankedLevel(m_level)) {
            LevelInfoLayer::onBack(sender);
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            // During a live Round, repeated Back taps cannot escape the Ranked stack.
            // Death Match is the exception after all 3 local attempts are consumed:
            // return to the Ranked waiting screen instead of re-entering gameplay.
            if (runtime.canEnterCurrentLevel()) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }

    void keyBackClicked() override {
        if (!isCurrentRankedLevel(m_level)) {
            LevelInfoLayer::keyBackClicked();
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            if (runtime.canEnterCurrentLevel()) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }
};
