#include "RankedRuntime.hpp"
#include "RankedSongGate.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

using SteadyClock = std::chrono::steady_clock;

struct PendingSongGate {
    int levelId = 0;
    double countdownRemainingSeconds = 0.0;
    bool pending = false;
};

PendingSongGate g_pendingSongGate;

bool rankedPrepareState(std::string const& state) {
    return state == "ROUND_PREPARE" || state == "DEATHMATCH_PREPARE";
}

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

bool consumeSongGateRequest(GJGameLevel* level, double& countdownRemainingSeconds) {
    if (!level || !g_pendingSongGate.pending) return false;
    if (g_pendingSongGate.levelId != static_cast<int>(level->m_levelID)) return false;
    countdownRemainingSeconds = std::max(0.0, g_pendingSongGate.countdownRemainingSeconds);
    g_pendingSongGate = {};
    return true;
}

void disableMenusOutside(CCNode* root, CCNode* allowedRoot) {
    if (!root || root == allowedRoot) return;
    if (auto* menu = typeinfo_cast<CCMenu*>(root)) {
        menu->setTouchEnabled(false);
    }
    auto* children = root->getChildren();
    if (!children) return;
    CCObject* object = nullptr;
    CCARRAY_FOREACH(children, object) {
        auto* child = typeinfo_cast<CCNode*>(object);
        if (!child || child == allowedRoot) continue;
        disableMenusOutside(child, allowedRoot);
    }
}

CCLabelBMFont* gateLabel(std::string const& text, float scale, CCPoint position, ccColor3B color = {255, 255, 255}) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition(position);
    return label;
}

} // namespace

namespace corum::ranked {

bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds) {
    if (!level) return false;
    g_pendingSongGate.levelId = static_cast<int>(level->m_levelID);
    g_pendingSongGate.countdownRemainingSeconds = std::max(0.0, countdownRemainingSeconds);
    g_pendingSongGate.pending = true;

    auto* scene = LevelInfoLayer::scene(level, false);
    if (!scene) {
        g_pendingSongGate = {};
        return false;
    }
    CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.20f, scene));
    return true;
}

} // namespace corum::ranked

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        bool returningToRanked = false;
        bool autoPlayScheduled = false;
        bool songGateActive = false;
        bool songGateReadySent = false;
        bool songGateStartingPlay = false;
        bool songGateBypassed = false;
        SteadyClock::time_point songGateOpenedAt {};
        SteadyClock::time_point songGateCountdownEndsAt {};
        CCLayerColor* songGateMask = nullptr;
        CCLabelBMFont* songGateStatus = nullptr;
        CCLabelBMFont* songGateCountdown = nullptr;
        CCNode* songWidgetOriginalParent = nullptr;
        CCPoint songWidgetOriginalPosition {};
        int songWidgetOriginalZ = 0;
    };

    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!isCurrentRankedLevel(m_level)) return;

        double countdownRemaining = 0.0;
        if (rankedPrepareState(runtime.view().match.state) && consumeSongGateRequest(m_level, countdownRemaining)) {
            setupSongDownloadGate(countdownRemaining);
            return;
        }

        if (rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) {
            scheduleAutoPlay(0.22f);
        } else {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.10f);
        }
    }

    void setupSongDownloadGate(double countdownRemaining) {
        if (m_fields->songGateActive) return;
        m_fields->songGateActive = true;
        m_fields->songGateOpenedAt = SteadyClock::now();
        m_fields->songGateCountdownEndsAt = m_fields->songGateOpenedAt + std::chrono::milliseconds(
            static_cast<long long>(std::ceil(std::max(0.0, countdownRemaining) * 1000.0))
        );

        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* mask = CCLayerColor::create({18, 24, 38, 255});
        mask->setContentSize(size);
        mask->setPosition(CCPointZero);
        mask->setZOrder(1000);
        addChild(mask, 1000);
        m_fields->songGateMask = mask;

        auto* title = gateLabel("DOWNLOAD SONG", 0.48f, {size.width / 2.0f, size.height - 34.0f}, {255, 216, 86});
        mask->addChild(title, 2);
        auto* hint = gateLabel("ONLY THE SONG DOWNLOAD CONTROL IS AVAILABLE", 0.20f, {size.width / 2.0f, size.height - 58.0f}, {185, 196, 216});
        hint->limitLabelWidth(size.width - 56.0f, 0.20f, 0.14f);
        mask->addChild(hint, 2);

        m_fields->songGateStatus = gateLabel("USE THE GEOMETRY DASH DOWNLOAD BUTTON", 0.24f, {size.width / 2.0f, 54.0f}, {95, 180, 255});
        mask->addChild(m_fields->songGateStatus, 2);
        m_fields->songGateCountdown = gateLabel("STARTS IN...", 0.34f, {size.width / 2.0f, 28.0f}, {225, 225, 230});
        mask->addChild(m_fields->songGateCountdown, 2);

        // Keep the real, on-screen Geometry Dash CustomSongWidget. alpha.16
        // created a hidden LevelInfoLayer and invoked its widget/manager directly;
        // that path could crash because the vanilla control was not attached to
        // its normal scene lifecycle. Here the exact LevelInfoLayer widget is the
        // only interactive control during preparation.
        if (m_songWidget) {
            auto* parent = m_songWidget->getParent();
            if (parent) {
                m_fields->songWidgetOriginalParent = parent;
                m_fields->songWidgetOriginalPosition = m_songWidget->getPosition();
                m_fields->songWidgetOriginalZ = m_songWidget->getZOrder();
                auto const worldPosition = parent->convertToWorldSpace(m_songWidget->getPosition());
                auto const localPosition = convertToNodeSpace(worldPosition);
                m_songWidget->retain();
                m_songWidget->removeFromParentAndCleanup(false);
                addChild(m_songWidget, 1002);
                m_songWidget->setPosition(localPosition);
                m_songWidget->release();
            } else {
                m_songWidget->setZOrder(1002);
            }
        }

        // Underlying LevelInfo buttons remain visually covered by the mask and
        // their menus are disabled, so taps cannot leak through to Play/Back/etc.
        // The reparented CustomSongWidget is excluded and remains fully vanilla.
        disableMenusOutside(this, m_songWidget);

        schedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate), 0.20f);
        updateSongDownloadGate(0.0f);
    }

    void updateSongDownloadGate(float) {
        if (!m_fields->songGateActive) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();

        if (!isCurrentRankedLevel(m_level)) {
            returnToRanked(0.0f);
            return;
        }

        auto const& match = runtime.view().match;
        auto const ready = songsReady(m_level);
        auto const now = SteadyClock::now();
        auto const songElapsed = std::chrono::duration<double>(now - m_fields->songGateOpenedAt).count();
        auto const countdownRemaining = std::max(
            0,
            static_cast<int>(std::ceil(std::chrono::duration<double>(m_fields->songGateCountdownEndsAt - now).count()))
        );

        if (ready) {
            runtime.setSongBypassAllowed(false);
            m_fields->songGateBypassed = false;
            if (m_fields->songGateStatus) {
                m_fields->songGateStatus->setString("DOWNLOADED");
                m_fields->songGateStatus->setColor({78, 232, 112});
            }
        } else if (songElapsed >= 20.0) {
            // Song availability is non-fatal. Keep Geometry Dash's own download
            // request alive; only stop blocking Ranked readiness after 20 seconds.
            runtime.setSongBypassAllowed(true);
            m_fields->songGateBypassed = true;
            if (m_fields->songGateStatus) {
                m_fields->songGateStatus->setString("SONG TIMEOUT - STARTING WITHOUT SONG");
                m_fields->songGateStatus->setColor({255, 216, 86});
            }
        } else if (m_fields->songGateStatus) {
            m_fields->songGateStatus->setString("USE THE GEOMETRY DASH DOWNLOAD BUTTON");
            m_fields->songGateStatus->setColor({95, 180, 255});
        }

        if (m_fields->songGateCountdown) {
            if (countdownRemaining > 0) {
                m_fields->songGateCountdown->setString(fmt::format("STARTS IN... {}", countdownRemaining).c_str());
            } else if (!ready && !m_fields->songGateBypassed) {
                m_fields->songGateCountdown->setString("WAITING FOR SONG...");
            } else {
                m_fields->songGateCountdown->setString("READY");
            }
        }

        if (
            rankedPrepareState(match.state) &&
            now >= m_fields->songGateCountdownEndsAt &&
            (ready || m_fields->songGateBypassed) &&
            !m_fields->songGateReadySent
        ) {
            m_fields->songGateReadySent = true;
            runtime.submitReady();
            return;
        }

        if (rankedPlayingState(match.state)) {
            if (!runtime.canEnterCurrentLevel() || m_fields->songGateStartingPlay) return;
            m_fields->songGateStartingPlay = true;
            if (!ready && runtime.songBypassAllowed()) {
                // This LevelInfoLayer is the real on-screen vanilla layer and has
                // completed its normal initialization, so acknowledge only the
                // missing-song warning and then use the normal onPlay path.
                m_level->m_showedSongWarning = true;
            }
            teardownSongDownloadGate();
            LevelInfoLayer::onPlay(nullptr);
            return;
        }

        if (
            match.state == "CANCELLED" ||
            match.state == "MATCH_RESULT" ||
            match.state == "ROUND_RESULT" ||
            match.state == "DEATHMATCH_RESULT"
        ) {
            returnToRanked(0.0f);
        }
    }

    void teardownSongDownloadGate() {
        if (!m_fields->songGateActive) return;
        m_fields->songGateActive = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate));

        if (m_songWidget && m_fields->songWidgetOriginalParent && m_songWidget->getParent() == this) {
            m_songWidget->retain();
            m_songWidget->removeFromParentAndCleanup(false);
            m_fields->songWidgetOriginalParent->addChild(m_songWidget, m_fields->songWidgetOriginalZ);
            m_songWidget->setPosition(m_fields->songWidgetOriginalPosition);
            m_songWidget->release();
        }
        if (m_fields->songGateMask) {
            m_fields->songGateMask->removeFromParentAndCleanup(true);
            m_fields->songGateMask = nullptr;
        }
    }

    void scheduleAutoPlay(float delay) {
        if (m_fields->autoPlayScheduled || m_fields->returningToRanked || m_fields->songGateActive) return;
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
            // A live round should only be reachable after this client submitted
            // Ready from the song gate. If state is ever inconsistent, return to
            // Ranked rather than invoking any hidden downloader from LevelInfo.
            returnToRanked(0.0f);
            return;
        }

        if (!ready && runtime.songBypassAllowed()) {
            m_level->m_showedSongWarning = true;
        }
        LevelInfoLayer::onPlay(nullptr);
    }

    void onPlay(CCObject* sender) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (m_fields->songGateActive) {
            // During preparation the real song widget is the only permitted
            // control. Do not let a leaked Play tap bypass the Ranked countdown.
            return;
        }
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
        if (m_fields->songGateActive && rankedPrepareState(runtime.view().match.state)) return;
        if (isCurrentRankedLevel(m_level) && rankedPlayingState(runtime.view().match.state) && runtime.canEnterCurrentLevel()) return;
        if (m_fields->returningToRanked) return;
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void onBack(CCObject* sender) {
        if (m_fields->songGateActive) return;
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
        if (m_fields->songGateActive) return;
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
