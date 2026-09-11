#include "RankedRuntime.hpp"
#include "RankedSongGate.hpp"
#include "RankedLevelPreparation.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/GameLevelOptionsLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/utils/addresser.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace {

using SteadyClock = std::chrono::steady_clock;

struct PendingPrepareGate {
    corum::ranked::LevelPreparationContext context;
    SteadyClock::time_point countdownEndsAt {};
    bool pending = false;
};

PendingPrepareGate g_pendingPrepareGate;

std::string tr(std::string_view key, std::string_view english) {
    return corum::ranked::i18n::text(key, english);
}

bool rankedPrepareState(std::string const& state) {
    return
        state == "ROUND_PREPARE" ||
        state == "DEATHMATCH_PREPARE" ||
        state == "MAP_PREPARE";
}

bool rankedPlayingState(std::string const& state) {
    return
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING" ||
        state == "MAP_PLAYING" ||
        state == "MAP_SETTLING";
}

bool rankedDownloadGateState(std::string const& state) {
    return
        rankedPrepareState(state) ||
        state == "ROUND_PLAYING" ||
        state == "FINAL_ATTEMPT_WINDOW" ||
        state == "LAST_ATTEMPT_WINDOW" ||
        state == "ROUND_SETTLING" ||
        state == "DEATHMATCH_PLAYING" ||
        state == "MAP_PLAYING";
}

bool rankedLevelEntryAllowed(corum::ranked::RankedRuntime const& runtime) {
    return
        runtime.canEnterCurrentLevel() ||
        runtime.canEnterCurrentLevelAsSpectator();
}

bool jukeboxLoaded() {
    return Loader::get() && Loader::get()->isModLoaded("fleym.nongd");
}

bool hasOpenModal(CCNode* root) {
    if (!root || !root->isVisible()) return false;
    for (auto* child : root->getChildrenExt()) {
        if (!child || !child->isVisible()) continue;
        if (typeinfo_cast<FLAlertLayer*>(child)) return true;
        if (hasOpenModal(child)) return true;
    }
    return false;
}

GameLevelOptionsLayer* openLevelOptions(CCNode* root) {
    if (!root || !root->isVisible()) return nullptr;
    if (auto* options = typeinfo_cast<GameLevelOptionsLayer*>(root)) return options;
    for (auto* child : root->getChildrenExt()) {
        if (auto* options = openLevelOptions(child)) return options;
    }
    return nullptr;
}

std::string rankedGateTitle() {
    auto const& match = corum::ranked::RankedRuntime::get().view().match;
    auto const& placement = corum::ranked::RankedRuntime::get().view().placement;
    if (match.placement) {
        return fmt::format(
            "STAGE {} · MAP {}",
            placement.currentStageIndex,
            placement.currentMapIndex
        );
    }
    if (match.deathmatchSequence > 0 || match.state == "DEATHMATCH_PREPARE" || match.state == "DEATHMATCH_PLAYING") {
        return tr("death-match", "DEATH MATCH");
    }
    if (match.roundNumber <= 0) return "RANKED MATCH";
    if (match.roundNumber == 2) return "ROUND 2 - MATCH POINT";
    if (match.roundNumber == 3) return "ROUND 3 - TIEBREAKER";
    return fmt::format("ROUND {}", match.roundNumber);
}

bool mapReady(GJGameLevel* level) {
    if (!level || level->m_levelString.empty() || level->m_levelNotDownloaded) return false;
    auto const expectedLevelId = corum::ranked::RankedRuntime::get().currentLevelId();
    return expectedLevelId > 0 && expectedLevelId == static_cast<int>(level->m_levelID);
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

bool songFileExists(MusicDownloadManager* manager, int id) {
    if (!manager || id <= 0) return false;
    auto const path = manager->pathForSong(id);
    if (path.empty()) return false;
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(path.c_str()), error) && !error;
}

bool songIdReady(MusicDownloadManager* manager, int id) {
    if (!manager || id <= 0) return false;
    return
        manager->isResourceSong(id) ||
        manager->isSongDownloaded(id) ||
        songFileExists(manager, id);
}

bool songsReady(GJGameLevel* level, CustomSongWidget* songWidget) {
    if (!mapReady(level) || !songWidget ||
        !corum::ranked::RankedLevelPreparation::get().authorizes(
            level, corum::ranked::currentPreparationContext())) return false;

    // Only trust the native classification after the level's online metadata has
    // been attested. Real RobTop songs (including track 0) still need no download.
    if (songWidget && songWidget->m_isRobtopSong) return true;

    auto* manager = MusicDownloadManager::sharedState();
    if (songWidget && songWidget->m_customSongID > 0) {
        return songIdReady(manager, songWidget->m_customSongID);
    }

    auto ids = collectSongIds(level);
    if (ids.empty()) return true;
    if (!manager) return false;
    for (auto id : ids) {
        if (!songIdReady(manager, id)) return false;
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

bool consumePrepareGateRequest(GJGameLevel* level, SteadyClock::time_point& countdownEndsAt) {
    if (!level || !g_pendingPrepareGate.pending) return false;
    auto const exactContext =
        g_pendingPrepareGate.context.levelId == static_cast<int>(level->m_levelID) &&
        g_pendingPrepareGate.context == corum::ranked::currentPreparationContext();
    if (!exactContext) {
        g_pendingPrepareGate = {};
        return false;
    }
    countdownEndsAt = g_pendingPrepareGate.countdownEndsAt;
    g_pendingPrepareGate = {};
    return true;
}

void setMenuItemsEnabled(CCNode* root, bool enabled) {
    if (!root) return;
    // A popup can be attached under LevelInfo. Its own controls and close button
    // must not be disabled by the underlying page's recurring prepare lock.
    if (typeinfo_cast<FLAlertLayer*>(root)) return;
    if (auto* item = typeinfo_cast<CCMenuItem*>(root)) item->setEnabled(enabled);
    for (CCNode* child : root->getChildrenExt()) {
        if (child) setMenuItemsEnabled(child, enabled);
    }
}

void enableSongAcquisitionControls(CustomSongWidget* songWidget) {
    if (!songWidget) return;

    // Jukebox opens its NONG manager from the song widget itself. When it is
    // actually loaded, restore the whole song-widget subtree rather than only
    // vanilla download buttons. The effective file is still validated through
    // MusicDownloadManager::pathForSong() before Ranked can launch.
    if (jukeboxLoaded()) setMenuItemsEnabled(songWidget, true);

    // CustomSongWidget is a CCNode with its own button menu. Disabling parent
    // CCMenu touch dispatch (the alpha.21 approach) can leave the visible song
    // download button completely inert. Keep menu dispatch alive and gate at the
    // individual menu-item level instead.
    if (songWidget->m_buttonMenu) songWidget->m_buttonMenu->setTouchEnabled(true);

    auto enable = [](CCMenuItemSpriteExtra* item) {
        if (!item) return;
        item->setEnabled(true);
    };
    enable(songWidget->m_downloadBtn);
    enable(songWidget->m_cancelDownloadBtn);
    enable(songWidget->m_getSongInfoBtn);
}

// During prepare, leave Cocos menu touch dispatch enabled and disable the actual
// menu items instead. Then explicitly restore only the vanilla song acquisition
// controls. This makes the on-screen GD download button clickable while blocking
// Play / Back / Like / Info / leaderboard / refresh and every other menu action.
void lockInteractionToSong(CCNode* root, CustomSongWidget* songWidget) {
    if (!root) return;
    setMenuItemsEnabled(root, false);
    enableSongAcquisitionControls(songWidget);
}

void lockAllRankedPrepareInteraction(CCNode* root) {
    if (!root) return;
    setMenuItemsEnabled(root, false);
}

void unlockRankedPrepareInteraction(CCNode* root) {
    // Restore the LevelInfo controls when its prepare gate is torn down. A
    // successful vanilla Play transition reaches onExit before this runs.
    setMenuItemsEnabled(root, true);
}

bool nodeContains(CCNode* root, CCNode* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    for (auto* child : root->getChildrenExt()) {
        if (nodeContains(child, target)) return true;
    }
    return false;
}

CCMenuItem* vanillaPlayButton(CCMenu* menu, CCNode* playSprite) {
    if (!menu || !playSprite) return nullptr;

    auto verifiedPlayItem = [playSprite](CCNode* node) -> CCMenuItem* {
        auto* item = typeinfo_cast<CCMenuItemSprite*>(node);
        if (!item || !nodeContains(item->getNormalImage(), playSprite)) return nullptr;
        return item;
    };

    // Node IDs are the most explicit route when geode.node-ids (or another
    // compatible ID provider) is present, but an ID alone is not sufficient:
    // while LevelInfo is rebuilding, the central item can temporarily represent
    // download/update work. Require the live native m_playSprite identity too.
    if (auto* identified = verifiedPlayItem(menu->getChildByID("play-button"))) {
        return identified;
    }

    for (auto* child : menu->getChildrenExt()) {
        if (auto* item = verifiedPlayItem(child)) return item;
    }
    return nullptr;
}

CCMenuItem* vanillaOptionsButton(LevelInfoLayer* layer) {
    if (!layer) return nullptr;
    auto* menu = typeinfo_cast<CCMenu*>(layer->getChildByID("settings-menu"));
    if (!menu) return nullptr;
    auto* item = typeinfo_cast<CCMenuItem*>(menu->getChildByID("settings-button"));
    if (!item || item->getParent() != menu ||
        item->m_pListener != static_cast<CCObject*>(layer) || !item->m_pfnSelector) return nullptr;
    // Resolve only OUR known nonvirtual binding, never an unknown candidate (a
    // virtual member pointer is not an executable address on every GD platform).
    // Preserve the selector's this-adjustment and accept the binding or the
    // actual native entry installed by GD; do not change the item's callback.
    auto const bindingSelector = menu_selector(LevelInfoLayer::onLevelOptions);
    auto nativeSelector = bindingSelector;
    auto const nativeAddress = geode::addresser::getNonVirtual(&LevelInfoLayer::onLevelOptions);
    static_assert(sizeof(nativeSelector) >= sizeof(nativeAddress));
    std::memcpy(&nativeSelector, &nativeAddress, sizeof(nativeAddress));
    if (item->m_pfnSelector != bindingSelector && item->m_pfnSelector != nativeSelector) return nullptr;
    return item;
}

CCLabelBMFont* gateLabel(std::string const& text, float scale, CCPoint position, ccColor3B color = {255, 255, 255}) {
    auto weight = corum::ranked::ui::FontWeight::SemiBold;
    if (scale >= 0.30f) weight = corum::ranked::ui::FontWeight::Black;
    return corum::ranked::ui::label(text, scale, position, color, weight);
}

CCNode* gatePanel(CCSize size, CCPoint position, ccColor3B accent = {52, 214, 255}) {
    return corum::ranked::ui::surface(
        size,
        position,
        accent,
        corum::ranked::ui::Ink,
        226,
        true
    );
}

} // namespace

namespace corum::ranked {

bool rankedSongResourceReady(MusicDownloadManager* manager, int songId) {
    return songIdReady(manager, songId);
}

bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds) {
    auto const context = currentPreparationContext();
    if (!RankedLevelPreparation::get().authorizes(level, context)) return false;
    g_pendingPrepareGate.context = context;
    g_pendingPrepareGate.countdownEndsAt = SteadyClock::now() + std::chrono::milliseconds(
        static_cast<long long>(std::ceil(std::max(0.0, countdownRemainingSeconds) * 1000.0))
    );
    g_pendingPrepareGate.pending = true;

    tracePreparedLevel("scene-before-prepare", level);
    auto* scene = LevelInfoLayer::scene(level, false);
    if (!scene) {
        g_pendingPrepareGate = {};
        return false;
    }
    CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.20f, scene));
    return true;
}

} // namespace corum::ranked

class $modify(CorumRankedLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        bool rankedOwned = false;
        corum::ranked::LevelPreparationContext context;
        bool metadataRecovery = false;
        bool modalHeld = false;
        bool optionsWarningLogged = false;
        int lastReadiness = -1;
        Ref<CCMenu> nativeOptionsMenu;
        Ref<GJGameLevel> optionsEditedLevel;
        int nativeOptionsZ = 0;
        bool returningToRanked = false;
        bool autoPlayScheduled = false;
        // Automatic start and song-gate completion share one scene-transition
        // latch. Physical Play/Space input remains blocked for Ranked.
        bool gameplayLaunchStarted = false;
        bool vanillaPlayActivation = false;
        int gameplayLaunchAttempts = 0;
        int gameplayLaunchBusyChecks = 0;
        SteadyClock::time_point nextGameplayLaunchAt {};
        bool songGateActive = false;
        SteadyClock::time_point lastReadySubmitAt {};
        bool songGateStartingPlay = false;
        bool songGateBypassed = false;
        bool mapDownloadRequested = false;
        bool mapFailureReported = false;
        SteadyClock::time_point gateOpenedAt {};
        SteadyClock::time_point countdownEndsAt {};
        SteadyClock::time_point mapDownloadStartedAt {};
        SteadyClock::time_point lastMapDownloadRequestAt {};
        SteadyClock::time_point songWaitStartedAt {};
        CCNode* gateOverlay = nullptr;
        CCLayerColor* gateIntroCover = nullptr;
        CCLayerColor* gateIntroPanel = nullptr;
        CCLabelBMFont* gateTitle = nullptr;
        CCLabelBMFont* gateStatus = nullptr;
        CCLabelBMFont* gateCountdown = nullptr;
        CCLabelBMFont* gateProgressLabel = nullptr;
        CCLayerColor* gateProgressFill = nullptr;
        float gateProgressWidth = 0.0f;
        CCLabelBMFont* introTitle = nullptr;
        CCLabelBMFont* introMap = nullptr;
        CCLabelBMFont* introCreator = nullptr;
        CCLabelBMFont* introStatus = nullptr;
        CCLabelBMFont* introCountdown = nullptr;
        CCLabelBMFont* introProgressLabel = nullptr;
        CCLayerColor* introProgressFill = nullptr;
        float introProgressWidth = 0.0f;
        float displayedPreparationProgress = 0.30f;
        bool introRevealStarted = false;
    };

    bool init(GJGameLevel* level, bool challenge) {
        auto const ranked = isCurrentRankedLevel(level);
        auto const context = corum::ranked::currentPreparationContext();
        auto& preparation = corum::ranked::RankedLevelPreparation::get();
        if (ranked) {
            corum::ranked::tracePreparedLevel("init-before", level, nullptr, this);
            if (!preparation.authorizes(level, context)) return false;
        }
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (ranked) {
            m_fields->rankedOwned = true;
            m_fields->context = context;
            corum::ranked::tracePreparedLevel("init-after", m_level, m_songWidget, this);
            if (!preparation.authorizes(m_level, context)) {
                preparation.invalidate(context);
                return false;
            }
            preparation.adopt(m_level, context);
            // Preserve vanilla's canonical Saved/SearchResult distinction.
            m_levelType = m_level->m_levelType;
        }
        return true;
    }

    bool isCurrentRankedScene() {
        return m_fields->rankedOwned && m_fields->context == corum::ranked::currentPreparationContext() &&
            isCurrentRankedLevel(m_level);
    }

    bool metadataReady() {
        return corum::ranked::RankedLevelPreparation::get().authorizes(m_level, m_fields->context);
    }

    bool modalBlocksPlay() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (auto* options = openLevelOptions(scene)) {
            if (isCurrentRankedScene() && options->m_level &&
                static_cast<int>(options->m_level->m_levelID) == m_fields->context.levelId) {
                m_fields->optionsEditedLevel = options->m_level;
            }
        } else if (m_fields->optionsEditedLevel) {
            // GD's own popup retains its original level. A native download may
            // replace LevelInfo's pointer while that popup is open. Transfer ONLY
            // the native user's two preferences, never online metadata or scores.
            if (isCurrentRankedScene() && m_level != m_fields->optionsEditedLevel.data()) {
                m_level->m_lowDetailModeToggled = m_fields->optionsEditedLevel->m_lowDetailModeToggled;
                m_level->m_disableShakeToggled = m_fields->optionsEditedLevel->m_disableShakeToggled;
                corum::ranked::tracePreparedLevel("options-preserved-after-replacement", m_level, m_songWidget, this);
            }
            m_fields->optionsEditedLevel = nullptr;
        }
        auto const open = hasOpenModal(scene);
        if (open != m_fields->modalHeld) {
            m_fields->modalHeld = open;
            corum::ranked::tracePreparedLevel(open ? "modal-hold" : "modal-resume", m_level, m_songWidget, this);
        }
        return open;
    }

    void recoverMetadata(char const* reason) {
        if (m_fields->metadataRecovery || m_fields->returningToRanked) return;
        corum::ranked::tracePreparedLevel(reason, m_level, m_songWidget, this);
        corum::ranked::RankedLevelPreparation::get().invalidate(m_fields->context);
        if (isCurrentRankedScene()) corum::ranked::RankedRuntime::get().setSongBypassAllowed(false);
        m_fields->metadataRecovery = true;
        m_fields->gameplayLaunchStarted = false;
        m_fields->songGateStartingPlay = false;
        m_fields->autoPlayScheduled = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked));
        schedule(schedule_selector(CorumRankedLevelInfoLayer::returnForMetadata), 0.20f);
    }

    void returnForMetadata(float) {
        if (m_fields->returningToRanked) return;
        // Do not destroy a live options/Jukebox popup or reset authoritative time.
        corum::ranked::RankedRuntime::get().tick();
        if (modalBlocksPlay()) return;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::returnForMetadata));
        teardownSongDownloadGate();
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void restoreVanillaOptionsMenu() {
        if (m_fields->nativeOptionsMenu) {
            m_fields->nativeOptionsMenu->setZOrder(m_fields->nativeOptionsZ);
            m_fields->nativeOptionsMenu = nullptr;
        }
    }

    void applyPrepareInteraction() {
        if (m_fields->gateIntroCover && !m_fields->introRevealStarted) {
            lockAllRankedPrepareInteraction(this);
        } else {
            lockInteractionToSong(this, m_songWidget);
        }
        auto* button = vanillaOptionsButton(this);
        auto* menu = button ? typeinfo_cast<CCMenu*>(button->getParent()) : nullptr;
        if (menu != m_fields->nativeOptionsMenu.data()) {
            restoreVanillaOptionsMenu();
            if (menu) {
                m_fields->nativeOptionsMenu = menu;
                m_fields->nativeOptionsZ = menu->getZOrder();
                log::info("[RankedPrepare] vanilla-options owner={} menu={} item={}",
                    static_cast<void*>(this), static_cast<void*>(menu), static_cast<void*>(button));
            }
        }
        auto const modalOpen = modalBlocksPlay();
        if (menu) {
            // Use the existing position and exact button. Do not expose siblings.
            menu->setZOrder(modalOpen ? m_fields->nativeOptionsZ : 1005);
            menu->setTouchEnabled(true);
            button->setEnabled(true);
        } else if (!m_fields->optionsWarningLogged) {
            m_fields->optionsWarningLogged = true;
            log::warn("[RankedPrepare] native settings-button identity unavailable; no replacement created");
        }
        // A modal attached directly to LevelInfo must also render above its cover.
        if (m_fields->gateOverlay) m_fields->gateOverlay->setVisible(!modalOpen);
        if (m_fields->gateIntroCover) m_fields->gateIntroCover->setVisible(!modalOpen);
    }

    void onEnterTransitionDidFinish() override {
        LevelInfoLayer::onEnterTransitionDidFinish();
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (!m_fields->rankedOwned) return;
        if (m_fields->metadataRecovery) {
            schedule(schedule_selector(CorumRankedLevelInfoLayer::returnForMetadata), 0.20f);
            return;
        }
        if (!isCurrentRankedScene() || !metadataReady()) {
            recoverMetadata("reentry-metadata-invalid");
            return;
        }

        SteadyClock::time_point countdownEndsAt {};
        if (
            rankedDownloadGateState(runtime.view().match.state) &&
            consumePrepareGateRequest(m_level, countdownEndsAt)
        ) {
            setupSongDownloadGate(countdownEndsAt);
            return;
        }

        if (rankedPlayingState(runtime.view().match.state) && rankedLevelEntryAllowed(runtime)) {
            scheduleAutoPlay(0.22f);
        } else {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.10f);
        }
    }

    void setupSongDownloadGate(SteadyClock::time_point countdownEndsAt) {
        if (m_fields->songGateActive) return;
        m_fields->songGateActive = true;
        corum::ranked::RankedRuntime::get().setSongBypassAllowed(false);
        m_fields->songGateBypassed = false;
        m_fields->gateOpenedAt = SteadyClock::now();
        m_fields->countdownEndsAt = std::max(countdownEndsAt, m_fields->gateOpenedAt);

        auto const size = CCDirector::sharedDirector()->getWinSize();

        // The real LevelInfo page is already alive underneath this UI and owns
        // vanilla map/update/song acquisition. A full-screen Ranked cover keeps
        // the map card readable during the first half of PREPARE; at five seconds
        // remaining it fades away to reveal that exact GD page.
        m_fields->gateOverlay = gatePanel({200.0f, 72.0f}, {size.width / 2.0f, size.height / 2.0f - 28.0f});
        m_fields->gateOverlay->setZOrder(1001);
        addChild(m_fields->gateOverlay, 1001);

        m_fields->gateTitle = gateLabel(rankedGateTitle(), 0.20f, {100.0f, 62.0f}, corum::ranked::ui::Cyan);
        m_fields->gateTitle->limitLabelWidth(184.0f, 0.20f, 0.14f);
        m_fields->gateOverlay->addChild(m_fields->gateTitle, 3);
        m_fields->gateCountdown = gateLabel(tr("starts-in", "STARTS IN") + "  10", 0.31f, {100.0f, 46.0f}, corum::ranked::ui::Gold);
        m_fields->gateCountdown->limitLabelWidth(184.0f, 0.31f, 0.21f);
        m_fields->gateOverlay->addChild(m_fields->gateCountdown, 3);
        m_fields->gateStatus = gateLabel(tr("preparing-map", "PREPARING MAP..."), 0.15f, {100.0f, 29.0f}, corum::ranked::ui::TextMuted);
        m_fields->gateStatus->limitLabelWidth(184.0f, 0.15f, 0.10f);
        m_fields->gateOverlay->addChild(m_fields->gateStatus, 3);

        constexpr float compactProgressWidth = 154.0f;
        auto* compactProgressBack = CCLayerColor::create({5, 12, 25, 245});
        compactProgressBack->setContentSize({compactProgressWidth, 8.0f});
        compactProgressBack->setPosition({11.0f, 8.0f});
        m_fields->gateOverlay->addChild(compactProgressBack, 3);
        m_fields->gateProgressFill = CCLayerColor::create({64, 224, 255, 245});
        m_fields->gateProgressFill->setContentSize({compactProgressWidth * 0.30f, 8.0f});
        m_fields->gateProgressFill->setPosition(compactProgressBack->getPosition());
        m_fields->gateProgressWidth = compactProgressWidth;
        m_fields->gateOverlay->addChild(m_fields->gateProgressFill, 4);
        m_fields->gateProgressLabel = gateLabel("30%", 0.17f, {183.0f, 12.0f}, corum::ranked::ui::Text);
        m_fields->gateOverlay->addChild(m_fields->gateProgressLabel, 4);

        auto const remainingAtOpen = std::chrono::duration<double>(
            m_fields->countdownEndsAt - SteadyClock::now()
        ).count();
        if (remainingAtOpen > 5.0) {
            m_fields->gateIntroCover = CCLayerColor::create({5, 10, 22, 255});
            m_fields->gateIntroCover->setContentSize(size);
            m_fields->gateIntroCover->setPosition(CCPointZero);
            m_fields->gateIntroCover->setCascadeOpacityEnabled(true);
            corum::ranked::ui::addBackdrop(m_fields->gateIntroCover, size);
            addChild(m_fields->gateIntroCover, 1002);

            auto const introPanelSize = CCSize {286.0f, 156.0f};
            m_fields->gateIntroPanel = CCLayerColor::create({0, 0, 0, 0});
            m_fields->gateIntroPanel->setContentSize(introPanelSize);
            m_fields->gateIntroPanel->setPosition({
                size.width / 2.0f - introPanelSize.width / 2.0f,
                size.height / 2.0f - introPanelSize.height / 2.0f,
            });
            m_fields->gateIntroPanel->setCascadeOpacityEnabled(true);
            m_fields->gateIntroCover->addChild(m_fields->gateIntroPanel, 5);
            m_fields->gateIntroPanel->addChild(gatePanel(
                introPanelSize,
                {introPanelSize.width / 2.0f, introPanelSize.height / 2.0f},
                corum::ranked::ui::Cyan
            ));

            m_fields->introTitle = gateLabel(rankedGateTitle(), 0.31f, {143.0f, 139.0f}, corum::ranked::ui::Cyan);
            m_fields->introTitle->limitLabelWidth(264.0f, 0.31f, 0.21f);
            m_fields->gateIntroPanel->addChild(m_fields->introTitle, 4);
            m_fields->introMap = gateLabel(
                m_level ? std::string(m_level->m_levelName.c_str()) : tr("selected-map", "SELECTED MAP"),
                0.39f,
                {143.0f, 111.0f},
                corum::ranked::ui::Text
            );
            m_fields->introMap->limitLabelWidth(258.0f, 0.39f, 0.24f);
            m_fields->gateIntroPanel->addChild(m_fields->introMap, 4);
            auto const creator = m_level && !m_level->m_creatorName.empty()
                ? fmt::format("BY {}", std::string(m_level->m_creatorName.c_str()))
                : tr("creator-loading", "CREATOR LOADING...");
            m_fields->introCreator = gateLabel(creator, 0.18f, {143.0f, 91.0f}, corum::ranked::ui::TextMuted);
            m_fields->introCreator->limitLabelWidth(252.0f, 0.18f, 0.12f);
            m_fields->gateIntroPanel->addChild(m_fields->introCreator, 4);
            m_fields->introCountdown = gateLabel(tr("starts-in", "STARTS IN") + "  10", 0.30f, {143.0f, 71.0f}, corum::ranked::ui::Gold);
            m_fields->gateIntroPanel->addChild(m_fields->introCountdown, 4);
            m_fields->introStatus = gateLabel(tr("preparing-map", "PREPARING MAP..."), 0.19f, {143.0f, 51.0f}, corum::ranked::ui::Cyan);
            m_fields->introStatus->limitLabelWidth(256.0f, 0.19f, 0.13f);
            m_fields->gateIntroPanel->addChild(m_fields->introStatus, 4);

            constexpr float introProgressWidth = 220.0f;
            auto* introProgressBack = CCLayerColor::create({5, 12, 25, 245});
            introProgressBack->setContentSize({introProgressWidth, 10.0f});
            introProgressBack->setPosition({18.0f, 26.0f});
            m_fields->gateIntroPanel->addChild(introProgressBack, 3);
            m_fields->introProgressFill = CCLayerColor::create({64, 224, 255, 245});
            m_fields->introProgressFill->setContentSize({introProgressWidth * 0.30f, 10.0f});
            m_fields->introProgressFill->setPosition(introProgressBack->getPosition());
            m_fields->introProgressWidth = introProgressWidth;
            m_fields->gateIntroPanel->addChild(m_fields->introProgressFill, 4);
            m_fields->introProgressLabel = gateLabel("PREPARING 30%", 0.18f, {143.0f, 13.0f}, corum::ranked::ui::Text);
            m_fields->gateIntroPanel->addChild(m_fields->introProgressLabel, 4);
        }

        applyPrepareInteraction();
        requestVanillaMapDownload();

        schedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate), 0.20f);
        updateSongDownloadGate(0.0f);
    }

    bool vanillaLevelReadyForPlay() {
        // m_levelString alone is not a sufficient launch boundary. Vanilla owns
        // the download/update decision that LevelInfoLayer::onPlay will use.
        return metadataReady() && mapReady(m_level) && !LevelInfoLayer::shouldDownloadLevel();
    }

    void requestVanillaMapDownload() {
        if (m_fields->metadataRecovery) return;
        if (m_fields->rankedOwned && (!isCurrentRankedScene() || !metadataReady())) {
            recoverMetadata("download-request-metadata-invalid");
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& state = runtime.view().match.state;
        if (
            m_fields->returningToRanked ||
            !isCurrentRankedScene() ||
            (!rankedDownloadGateState(state) && !rankedPlayingState(state)) ||
            m_fields->mapDownloadRequested ||
            vanillaLevelReadyForPlay()
        ) return;

        auto const now = SteadyClock::now();
        if (m_fields->mapDownloadStartedAt == SteadyClock::time_point{}) {
            m_fields->mapDownloadStartedAt = now;
        }
        m_fields->lastMapDownloadRequestAt = now;
        m_fields->mapDownloadRequested = true;

        log::info(
            "Ranked map requires vanilla download/update: level={} type={} hasPayload={} notDownloaded={}",
            m_level ? static_cast<int>(m_level->m_levelID) : 0,
            m_level ? static_cast<int>(m_level->m_levelType) : -1,
            m_level && !m_level->m_levelString.empty(),
            m_level ? m_level->m_levelNotDownloaded : true
        );

        // Important: do not call GameLevelManager::downloadLevel directly here.
        // Let the real on-screen LevelInfoLayer own its normal download delegate,
        // loading UI, saved-level replacement and song metadata initialization.
        corum::ranked::tracePreparedLevel("download-request", m_level, m_songWidget, this);
        LevelInfoLayer::downloadLevel();
    }

    void finishIntroReveal(float) {
        if (m_fields->gateIntroCover) {
            m_fields->gateIntroCover->removeFromParentAndCleanup(true);
            m_fields->gateIntroCover = nullptr;
        }
        m_fields->gateIntroPanel = nullptr;
        m_fields->introTitle = nullptr;
        m_fields->introMap = nullptr;
        m_fields->introCreator = nullptr;
        m_fields->introStatus = nullptr;
        m_fields->introCountdown = nullptr;
        m_fields->introProgressLabel = nullptr;
        m_fields->introProgressFill = nullptr;
        m_fields->introProgressWidth = 0.0f;
    }

    void revealExactLevelInfoPage() {
        if (!m_fields->gateIntroCover || m_fields->introRevealStarted) return;
        m_fields->introRevealStarted = true;
        lockInteractionToSong(this, m_songWidget);
        m_fields->gateIntroCover->stopAllActions();
        m_fields->gateIntroCover->runAction(CCFadeTo::create(0.25f, 0));
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::finishIntroReveal),
            0.26f
        );
    }

    void updatePreparationProgress(
        SteadyClock::time_point now,
        bool hasLevelPayload,
        bool levelReady,
        bool songReady,
        bool ownReady,
        bool opponentReady,
        std::string const& state
    ) {
        float target = 0.30f;
        std::string stage = tr("downloading-map", "DOWNLOADING MAP...");
        if (!levelReady) {
            auto const startedAt = m_fields->mapDownloadStartedAt == SteadyClock::time_point{}
                ? m_fields->gateOpenedAt
                : m_fields->mapDownloadStartedAt;
            auto const elapsed = std::max(0.0, std::chrono::duration<double>(now - startedAt).count());
            target += std::min(0.44f, static_cast<float>(elapsed) * 0.026f);
            if (hasLevelPayload) {
                target = std::max(target, 0.58f);
                stage = tr("updating-map", "UPDATING MAP...");
            }
        } else if (!songReady) {
            auto const startedAt = m_fields->songWaitStartedAt == SteadyClock::time_point{}
                ? now
                : m_fields->songWaitStartedAt;
            auto const elapsed = std::max(0.0, std::chrono::duration<double>(now - startedAt).count());
            target = 0.75f + std::min(0.16f, static_cast<float>(elapsed) * 0.025f);
            stage = tr("waiting-song", "WAITING FOR SONG...");
        } else {
            target = 0.92f;
            stage = tr("map-audio-ready", "MAP + AUDIO READY");
        }

        if (levelReady && (songReady || corum::ranked::RankedRuntime::get().songBypassAllowed())) {
            if (rankedPlayingState(state)) {
                target = 1.0f;
                stage = tr("starting", "STARTING...");
            } else if (ownReady && !opponentReady) {
                target = std::max(target, 0.96f);
                stage = tr("waiting-opponent", "WAITING FOR OPPONENT");
            } else if (ownReady && opponentReady) {
                target = std::max(target, 0.98f);
                stage = tr("starting", "STARTING...");
            } else {
                target = std::max(target, 0.94f);
                stage = tr("ready-to-start", "READY TO START");
            }
        }

        target = std::max(target, m_fields->displayedPreparationProgress);
        auto const delta = target - m_fields->displayedPreparationProgress;
        m_fields->displayedPreparationProgress = std::clamp(
            m_fields->displayedPreparationProgress + std::max(0.0025f, delta * 0.28f),
            0.0f,
            target
        );
        auto const percentage = static_cast<int>(std::round(
            m_fields->displayedPreparationProgress * 100.0f
        ));

        if (m_fields->gateProgressFill) {
            m_fields->gateProgressFill->setContentSize({
                m_fields->gateProgressWidth * m_fields->displayedPreparationProgress,
                8.0f
            });
        }
        if (m_fields->gateProgressLabel) {
            m_fields->gateProgressLabel->setString(fmt::format("{}%", percentage).c_str());
        }
        if (m_fields->introProgressFill) {
            m_fields->introProgressFill->setContentSize({
                m_fields->introProgressWidth * m_fields->displayedPreparationProgress,
                10.0f
            });
        }
        if (m_fields->introProgressLabel) {
            m_fields->introProgressLabel->setString(
                fmt::format("{} {}%", tr("preparing", "PREPARING"), percentage).c_str()
            );
        }
        if (m_fields->introStatus) m_fields->introStatus->setString(stage.c_str());
        if (m_fields->introMap && m_level) {
            m_fields->introMap->setString(std::string(m_level->m_levelName.c_str()).c_str());
        }
        if (m_fields->introCreator && m_level && !m_level->m_creatorName.empty()) {
            m_fields->introCreator->setString(
                fmt::format("BY {}", std::string(m_level->m_creatorName.c_str())).c_str()
            );
        }
    }

    void updateSongDownloadGate(float) {
        if (!m_fields->songGateActive || m_fields->metadataRecovery) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();

        if (!isCurrentRankedScene()) {
            returnToRanked(0.0f);
            return;
        }
        if (!metadataReady()) {
            recoverMetadata("gate-metadata-invalid");
            return;
        }

        auto const& match = runtime.view().match;
        if (m_fields->gateTitle) m_fields->gateTitle->setString(rankedGateTitle().c_str());
        if (m_fields->introTitle) m_fields->introTitle->setString(rankedGateTitle().c_str());
        auto const now = SteadyClock::now();
        auto const hasLevelPayload = mapReady(m_level);
        auto const levelReady = vanillaLevelReadyForPlay();
        auto const countdownRemaining = std::max(
            0,
            static_cast<int>(std::ceil(std::chrono::duration<double>(m_fields->countdownEndsAt - now).count()))
        );
        if (m_fields->introCountdown) {
            m_fields->introCountdown->setString(
                fmt::format("{}  {:02d}", tr("starts-in", "STARTS IN"), countdownRemaining).c_str()
            );
        }
        if (countdownRemaining <= 5) revealExactLevelInfoPage();

        // Base LevelInfoLayer may replace/rebuild its song widget when the level
        // payload arrives. Re-apply the interaction lock every tick so the newly
        // created vanilla song control is the only clickable UI.
        applyPrepareInteraction();

        if (!levelReady) {
            if (m_fields->gateStatus) {
                auto const statusText = hasLevelPayload
                    ? tr("updating-map", "UPDATING MAP...")
                    : tr("downloading-map", "DOWNLOADING MAP...");
                m_fields->gateStatus->setString(statusText.c_str());
                m_fields->gateStatus->setColor({95, 180, 255});
            }

            if (m_fields->mapDownloadStartedAt == SteadyClock::time_point{}) {
                requestVanillaMapDownload();
            } else {
                auto const mapElapsed = std::chrono::duration<double>(now - m_fields->mapDownloadStartedAt).count();
                auto const sinceRequest = std::chrono::duration<double>(now - m_fields->lastMapDownloadRequestAt).count();
                if (m_fields->mapDownloadRequested && sinceRequest >= 5.0) {
                    // If vanilla never produced a completion/failure callback, let
                    // LevelInfoLayer retry its own download rather than deadlocking.
                    m_fields->mapDownloadRequested = false;
                }
                if (!m_fields->mapDownloadRequested && sinceRequest >= 1.0) requestVanillaMapDownload();
                if (mapElapsed >= 30.0 && !m_fields->mapFailureReported) {
                    m_fields->mapFailureReported = true;
                    runtime.reportMapDownloadFailure();
                }
            }
        } else {
            if (m_fields->songWaitStartedAt == SteadyClock::time_point{}) {
                m_fields->songWaitStartedAt = now;
            }

            auto const ready = songsReady(m_level, m_songWidget);
            if (ready) {
                runtime.setSongBypassAllowed(false);
                m_fields->songGateBypassed = false;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString(tr("song-downloaded", "SONG DOWNLOADED").c_str());
                    m_fields->gateStatus->setColor({78, 232, 112});
                }
            } else if (now >= m_fields->countdownEndsAt) {
                // The visible Ranked countdown is the resource wait budget for
                // optional music. A broken/slow custom-song API must never keep a
                // fully downloaded playable map stuck after STARTS IN 00.
                // The vanilla widget remains alive, so audio may still finish
                // downloading while gameplay proceeds without it.
                runtime.setSongBypassAllowed(true);
                m_fields->songGateBypassed = true;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString(tr("starting-without-song", "SONG NOT READY - STARTING WITHOUT SONG").c_str());
                    m_fields->gateStatus->setColor({255, 216, 86});
                }
            } else if (m_fields->gateStatus) {
                m_fields->gateStatus->setString(tr("song-download-only", "ONLY SONG DOWNLOAD IS AVAILABLE").c_str());
                m_fields->gateStatus->setColor({95, 180, 255});
            }
        }

        auto const songReady = levelReady && songsReady(m_level, m_songWidget);
        auto const readyForStart = levelReady && (songReady || runtime.songBypassAllowed());
        auto const readiness = (metadataReady() ? 1 : 0) | (levelReady ? 2 : 0) |
            (songReady ? 4 : 0) | (runtime.songBypassAllowed() ? 8 : 0) | (m_fields->modalHeld ? 16 : 0);
        if (readiness != m_fields->lastReadiness) {
            m_fields->lastReadiness = readiness;
            log::info("[RankedPrepare] readiness context={} metadata={} geometry={} song={} bypass={} modal={}",
                corum::ranked::preparationContextText(m_fields->context), metadataReady(), levelReady,
                songReady, runtime.songBypassAllowed(), m_fields->modalHeld);
        }

        auto const ownReady = match.side == "A" ? match.readyA : match.readyB;
        auto const opponentReady = match.side == "A" ? match.readyB : match.readyA;
        updatePreparationProgress(
            now,
            hasLevelPayload,
            levelReady,
            songReady,
            ownReady,
            opponentReady,
            match.state
        );

        if (m_fields->gateCountdown) {
            if (countdownRemaining > 0) {
                m_fields->gateCountdown->setString(fmt::format("{}  {:02d}", tr("starts-in", "STARTS IN"), countdownRemaining).c_str());
            } else if (!levelReady) {
                m_fields->gateCountdown->setString(tr("waiting-map", "WAITING FOR MAP").c_str());
            } else if (!readyForStart) {
                m_fields->gateCountdown->setString(tr("waiting-song", "WAITING FOR SONG").c_str());
            } else if (rankedPlayingState(match.state)) {
                m_fields->gateCountdown->setString(tr("starting", "STARTING...").c_str());
            } else if (rankedPrepareState(match.state) && ownReady && !opponentReady) {
                m_fields->gateCountdown->setString(tr("waiting-opponent", "WAITING FOR OPPONENT").c_str());
            } else if (rankedPrepareState(match.state) && ownReady && opponentReady) {
                m_fields->gateCountdown->setString(tr("starting", "STARTING...").c_str());
            } else if (rankedPrepareState(match.state) && !runtime.view().error.empty()) {
                // Do not hide an authoritative Ready endpoint failure behind an
                // endless SENDING READY label. The bounded retry below remains
                // active, while the player can see that the server rejected or
                // failed the previous request.
                m_fields->gateCountdown->setString(tr("server-error-retrying", "SERVER ERROR - RETRYING").c_str());
            } else {
                m_fields->gateCountdown->setString(tr("sending-ready", "SENDING READY...").c_str());
            }
        }

        if (
            rankedPrepareState(match.state) &&
            now >= (
                runtime.isPlacement()
                    ? m_fields->countdownEndsAt - std::chrono::seconds(5)
                    : m_fields->countdownEndsAt
            ) &&
            readyForStart &&
            !ownReady
        ) {
            // Ready is server-idempotent. Do not make this a one-shot client event:
            // submitReady() can legitimately be skipped while another control
            // request is busy, and a transient HTTP failure must not strand the
            // LevelInfo gate at READY forever. Retry at a bounded cadence until
            // authority actually publishes PLAYING.
            auto const readyRetryDue =
                m_fields->lastReadySubmitAt == SteadyClock::time_point{} ||
                std::chrono::duration<double>(now - m_fields->lastReadySubmitAt).count() >= 1.0;
            if (readyRetryDue) {
                m_fields->lastReadySubmitAt = now;
                runtime.submitReady();
            }
            return;
        }

        if (rankedPlayingState(match.state)) {
            if (
                !readyForStart ||
                !rankedLevelEntryAllowed(runtime) ||
                now < m_fields->nextGameplayLaunchAt ||
                m_fields->songGateStartingPlay ||
                m_fields->gameplayLaunchStarted ||
                modalBlocksPlay()
            ) return;

            // Keep the gate and its polling callback alive until gameplay has
            // actually been armed. READY -> PLAYING can land between client/GD
            // frames; tearing the gate down before armCurrentLevelForGameplay()
            // succeeds removes the only retry loop and can strand this page at
            // READY forever.
            m_fields->songGateStartingPlay = true;
            if (!launchRankedGameplay()) {
                m_fields->songGateStartingPlay = false;
                return;
            }
            return;
        }

        if (
            match.state == "CANCELLED" ||
            match.state == "MATCH_RESULT" ||
            match.state == "ROUND_RESULT" ||
            match.state == "DEATHMATCH_RESULT" ||
            match.state == "MAP_RESULT" ||
            match.state == "STAGE_RESULT" ||
            match.state == "PLACEMENT_RESULT"
        ) {
            returnToRanked(0.0f);
        }
    }

    void teardownSongDownloadGate() {
        if (!m_fields->songGateActive) return;
        m_fields->songGateActive = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::updateSongDownloadGate));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::finishIntroReveal));

        // Restore vanilla LevelInfo interaction before invoking its onPlay path.
        unlockRankedPrepareInteraction(this);

        if (m_fields->gateOverlay) {
            m_fields->gateOverlay->removeFromParentAndCleanup(true);
            m_fields->gateOverlay = nullptr;
        }
        if (m_fields->gateIntroCover) {
            m_fields->gateIntroCover->removeFromParentAndCleanup(true);
            m_fields->gateIntroCover = nullptr;
        }
        restoreVanillaOptionsMenu();
        m_fields->gateIntroPanel = nullptr;
        m_fields->gateTitle = nullptr;
        m_fields->gateStatus = nullptr;
        m_fields->gateCountdown = nullptr;
        m_fields->gateProgressLabel = nullptr;
        m_fields->gateProgressFill = nullptr;
        m_fields->gateProgressWidth = 0.0f;
        m_fields->introTitle = nullptr;
        m_fields->introMap = nullptr;
        m_fields->introCreator = nullptr;
        m_fields->introStatus = nullptr;
        m_fields->introCountdown = nullptr;
        m_fields->introProgressLabel = nullptr;
        m_fields->introProgressFill = nullptr;
        m_fields->introProgressWidth = 0.0f;
    }

    void levelDownloadFinished(GJGameLevel* level) override {
        if (m_fields->rankedOwned) {
            corum::ranked::tracePreparedLevel("download-live-before", m_level, m_songWidget, this);
            corum::ranked::tracePreparedLevel("download-callback", level, nullptr, this);
            if (m_fields->metadataRecovery) return;
            if (!isCurrentRankedScene() || !level ||
                static_cast<int>(level->m_levelID) != m_fields->context.levelId) {
                recoverMetadata("download-stale-context");
                return;
            }
            modalBlocksPlay();
        }
        if (!m_fields->songGateActive) {
            LevelInfoLayer::levelDownloadFinished(level);
            if (m_fields->rankedOwned) {
                corum::ranked::tracePreparedLevel("download-live-after", m_level, m_songWidget, this);
                if (!metadataReady()) {
                    recoverMetadata("download-metadata-invalid");
                    return;
                }
                corum::ranked::RankedLevelPreparation::get().adopt(m_level, m_fields->context);
            }
            if (isCurrentRankedScene()) {
                m_levelType = m_level->m_levelType;
            }
            m_fields->mapDownloadRequested = false;
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (
                vanillaLevelReadyForPlay() &&
                rankedPlayingState(runtime.view().match.state) &&
                rankedLevelEntryAllowed(runtime)
            ) scheduleAutoPlay(0.10f);
            return;
        }

        // Reject a callback for anything except the exact server-selected playable
        // (alternate) Level ID before vanilla is allowed to replace m_level. This
        // keeps the original exact search result intact for a retry instead of
        // accidentally switching the Ranked page to an unrelated cached payload.
        auto const expectedLevelId = corum::ranked::RankedRuntime::get().currentLevelId();
        auto const validPayload =
            level &&
            expectedLevelId > 0 &&
            static_cast<int>(level->m_levelID) == expectedLevelId &&
            !level->m_levelString.empty() &&
            !level->m_levelNotDownloaded;
        if (!validPayload) {
            m_fields->mapDownloadRequested = false;
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString(tr("map-payload-mismatch", "MAP PAYLOAD MISMATCH - RETRYING").c_str());
                m_fields->gateStatus->setColor({255, 92, 92});
            }
            requestVanillaMapDownload();
            return;
        }

        // Native owns the payload replacement. Verify the resulting live metadata;
        // geometry completion alone is NOT proof that native rebuilt everything.
        LevelInfoLayer::levelDownloadFinished(level);
        corum::ranked::tracePreparedLevel("download-live-after", m_level, m_songWidget, this);
        if (!metadataReady()) {
            recoverMetadata("download-metadata-invalid");
            return;
        }
        corum::ranked::RankedLevelPreparation::get().adopt(m_level, m_fields->context);
        if (m_level) m_levelType = m_level->m_levelType;
        m_fields->mapDownloadRequested = false;
        if (!isCurrentRankedScene() || !vanillaLevelReadyForPlay()) {
            if (m_fields->gateStatus) {
                m_fields->gateStatus->setString(tr("map-update-incomplete", "MAP UPDATE INCOMPLETE - RETRYING").c_str());
                m_fields->gateStatus->setColor({255, 92, 92});
            }
            requestVanillaMapDownload();
            return;
        }
        m_fields->songWaitStartedAt = SteadyClock::now();
        applyPrepareInteraction();
        updateSongDownloadGate(0.0f);
    }

    void levelDownloadFailed(int response) override {
        if (m_fields->rankedOwned && !isCurrentRankedScene()) return;
        LevelInfoLayer::levelDownloadFailed(response);
        m_fields->mapDownloadRequested = false;
        if (!m_fields->songGateActive) return;
        if (m_fields->gateStatus) {
            auto const errorText = corum::ranked::i18n::isKorean()
                ? fmt::format("{} ({}) - {}", tr("map-download-failed", "MAP DOWNLOAD FAILED"), response, tr("retrying", "RETRYING"))
                : fmt::format("MAP DOWNLOAD FAILED ({}) - RETRYING", response);
            m_fields->gateStatus->setString(errorText.c_str());
            m_fields->gateStatus->setColor({255, 92, 92});
        }
        applyPrepareInteraction();
    }

    void levelUpdateFinished(GJGameLevel* level, UpdateResponse response) override {
        if (m_fields->rankedOwned) {
            corum::ranked::tracePreparedLevel("update-live-before", m_level, m_songWidget, this);
            corum::ranked::tracePreparedLevel("update-callback", level, nullptr, this);
            if (m_fields->metadataRecovery) return;
            if (!isCurrentRankedScene() ||
                (level && static_cast<int>(level->m_levelID) != m_fields->context.levelId)) {
                recoverMetadata("update-stale-context");
                return;
            }
            modalBlocksPlay();
        }
        LevelInfoLayer::levelUpdateFinished(level, response);
        if (!m_fields->rankedOwned) return;
        corum::ranked::tracePreparedLevel("update-live-after", m_level, m_songWidget, this);
        if (!metadataReady()) {
            recoverMetadata("update-metadata-invalid");
            return;
        }
        corum::ranked::RankedLevelPreparation::get().adopt(m_level, m_fields->context);
        m_levelType = m_level->m_levelType;
        if (m_fields->songGateActive) updateSongDownloadGate(0.0f);
        else scheduleAutoPlay(0.25f);
    }

    bool launchRankedGameplay() {
        if (m_fields->gameplayLaunchStarted || m_fields->returningToRanked || m_fields->metadataRecovery) return false;

        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            !isCurrentRankedScene() ||
            !rankedPlayingState(runtime.view().match.state) ||
            !rankedLevelEntryAllowed(runtime)
        ) {
            return false;
        }
        if (!metadataReady()) {
            recoverMetadata("launch-metadata-invalid");
            return false;
        }
        if (modalBlocksPlay()) return false;
        if (!vanillaLevelReadyForPlay()) {
            requestVanillaMapDownload();
            return false;
        }

        // Arm only when the real, visible Geometry Dash Play item exists and the
        // vanilla level page is no longer processing another action. This keeps
        // retries harmless: no synthetic scene and no attempt timestamp are
        // created while LevelInfo is still rebuilding after a map download.
        auto* nativePlayButton = vanillaPlayButton(m_playBtnMenu, m_playSprite);
        if (!nativePlayButton || m_isBusy) return false;

        auto const ready = songsReady(m_level, m_songWidget);
        if (!ready && !runtime.songBypassAllowed()) return false;
        if (!ready && runtime.songBypassAllowed()) m_level->m_showedSongWarning = true;

        m_fields->gameplayLaunchStarted = true;
        ++m_fields->gameplayLaunchAttempts;
        m_fields->gameplayLaunchBusyChecks = 0;
        m_fields->autoPlayScheduled = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked));
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch),
            4.0f
        );

        // Automatic entry must behave like a real press, not construct or
        // replace a PlayLayer scene behind Geometry Dash's state machine.
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton),
            0.0f
        );
        return true;
    }

    void activateRankedPlayButton(float) {
        if (!m_fields->gameplayLaunchStarted || m_fields->returningToRanked || m_fields->metadataRecovery) return;
        auto& runtime = corum::ranked::RankedRuntime::get();
        runtime.tick();
        if (!isCurrentRankedScene() || !metadataReady()) {
            recoverMetadata("activation-context-or-metadata-invalid");
            return;
        }
        auto const ready = songsReady(m_level, m_songWidget);
        if (!corum::ranked::preparationAllowsLaunch(isCurrentRankedScene(), metadataReady(),
            vanillaLevelReadyForPlay(), ready, runtime.songBypassAllowed(),
            rankedPlayingState(runtime.view().match.state) && rankedLevelEntryAllowed(runtime),
            modalBlocksPlay(), m_isBusy)) {
            // A popup/state change can land between scheduling and activation.
            // Release only the UI latch; no start timestamp has been armed yet.
            m_fields->gameplayLaunchStarted = false;
            m_fields->songGateStartingPlay = false;
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
            if (!m_fields->songGateActive) scheduleAutoPlay(0.25f);
            return;
        }
        auto* nativePlayButton = vanillaPlayButton(m_playBtnMenu, m_playSprite);
        if (!nativePlayButton || m_isBusy) return;

        // Preserve the callback Geometry Dash installed on the real Play item.
        // Replacing it with this hook's selector re-entered the Geode hook chain
        // twice on iOS, which was visible as two forwarded onPlay lines for one
        // activation. The original callback still traverses this hook normally.
        if (!nativePlayButton->m_pListener || !nativePlayButton->m_pfnSelector) {
            log::warn("Verified Ranked Play item has no native callback");
            return;
        }
        // Last possible boundary: a reconnecting spectator uses this same
        // verified native Play item but never arms a local Attempt. Normal play
        // still preserves the accepted time for real watchdog retries.
        auto const spectatorLaunch = runtime.canEnterCurrentLevelAsSpectator();
        if (!spectatorLaunch && !runtime.armCurrentLevelForGameplay()) {
            m_fields->gameplayLaunchStarted = false;
            m_fields->songGateStartingPlay = false;
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
            if (!m_fields->songGateActive) scheduleAutoPlay(0.25f);
            return;
        }
        corum::ranked::tracePreparedLevel("play-activate", m_level, m_songWidget, this);
        m_fields->vanillaPlayActivation = true;
        log::info(
            "Activating verified Ranked Play button: match={} level={} launch={} attempt={} node={}",
            corum::ranked::RankedRuntime::get().view().match.matchId,
            static_cast<int>(m_level->m_levelID),
            spectatorLaunch ? "spectator-read-only" : "gameplay",
            m_fields->gameplayLaunchAttempts,
            nativePlayButton->getID()
        );
        // activate() can synchronously begin a scene transition and run
        // onExit. Keep this layer alive until the activation callback returns so
        // clearing the one-shot bypass below never touches a released object.
        retain();
        nativePlayButton->setEnabled(true);
        nativePlayButton->activate();
        // If Geometry Dash stayed on this page, restore only the Play lock. Do
        // not recursively mutate every LevelInfo control while vanilla's
        // asynchronous play/update pipeline may still be running.
        if (m_fields->gameplayLaunchStarted) {
            nativePlayButton->setEnabled(false);
        }
        m_fields->vanillaPlayActivation = false;
        release();
    }

    void verifyGameplayLaunch(float) {
        if (!m_fields->gameplayLaunchStarted) return;

        // Vanilla may still be finishing its update/play steps. Re-pressing the
        // item while that work owns the page restarts the map spinner forever.
        if (m_isBusy && ++m_fields->gameplayLaunchBusyChecks <= 10) {
            log::debug(
                "Ranked vanilla Play is still processing (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
            scheduleOnce(
                schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch),
                1.0f
            );
            return;
        }

        if (m_isBusy) {
            log::warn(
                "Ranked vanilla Play/update remained busy; releasing launch latch (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            m_fields->mapDownloadRequested = false;
            m_fields->nextGameplayLaunchAt = SteadyClock::now() + std::chrono::seconds(1);
            if (m_fields->gateCountdown) {
                m_fields->gateCountdown->setString(tr("map-update-stalled", "MAP UPDATE STALLED - RETRYING").c_str());
                m_fields->gateCountdown->setColor({255, 92, 92});
            }
            if (!m_fields->songGateActive) scheduleAutoPlay(1.0f);
            return;
        }

        auto const updateRequired = !vanillaLevelReadyForPlay();

        if (updateRequired) {
            log::warn(
                "Ranked Play resolved to map download/update (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
        } else {
            log::warn(
                "Verified Ranked Play button did not leave LevelInfo (attempt {})",
                m_fields->gameplayLaunchAttempts
            );
        }
        m_fields->gameplayLaunchStarted = false;
        m_fields->vanillaPlayActivation = false;
        m_fields->songGateStartingPlay = false;
        m_fields->gameplayLaunchBusyChecks = 0;

        m_fields->nextGameplayLaunchAt = SteadyClock::now() + std::chrono::milliseconds(450);
        if (m_fields->gateCountdown) {
            auto const retryText = updateRequired
                ? tr("updating-map", "UPDATING MAP...")
                : tr("play-failed", "PLAY FAILED - RETRYING");
            m_fields->gateCountdown->setString(retryText.c_str());
            m_fields->gateCountdown->setColor(
                updateRequired ? ccColor3B{95, 180, 255} : ccColor3B{255, 92, 92}
            );
        }

        if (updateRequired) {
            m_fields->mapDownloadRequested = false;
            requestVanillaMapDownload();
        }

        if (!m_fields->songGateActive) {
            auto& runtime = corum::ranked::RankedRuntime::get();
            if (
                isCurrentRankedScene() &&
                rankedPlayingState(runtime.view().match.state) &&
                rankedLevelEntryAllowed(runtime)
            ) {
                scheduleAutoPlay(0.45f);
            } else {
                returnToRanked(0.0f);
            }
        }
    }

    void onExit() override {
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::returnForMetadata));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked));
        // onExit is the acknowledgement that Geometry Dash's own Play button
        // accepted the transition.
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton));
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
        auto const acceptedRankedLaunch = m_fields->gameplayLaunchStarted;
        if (acceptedRankedLaunch && m_fields->songGateActive) teardownSongDownloadGate();
        if (acceptedRankedLaunch) {
            // PlayLayer::onQuit returns to this same vanilla LevelInfo instance.
            // Clear only the UI launch latch here so onEnterTransitionDidFinish
            // can automatically activate the real Play button for the next legal
            // attempt. RankedRuntime remains the authority for timeout, result,
            // spectator, three-clear, and Deathmatch-attempt limits.
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            m_fields->autoPlayScheduled = false;
            m_fields->gameplayLaunchAttempts = 0;
            m_fields->nextGameplayLaunchAt = {};
        }
        LevelInfoLayer::onExit();
    }

    void scheduleAutoPlay(float delay) {
        if (
            m_fields->gameplayLaunchStarted ||
            m_fields->autoPlayScheduled ||
            m_fields->returningToRanked ||
            m_fields->metadataRecovery ||
            m_fields->songGateActive
        ) return;
        m_fields->autoPlayScheduled = true;
        scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked), delay);
    }

    void autoPlayRanked(float) {
        m_fields->autoPlayScheduled = false;
        if (m_fields->gameplayLaunchStarted) return;
        if (launchRankedGameplay()) return;

        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            isCurrentRankedScene() &&
            rankedPlayingState(runtime.view().match.state) &&
            rankedLevelEntryAllowed(runtime)
        ) {
            // The real button can be transiently absent/busy while vanilla
            // rebuilds LevelInfo. Stay automatic and retry instead of exposing a
            // manual Play fallback or abandoning the Ranked page.
            scheduleAutoPlay(0.25f);
            return;
        }
        returnToRanked(0.0f);
    }

    void onPlay(CCObject* sender) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (m_fields->rankedOwned &&
            (m_fields->metadataRecovery || !isCurrentRankedScene() || !metadataReady())) return;
        if (m_fields->vanillaPlayActivation) {
            log::debug(
                "Ranked LevelInfo onPlay forwarded to vanilla: match={} level={}",
                runtime.view().match.matchId,
                m_level ? static_cast<int>(m_level->m_levelID) : 0
            );
            LevelInfoLayer::onPlay(sender);
            return;
        }
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) {
            // Only song acquisition is legal during the gate, and repeated
            // Play/Space becomes inert once the Ranked scene launch has started.
            return;
        }
        if (isCurrentRankedScene() && rankedPlayingState(runtime.view().match.state)) {
            // Ranked owns automatic activation. A physical tap cannot create a
            // second attempt or bypass the authoritative entry checks.
            return;
        }
        LevelInfoLayer::onPlay(sender);
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            m_fields->rankedOwned &&
            (
                m_fields->songGateActive ||
                m_fields->gameplayLaunchStarted ||
                rankedPrepareState(runtime.view().match.state) ||
                rankedPlayingState(runtime.view().match.state)
            )
        ) {
            // LevelInfoLayer maps keyboard shortcuts such as Space directly to
            // Play. During Ranked prepare/launch that bypasses the disabled menu
            // item and can start a second, re-entrant scene transition while the
            // keyboard dispatcher is still iterating its delegates. iOS then
            // dereferences the released delegate (the observed SIGBUS). Ranked
            // already owns automatic Play activation, so keyboard shortcuts on
            // this transient page must be inert.
            return;
        }
        LevelInfoLayer::keyDown(key, timestamp);
    }

    void returnToRanked(float) {
        if (m_fields->returningToRanked) return;
        if (m_fields->rankedOwned && modalBlocksPlay()) {
            scheduleOnce(schedule_selector(CorumRankedLevelInfoLayer::returnToRanked), 0.20f);
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const currentLevel = isCurrentRankedScene();

        // Authority may replace ROUND_PLAYING with a Death Match (or another
        // Round/map) while the old LevelInfo selector is still waiting for a
        // transition. Do not let the old launch latch hold that obsolete scene
        // for the full watchdog interval; it otherwise stacks a second
        // LevelInfo page and starts two contexts against one visible screen.
        if (!currentLevel) {
            log::warn(
                "Discarding obsolete Ranked LevelInfo launch: shownLevel={} expectedLevel={} state={}",
                m_level ? static_cast<int>(m_level->m_levelID) : 0,
                runtime.currentLevelId(),
                runtime.view().match.state
            );
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::activateRankedPlayButton));
            unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
            m_fields->gameplayLaunchStarted = false;
            m_fields->vanillaPlayActivation = false;
            m_fields->songGateStartingPlay = false;
            m_fields->gameplayLaunchBusyChecks = 0;
            if (m_fields->songGateActive) teardownSongDownloadGate();
        } else if (m_fields->gameplayLaunchStarted) {
            return;
        }
        if (m_fields->songGateActive && rankedPrepareState(runtime.view().match.state)) return;
        if (currentLevel && rankedPlayingState(runtime.view().match.state) && rankedLevelEntryAllowed(runtime)) return;
        if (m_fields->returningToRanked) return;
        m_fields->returningToRanked = true;
        CCDirector::sharedDirector()->popScene();
    }

    void onBack(CCObject* sender) {
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) return;
        if (!isCurrentRankedScene()) {
            LevelInfoLayer::onBack(sender);
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            if (rankedLevelEntryAllowed(runtime)) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }

    void keyBackClicked() override {
        if (m_fields->songGateActive || m_fields->gameplayLaunchStarted) return;
        if (!isCurrentRankedScene()) {
            LevelInfoLayer::keyBackClicked();
            return;
        }
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (rankedPlayingState(runtime.view().match.state)) {
            if (rankedLevelEntryAllowed(runtime)) scheduleAutoPlay(0.05f);
            else returnToRanked(0.0f);
            return;
        }
        returnToRanked(0.0f);
    }
};
