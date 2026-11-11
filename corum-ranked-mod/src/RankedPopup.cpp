#include <array>
#include "ui/SurvivalBadgeCollection.hpp"
#include "ui/SurvivalRoundBadge.hpp"
#include "replay/HistoryReplayButton.hpp"
#include "ui/PublicPlayerProfile.hpp"
#include "ui/LobbyPopulationView.hpp"
#include "RankedPopup.hpp"
#include "domain/HistoryPresentation.hpp"
#include "domain/SurvivalVotePresentation.hpp"
#include "domain/SurvivalEliminationEffects.hpp"
#include "HistoryLevelLookup.hpp"
#include "ui/MapPoolVisuals.hpp"
#include "ui/RankedGuideCarousel.hpp"
#include "SurvivalThumbnailProvider.hpp"
#include "ui/ModeLobbyMotion.hpp"
#include "ui/SoloRoundIntro.hpp"
#include "ui/ModeLobbySurfaceFade.hpp"
#include "ui/LobbyHover.hpp"

#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "ui/RankedModUpdateManager.hpp"
#include "ui/RankedUpdateChannelPopup.hpp"
#include "DebugBotPopup.hpp"
#include "debug/DebugMenuPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#endif
#include "RankedSongGate.hpp"
#include "RankedLevelPreparation.hpp"
#include "SurvivalRuntime.hpp"
#include "RankedMapDetails.hpp"
#include "RankedRoundPreparation.hpp"
#include "domain/PlacementStageMerge.hpp"
#include "domain/AchievementPresentation.hpp"
#include "domain/AchievementScreenLayout.hpp"
#include "ui/AchievementEffects.hpp"
#include "domain/TransitionTrace.hpp"
#include "ui/RankBadgeAssets.hpp"
#include "ui/RankTransitionLayer.hpp"
#include "ui/ResultScoreLayer.hpp"
#include "ui/StandingVisuals.hpp"
#include "ui/RankedLeaderboardPopup.hpp"
#include "ui/AchievementCardFrame.hpp"
#include "ui/RankedMapPoolView.hpp"
#include "ui/RankedEliminationFxPopup.hpp"
#include "ui/RankedAchievementGradePopup.hpp"
#include "ui/RankedTapScroll.hpp"
#include "ui/RankedTierGuidePopup.hpp"
#include "ui/HudEditorPopup.hpp"
#include "ui/PlacementGradeBadge.hpp"
#include "ui/PlayerIcon.hpp"
#include "ui/SeasonTimer.hpp"
#include "ui/TierProgressBar.hpp"
#include "ui/LobbyScoreTransition.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"
#include "ui/RankedInput.hpp"
#include "ui/RankedSettingsPopup.hpp"
#include "ui/RankedHistoryFilterPopup.hpp"
#include "ui/RankedJukeboxPopup.hpp"
#include "ui/RankedPlayerInfoPopup.hpp"
#include "domain/PlayerInfoPresentation.hpp"
#include "ui/RankedSessionConflictPopup.hpp"
#include "ui/RankedConfirmPopup.hpp"
#include "ui/SurvivalPlayerInfoPopup.hpp"
#include "ui/RankedModeDisabledPopup.hpp"
#include "ui/MapDifficultyBadge.hpp"
#include "ui/RankedMapHeading.hpp"
#include "domain/ScoreDisplay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <tuple>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;
using corum::ranked::DeathmatchSummaryView;
using corum::ranked::EnvironmentModIssueView;
using corum::ranked::HistoryMatchView;
using corum::ranked::MatchView;
using corum::ranked::RankedRuntime;
using corum::ranked::RankedAudioManager;
using corum::ranked::RankedAudioMode;
using corum::ranked::RankedResourceDownloadState;
using corum::ranked::RankedProfileView;
using corum::ranked::RankedMapView;
using corum::ranked::RoundSummaryView;
using corum::ranked::RuntimeStage;
using corum::ranked::RuntimeView;
using corum::ranked::ui::RankTransitionLayer;
using corum::ranked::ui::RankTransitionType;

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr ccColor3B kPanelColor = corum::ranked::ui::Surface;
constexpr ccColor3B kAccent = corum::ranked::ui::Accent;
constexpr ccColor3B kGreen = corum::ranked::ui::Green;
constexpr ccColor3B kRed = corum::ranked::ui::Red;
constexpr ccColor3B kGold = corum::ranked::ui::Gold;
constexpr ccColor3B kSteel = corum::ranked::ui::Steel;
constexpr ccColor3B kDeepPanel = corum::ranked::ui::Surface;
constexpr ccColor3B kDeepPanel2 = corum::ranked::ui::SurfaceRaised;
constexpr ccColor3B kSideRed = corum::ranked::ui::Red;

std::string tr(std::string_view key, std::string_view english) {
    return corum::ranked::i18n::text(key, english);
}

std::size_t utf8Length(std::string const& value) {
    return static_cast<std::size_t>(std::count_if(
        value.begin(),
        value.end(),
        [](unsigned char byte) { return (byte & 0xc0) != 0x80; }
    ));
}

std::size_t utf8PrefixBytes(std::string const& value, std::size_t codePoints) {
    std::size_t seen = 0;
    std::size_t offset = 0;
    while (offset < value.size() && seen < codePoints) {
        ++offset;
        while (
            offset < value.size() &&
            (static_cast<unsigned char>(value[offset]) & 0xc0) == 0x80
        ) {
            ++offset;
        }
        ++seen;
    }
    return offset;
}

std::string shorten(std::string value, std::size_t maximum) {
    if (utf8Length(value) <= maximum) return value;
    if (maximum <= 3) return value.substr(0, utf8PrefixBytes(value, maximum));
    value.resize(utf8PrefixBytes(value, maximum - 3));
    return value + "...";
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string displayBanner(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    return value;
}

std::string formatScore(double value,bool deathmatch=false) {
    return corum::ranked::rankedScoreText(value,deathmatch);
}

std::string formatPlacementScore(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

std::string environmentModIdentity(EnvironmentModIssueView const& issue) {
    if (issue.displayName.empty() || issue.displayName == issue.id) return issue.id;
    return fmt::format("{} ({})", issue.displayName, issue.id);
}

std::string joinEnvironmentMods(
    std::vector<EnvironmentModIssueView> const& issues,
    bool includeVersions
) {
    std::string result;
    for (auto const& issue : issues) {
        if (!result.empty()) result += "  |  ";
        result += environmentModIdentity(issue);
        if (!includeVersions) continue;

        if (!issue.installedVersion.empty()) {
            result += fmt::format(
                " · {} {}",
                tr("installed-version", "INSTALLED"),
                issue.installedVersion
            );
        }
        if (issue.minimumVersion || issue.maximumVersion) {
            result += fmt::format(" · {} ", tr("required-version", "REQUIRED"));
            if (issue.minimumVersion) {
                result += *issue.minimumVersion + " " + tr("version-or-newer", "OR NEWER");
            }
            if (issue.minimumVersion && issue.maximumVersion) result += " / ";
            if (issue.maximumVersion) {
                result += *issue.maximumVersion + " " + tr("version-or-older", "OR OLDER");
            }
        }
    }
    return result;
}

std::string cbfSettingsText(std::vector<std::string> const& issues) {
    std::vector<std::string> settings;
    for (auto const& issue : issues) {
        constexpr std::string_view prefix = "CBF_SETTING_MISMATCH:";
        if (!issue.starts_with(prefix)) continue;
        auto const key = issue.substr(prefix.size());
        if (key == "soft-toggle") settings.push_back("Soft Toggle");
        else if (key == "click-on-steps") settings.push_back("Click on Steps");
        else if (key == "physics-bypass") settings.push_back("Physics Bypass");
        else settings.push_back(key);
    }
    if (settings.empty()) return {};

    std::string result = tr("cbf-setting-off", "TURN OFF THESE CBF SETTINGS") + ": ";
    for (auto const& setting : settings) {
        if (!result.ends_with(": ")) result += ", ";
        result += setting;
    }
    return result;
}

std::string scoreComparison(double ownScore, double opponentScore,bool deathmatch=false) {
    return fmt::format(fmt::runtime(tr("own-versus-score","YOUR SCORE  {}     OPPONENT  {}")),
        formatScore(ownScore,deathmatch),formatScore(opponentScore,deathmatch));
}

ccColor3B tierColor(std::string const& tier) {
    return corum::ranked::ui::tierColor(tier);
}

std::optional<RankTransitionType> authoritativeTransitionType(
    corum::ranked::RatingEventView const& event,
    RankedProfileView const& before,
    RankedProfileView const& after
) {
    auto const oldTier = upper(before.displayedTier);
    auto const newTier = upper(after.displayedTier);
    if (oldTier == newTier) return std::nullopt;

    // The server event is the sole authority for the transition type. These
    // checks only reject malformed presentation data; they never calculate or
    // change the resulting tier.
    if (event.placementCompleted && oldTier == "UNRANKED" && newTier != "UNRANKED") {
        return RankTransitionType::Placement;
    }
    if (corum::ranked::goldRankTier(newTier) && corum::ranked::rankTierOrder(newTier)>corum::ranked::rankTierOrder(oldTier) && oldTier != "UNRANKED") {
        return RankTransitionType::Promotion;
    }
    if (event.demoted && (corum::ranked::goldRankTier(oldTier) || corum::ranked::rankTierBase(oldTier)!=corum::ranked::rankTierBase(newTier)) && oldTier != "UNRANKED" && newTier != "UNRANKED") {
        return RankTransitionType::Demotion;
    }
    return std::nullopt;
}

std::pair<std::string, ccColor3B> ratingEventText(corum::ranked::RatingEventView const& event) {
    if (event.placementCompleted) return {tr("placed", "PLACED"), kGreen};
    if (event.promotionSucceeded) return {tr("promoted", "PROMOTED"), kGreen};
    if (event.promotionFailed) return {tr("promotion-failed", "PROMOTION FAILED"), kRed};
    if (event.promotionStarted) return {tr("promotion-match", "PROMOTION MATCH"), ccc3(145, 255, 125)};
    if (event.demoted) return {tr("demoted", "DEMOTED"), kRed};
    return {"", ccc3(255, 255, 255)};
}

ccColor3B colorFromHex(int value) {
    return ccc3(
        static_cast<GLubyte>((value >> 16) & 0xff),
        static_cast<GLubyte>((value >> 8) & 0xff),
        static_cast<GLubyte>(value & 0xff)
    );
}

ccColor3B colorFromCssHex(std::string const& value, ccColor3B fallback) {
    if (value.size() != 7 || value.front() != '#') return fallback;
    try {
        return colorFromHex(std::stoi(value.substr(1), nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

ccColor3B rarityColor(std::string const& rarity) {
    return corum::ranked::ui::achievementColor(corum::ranked::achievementRarityTier(rarity));
}

std::string achievementTitleText(corum::ranked::AchievementTitleView const& title) {
    if (!title.active) return {};
    auto const name = corum::ranked::achievementScopedName(title.scope, corum::ranked::i18n::text("achievement."+title.achievementId+".name",corum::ranked::i18n::isKorean()?title.nameKo:title.nameEn), corum::ranked::i18n::isKorean());
    return title.displayLabel.empty() ? name : fmt::format("{} - {}", name, title.displayLabel);
}

ccColor3B achievementTitleColor(corum::ranked::AchievementTitleView const& title) {
    if (!title.displayColor.empty()) return colorFromCssHex(title.displayColor, kAccent);
    return rarityColor(title.rarity);
}

// Corum difficulty/rating palette. Keep this local to Corum Ranked; Corum
// Integration remains a separate mod and is not linked into this target.
ccColor3B difficultyColor(std::string rating) {
    while (!rating.empty() && std::isspace(static_cast<unsigned char>(rating.front()))) rating.erase(rating.begin());
    while (!rating.empty() && std::isspace(static_cast<unsigned char>(rating.back()))) rating.pop_back();
    if (rating == "20") rating = "20.0";

    static std::unordered_map<std::string, int> const colors {
        {"Tiny", 0xff6fff}, {"0", 0xe8eaed}, {"1", 0x0099ff}, {"2", 0x00bbff},
        {"3", 0x00ddff}, {"4", 0x00ffff}, {"5", 0x00ffaa}, {"6", 0x00ff00},
        {"7", 0x66ff00}, {"8", 0x99ff00}, {"9", 0xccff00}, {"10", 0xffff00},
        {"11", 0xffdd00}, {"12", 0xffcc00}, {"13", 0xffaa00}, {"14", 0xff8800},
        {"15", 0xff6600}, {"16", 0xff4400}, {"17", 0xff0000}, {"18", 0xcc0000},
        {"18+", 0xa61c00}, {"19", 0x660000}, {"19+", 0x460c00}, {"20.0", 0x360900},
        {"20.1", 0x240600}, {"20.2", 0x130400}, {"20.3", 0x000000}, {"20.4", 0x0a031f},
        {"20.5", 0x11072d}, {"20.6", 0x180b3b}, {"20.7", 0x180b3b}, {"20.8", 0x261358},
        {"20.9", 0x2d1766}, {"21", 0x351c75}, {"21+", 0x4511c9}, {"-1", 0x4c1130},
        {"-2", 0x434343}, {"UnVF", 0x4f71a3},
    };

    auto const found = colors.find(rating);
    return colorFromHex(found == colors.end() ? 0xffffff : found->second);
}

CCLabelBMFont* makeLabel(
    std::string const& text,
    float scale,
    CCPoint position,
    ccColor3B color = {255, 255, 255},
    char const* font = nullptr,
    CCPoint anchor = {0.5f, 0.5f}
) {
    // SemiBold is the default UI voice. Medium remains available for deliberately
    // quiet helper copy, while display/critical values opt into Paperlogy Black.
    auto weight = corum::ranked::ui::FontWeight::SemiBold;
    if (font) {
        auto const role = std::string_view(font);
        if (role == "medium") weight = corum::ranked::ui::FontWeight::Medium;
        else if (role == "semibold") weight = corum::ranked::ui::FontWeight::SemiBold;
        else if (role == "black" || role == "display") weight = corum::ranked::ui::FontWeight::Black;
        else weight = corum::ranked::ui::FontWeight::ExtraBold;
    }
    auto* result = corum::ranked::ui::menuLabel(text, scale, position, color, weight);
    if (result) result->setAnchorPoint(anchor);
    return result;
}

// Q11: free play while the mod update downloads. The player leaves Ranked;
// this watcher keeps polling outside any scene and restarts the game, as
// the update screen announced, once the new package is installed.
class ModUpdateRestartWatcher final : public CCObject {
    bool m_fired = false;
    double m_waited = 0.0;
public:
    static void arm() {
        static ModUpdateRestartWatcher* armed = nullptr;
        if (armed) return;
        armed = new ModUpdateRestartWatcher();
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
            schedule_selector(ModUpdateRestartWatcher::tick), armed, 0.25f, false);
    }
    void tick(float dt) {
        using corum::ranked::ui::ModUpdateState;
        auto& manager = corum::ranked::ui::RankedModUpdateManager::get();
        if (manager.view().state != ModUpdateState::RestartRequired) return;
        if (!m_fired) {
            m_fired = true;
            Notification::create(corum::ranked::i18n::text("mod-update-installed-restart", "Update installed. Restarting now..."),
                NotificationIcon::Success, 1.5f)->show();
            return;
        }
        m_waited += dt;
        if (m_waited >= 1.25) manager.restart();
    }
};

// Q12: free play while resources download. Downloads belong to their
// managers and continue outside Ranked; this watcher says when all are ready.
class ResourceReadyWatcher final : public CCObject {
public:
    static void arm() {
        static ResourceReadyWatcher* armed = nullptr;
        if (!armed) armed = new ResourceReadyWatcher();
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(schedule_selector(ResourceReadyWatcher::tick), armed);
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(schedule_selector(ResourceReadyWatcher::tick), armed, 0.5f, false);
    }
    void tick(float) {
        if (!RankedAudioManager::get().resourcesReady() || corum::ranked::ui::RankAssetPackManager::get().requiresDownload()) return;
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(schedule_selector(ResourceReadyWatcher::tick), this);
        Notification::create(corum::ranked::i18n::text("all-resources-ready", "ALL RESOURCES READY"), NotificationIcon::Success, 2.0f)->show();
    }
};

CCNode* makePanel(CCSize size, CCPoint position, ccColor3B color = kPanelColor, GLubyte opacity = 235) {
    return corum::ranked::ui::surface(size, position, color, kDeepPanel2, opacity, false);
}

CCNode* makeNeonPanel(
    CCSize size,
    CCPoint position,
    ccColor3B accent = kAccent,
    ccColor3B fill = kDeepPanel,
    GLubyte fillOpacity = 245
) {
    // Board panel (.panel.rule): a flat fill with a thin top rule in the
    // accent. The frame takes the fill colour and the drop shadow is off.
    auto* panel = corum::ranked::ui::surface(size, position, accent, fill, fillOpacity, false);
    if (auto* border = typeinfo_cast<CCLayerColor*>(panel->getChildByTag(corum::ranked::ui::SurfaceBorderTag))) border->setColor(fill);
    if (auto* shadow = typeinfo_cast<CCLayerColor*>(panel->getChildByTag(corum::ranked::ui::SurfaceShadowTag))) shadow->setOpacity(0);
    if (auto* rule = typeinfo_cast<CCLayerColor*>(panel->getChildByTag(corum::ranked::ui::SurfaceRuleTag))) rule->setOpacity(255);
    return panel;
}

CCNode* makeTextPlate(
    std::string const& text,
    CCSize size,
    CCPoint position,
    ccColor3B accent,
    float scale = 0.24f,
    ccColor3B textColor = {255, 255, 255},
    char const* font = nullptr,
    CCLabelBMFont** outLabel = nullptr
) {
    auto* node = makeNeonPanel(size, position, accent, kDeepPanel2, 242);
    auto* label = makeLabel(text, scale, {size.width / 2.0f, size.height / 2.0f}, textColor, font);
    corum::ranked::ui::fitMenuLabelWidth(label, size.width - 12.0f, scale, std::max(0.12f, scale * 0.65f));
    node->addChild(label, 4);
    if (outLabel) *outLabel = label;
    return node;
}

// The broadcast action button. By default it is the primary action: a slab
// in the signal colour with an ink caption. `secondary` is the quiet outlined
// variant, `danger` the outlined red one, `selected` the filled side of a toggle.
CCNode* makeActionButtonVisual(
    std::string const& text,
    bool danger = false,
    bool selected = false,
    bool secondary = false
) {
    auto const width = std::clamp(46.0f + static_cast<float>(text.size()) * 7.0f, 104.0f, 178.0f);
    constexpr float height = 34.0f;
    auto const filled = !danger && (selected || !secondary);
    auto const accent = danger ? kRed : (filled ? corum::ranked::ui::AccentFill : kSteel);

    auto* root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({width, height});

    auto* panel = corum::ranked::ui::slab(
        {width, height},
        {width / 2.0f, height / 2.0f},
        accent,
        !filled,
        danger ? ccc3(39, 15, 28) : corum::ranked::ui::SurfaceRaised
    );
    root->addChild(panel, 0);

    auto* caption = corum::ranked::ui::menuLabel(
        text,
        0.62f,
        {width / 2.0f, height / 2.0f},
        filled ? corum::ranked::ui::AccentInk : (danger ? ccc3(255, 224, 229) : corum::ranked::ui::Text),
        true
    );
    corum::ranked::ui::fitMenuLabelWidth(caption, width - 20.0f, 0.62f, 0.30f);
    root->addChild(caption, 5);
    return root;
}

std::string resourceKey(MatchView const& match) {
    auto const& placement = RankedRuntime::get().view().placement;
    return fmt::format(
        "{}:{}:{}:{}:{}:{}:{}",
        match.matchId,
        match.roundNumber,
        match.deathmatchSequence,
        match.currentMap ? match.currentMap->levelId : 0,
        match.placement,
        match.placement ? placement.currentStageIndex : 0,
        match.placement ? placement.currentMapIndex : 0
    );
}

class CorumRankedLayer final : public CCLayerColor {
    enum class Page {
        Live,
        PlacementPoolSelection,
        PlacementSkipConfirm,
        HistoryList,
        HistoryDetail,
        Achievements,
        MapPool,
    };

    CCLayerColor* m_fadeRoot = nullptr;
    CCNode* m_root = nullptr;
    CCMenu* m_menu = nullptr;
    CCMenu* m_settingsMenu = nullptr;
    CCLabelBMFont* m_settingsLabel = nullptr;
    std::optional<WeakRef<corum::ranked::ui::RankedSettingsPopup>> m_settings;
    std::optional<WeakRef<corum::ranked::ui::RankedLeaderboardPopup>> m_leaderboard;
    std::optional<WeakRef<corum::ranked::ui::RankedMapPoolView>> m_mapPoolView;
    std::optional<WeakRef<corum::ranked::ui::RankedEliminationFxPopup>> m_eliminationFx;
    std::optional<WeakRef<corum::ranked::ui::RankedAchievementGradePopup>> m_achievementGrade;
    std::optional<WeakRef<corum::ranked::ui::RankedJukeboxPopup>> m_jukebox;
    std::optional<WeakRef<FLAlertLayer>> m_jukeboxNotice;
    std::optional<WeakRef<corum::ranked::ui::RankedPlayerInfoPopup>> m_playerInfo;
    std::optional<WeakRef<corum::ranked::ui::RankedSessionConflictPopup>> m_sessionConflict;
    std::optional<WeakRef<corum::ranked::ui::RankedModeDisabledPopup>> m_modeNotice;
    bool m_modeNoticeOpen = false;
    unsigned m_modeNoticeGeneration = 0;
    bool m_accountConflictShown = false;
    CCMenuItemSpriteExtra* m_jukeboxButton = nullptr;
    Page m_page = Page::Live;
    corum::ranked::ModeLobbyTransition m_modeTransition;
    bool m_returnToModesAfterQueue = false;
    CCLayerColor *m_lobbySurvival = nullptr, *m_lobbySolo = nullptr, *m_lobbyPanel = nullptr;
    CCMenu* m_lobbyToolbar = nullptr;
    CCLayerColor* m_lobbyContents = nullptr;
    // Queue expansion: the selected profile card widens over the action card.
    bool m_queueShown = false, m_queueAnimating = false;
    CCLayerColor* m_queueContents = nullptr;
    CCMenu* m_queueMenu = nullptr;
    CCLabelBMFont *m_queuePlayers = nullptr, *m_queueWait = nullptr, *m_queueMinimum = nullptr, *m_queueHint = nullptr, *m_queueHeading = nullptr;
    std::vector<CCLayerColor*> m_queueSlots;
    CCLayerColor* m_queueFillBar = nullptr; float m_queueFillWidth = 0.0f; int m_queueSeatMaximum = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_randomMapRequest; bool m_randomMapBusy = false;
    // Queue arena: radar ring (cyan while searching, gold on MATCH FOUND) and
    // the anonymous opponent slot. Names and icons appear only in the intro.
    CCNode *m_queueRingCyan = nullptr, *m_queueRingGold = nullptr, *m_queueSweep = nullptr;
    CCLabelBMFont *m_queueCenter = nullptr, *m_queueOpponentMark = nullptr, *m_queueOpponentLabel = nullptr;
    CCNode* m_queueOpponentBox = nullptr;
    // Solo radar: search circle + detected players, redrawn as the range widens.
    CCDrawNode* m_queueRadar = nullptr;
    CCLabelBMFont* m_queueRangeLabel = nullptr;
    bool m_queueSurvivalArena = false;
    float m_queueSweepAngle = 0.0f;
    SteadyClock::time_point m_radarFrameAt{};
    struct RadarBlip { float radius = 0, angle = 0, age = 0, fade = 0, matchedFor = 0; bool leaving = false, matched = false; };
    // Season timers: one shared under the title, or one per mode card. Ref
    // keeps a detached node valid; only timers still in the scene update.
    Ref<corum::ranked::ui::SeasonTimer> m_seasonShared, m_seasonSolo, m_seasonSurvival;
    std::map<std::string, RadarBlip> m_radarBlips;
    CCMenuItemSpriteExtra *m_queueLeave = nullptr, *m_queueFill = nullptr;
    float m_statusTextWidth = 0;
    corum::ranked::ui::LobbyHover* m_lobbyHover = nullptr;
    std::string m_achievementScope = "SHARED";
    corum::ranked::HistoryScope m_historyScope = corum::ranked::HistoryScope::All;
    std::size_t m_historyIndex = 0;
    std::string m_historyOrder="NEWEST",m_historyResult="ALL",m_historyRound,m_historySelectedId;
    int m_historyTabPage=0;
    ScrollLayer* m_historyListScroll=nullptr;
    ScrollLayer* m_historyBoardScroll=nullptr;
    struct HistoryProfileHit {WeakRef<CCNode> node;corum::ranked::SurvivalStanding player;std::string match;bool survival;};
    std::vector<HistoryProfileHit> m_historyProfiles;
    struct HistoryIcon {WeakRef<CCNode> holder;corum::ranked::SurvivalStanding player;};
    std::vector<HistoryIcon> m_historyIcons;
    std::uint64_t m_historyUiGeneration=0;
    std::optional<WeakRef<corum::ranked::ui::RankedConfirmPopup>> m_historyReconnect;
    std::optional<WeakRef<corum::ranked::ui::RankedHistoryFilterPopup>> m_historyFilterPopup;
    bool m_historyReconnectShown=false;
    std::optional<WeakRef<corum::ranked::ui::RankedUpdateChannelPopup>> m_updateChannelPopup;
    std::optional<float> m_historyListOffset;
    corum::ranked::SurvivalSelectionContext m_historyThumbnailContext;
    std::uint64_t m_historyThumbnailGeneration=0;
    CCSprite* m_historyThumbnail=nullptr;CCNode* m_historyThumbnailPlaceholder=nullptr;
    CCSize m_historyThumbnailSize;
    Ref<CCTexture2D> m_historyTexture;
    std::size_t m_achievementPage = 0;
    std::string m_titleSelectorAchievement;
    std::string m_phaseKey;
    std::string m_resourceKey;
    std::string m_lastMatchId;
    // Sentinel for a confirmed/pending NO BAN (the server stores a null ban).
    static constexpr char const* kNoBanChoice = "__no_ban__";
    std::string m_selectedBan;
    std::string m_pendingBan;
    std::string m_localMessage;
    int m_selectedPlacementStageCount = 0;
    int m_selectedPlacementPool = 0;
    bool m_placementPoolStep = false;
    SteadyClock::time_point m_phaseStartedAt {};
    std::vector<int> m_songIds;
    bool m_readySent = false;
    SteadyClock::time_point m_lastReadySubmitAt {};
    bool m_matchFoundReadySent = false;
    bool m_enteringSongGate = false;
    bool m_enteringLevel = false;
    std::string m_levelLookupError;
    bool m_levelLookupBusy = false;
    SteadyClock::time_point m_levelLookupStartedAt {};
    // Set only while a vanilla LevelInfo/Play scene is covering this retained
    // Ranked scene. A same-map Death Match retry does not change resourceKey(),
    // so the older resource-key reset left m_enteringLevel latched forever after
    // the child scene returned.
    bool m_rankedChildScenePushed = false;
    bool m_resourceGateInitialized = false;
    bool m_resourceGateAcknowledged = false;
    bool m_resourceGateWasMissing = false;
    bool m_resourceGateMenuRestored = false;
    std::string m_renderedUiKey;
    std::string m_pendingUiKey;
    std::string m_rankTransitionPlayedKey;
    std::string m_stageResultAudioPlayedKey;
    RankTransitionLayer* m_rankTransitionLayer = nullptr;
    corum::ranked::ui::ResultScoreLayer* m_resultScoreLayer = nullptr;
    corum::ranked::ResultPresentationGate m_resultPresentationGate;
    bool m_resultScorePending = false;
    CCNode* m_achievementToastRoot = nullptr;
    corum::ranked::AchievementToastQueue m_achievementToastQueue;
    std::unordered_map<std::string, corum::ranked::AchievementUnlockNoticeView> m_achievementToasts;
    bool m_achievementToastPlaying = false;
    bool m_uiFadingOut = false;
    bool m_fadeInNextRender = true;
    SteadyClock::time_point m_uiTransitionDeadline {};
    CCLabelBMFont* m_resourceCountLabel = nullptr;
    CCLayerColor* m_resourceProgressFill = nullptr;
    CCLabelBMFont* m_resourceProgressLabel = nullptr;
    float m_resourceProgressWidth = 0.0f;
    CCLabelBMFont* m_mainStatusLabel = nullptr;
    CCLayerColor* m_concessionNotice = nullptr;
    CCLabelBMFont* m_concessionNoticeText = nullptr;
    CCLabelBMFont* m_matchFoundCountdownLabel = nullptr;
    CCLabelBMFont* m_banTimerLabel = nullptr;
    CCLabelBMFont* m_prepareFooterLabel = nullptr;
    CCLabelBMFont* m_placementPrepareTimerLabel = nullptr;
    CCLabelBMFont* m_prepareLoadStageLabel = nullptr;
    CCLabelBMFont* m_prepareLoadPercentLabel = nullptr;
    CCLayerColor* m_prepareLoadFill = nullptr;
    float m_prepareLoadWidth = 0.0f;
    float m_prepareLoadVisual = 0.0f;
    CCLabelBMFont* m_spectatorStateLabel = nullptr;
    CCLabelBMFont* m_spectatorProgressLabel = nullptr;
    CCLabelBMFont* m_spectatorScoreLabel = nullptr;
    CCLabelBMFont* m_spectatorHintLabel = nullptr;
    // Solo VS board / round overview intro.
    std::string m_soloIntroPlayed, m_soloScoreAnimated, m_matchFoundMotionKey, m_banMotionKey, m_resultMotionKey, m_banDimKey;
    // A match found from the open queue panel shows MATCH FOUND there for ~1 s
    // before the versus intro. The opponent stays "OPPONENT" until that intro.
    std::string m_soloFoundHoldMatch;
    std::optional<SteadyClock::time_point> m_soloFoundHoldUntil;
    bool m_soloIntroRunning = false;
    // Live spectating is entered from the finished-attempt board, not automatically.
    std::string m_spectateRequestKey;
    SteadyClock::time_point m_soloHoldUntil{};
    std::string m_stableKeyCandidate;
    SteadyClock::time_point m_stableKeySince{};
    // Holds presentation rebuilds until a one-shot entrance motion finishes.
    void holdForMotion(float seconds) {
        auto const until = SteadyClock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000.0f));
        if (until > m_soloHoldUntil) m_soloHoldUntil = until;
    }
    CCLabelBMFont *m_soloTabLabel = nullptr, *m_soloClockLabel = nullptr, *m_soloClockCaption = nullptr, *m_soloOwnScore = nullptr, *m_soloOppScore = nullptr;
    CCNode *m_soloClockPanel = nullptr, *m_soloFooterPlate = nullptr;
    std::vector<CCLayerColor*> m_soloPips;
    float m_soloDigitScale = 15.0f / 64.0f;
    CCLayerColor* m_spectatorBarFill = nullptr;
    CCMenuItemSpriteExtra* m_soloSpectateButton = nullptr;
    float m_spectatorBarWidth = 0.0f;
    std::vector<std::pair<CCSprite*, CCSize>> m_soloThumbs;
    corum::ranked::SurvivalSelectionContext m_soloThumbContext;
    Ref<CCTexture2D> m_soloThumbTexture;

    bool init() override {
        if (!CCLayerColor::initWithColor({0, 0, 0, 0})) return false;
        setID("corum-ranked-fullscreen"_spr);
        setKeypadEnabled(true);

        auto const winSize = CCDirector::sharedDirector()->getWinSize();
        corum::ranked::ui::addBackdrop(this, winSize);

        m_fadeRoot = CCLayerColor::create({0, 0, 0, 0});
        m_fadeRoot->setContentSize(winSize);
        m_fadeRoot->setPosition(CCPointZero);
        m_fadeRoot->setCascadeOpacityEnabled(true);
        addChild(m_fadeRoot, 1);

        m_root = CCNode::create();
        m_root->setAnchorPoint({0.0f, 0.0f});
        m_root->setPosition(CCPointZero);
        m_root->setContentSize(winSize);
        m_fadeRoot->addChild(m_root, 1);

        m_menu = CCMenu::create();
        m_menu->setPosition(CCPointZero);
        m_menu->setID("corum-ranked-fullscreen-menu"_spr);
        m_fadeRoot->addChild(m_menu, 10);

        // Independent of page re-renders: always the same bottom-right entry.
        m_settingsMenu = CCMenu::create(); m_settingsMenu->setPosition(CCPointZero);
        m_settingsMenu->setID("ranked-settings-menu"_spr); addChild(m_settingsMenu, 100);
        auto* settingsFace = corum::ranked::ui::surface({92, 28}, {}, kSteel, kDeepPanel2, 250, true);
        m_settingsLabel = corum::ranked::ui::menuLabel(tr("settings", "Settings"), 0.27f, {46, 14},
            corum::ranked::ui::Text, corum::ranked::ui::FontWeight::ExtraBold);
        corum::ranked::ui::fitMenuLabelWidth(m_settingsLabel, 78, 0.27f, 0.18f);
        settingsFace->addChild(m_settingsLabel, 5);
        settingsFace->setAnchorPoint(CCPointZero); settingsFace->setPosition(CCPointZero);
        auto* settingsButton = CCMenuItemSpriteExtra::create(settingsFace, this, menu_selector(CorumRankedLayer::onSettings));
        settingsButton->setContentSize({92,28}); settingsButton->setAnchorPoint({0.5f,0.5f});
        settingsButton->setID("ranked-settings-button"_spr);
        settingsButton->setPosition({winSize.width - 70, 30}); m_settingsMenu->addChild(settingsButton);

        // Result Achievement notices live outside the normal layout/fade tree.
        // Their slide overlay never pushes Result content and survives a normal
        // Result re-render while the sequential queue is playing.
        m_achievementToastRoot = CCNode::create();
        m_achievementToastRoot->setContentSize(winSize);
        m_achievementToastRoot->setPosition(CCPointZero);
        m_achievementToastRoot->setID("ranked-achievement-toast-root"_spr);
        addChild(m_achievementToastRoot, 2000);

        m_concessionNotice = CCLayerColor::create({19, 21, 27, 240}, std::min(290.0f, winSize.width - 70), 26);
        m_concessionNotice->setPosition({(winSize.width - m_concessionNotice->getContentSize().width) / 2, winSize.height - 60});
        m_concessionNotice->setID("ranked-opponent-surrender-notice"_spr);
        m_concessionNoticeText = corum::ranked::ui::menuLabel("", 0.30f,
            {m_concessionNotice->getContentSize().width / 2, 13}, kGold, corum::ranked::ui::FontWeight::ExtraBold);
        m_concessionNotice->addChild(m_concessionNoticeText); m_concessionNotice->setVisible(false);
        addChild(m_concessionNotice, 2500);

        RankedRuntime::get().begin();
        // Existing process scheduler keeps the connection lease alive while
        // gameplay/paused native scenes cover the Ranked lobby.
        corum::ranked::RankedRoundPreparation::get();
        syncPhase();
        syncResources();
        m_renderedUiKey = presentationKey();
        render();
        applyFadeInIfNeeded();
        updateDynamicUi();
        return true;
    }

    void onEnter() override {
        // Restore flags BEFORE CCLayer enters its children, so their delegates
        // register in native parent/child order rather than after scene entry.
        setKeypadEnabled(!m_rankTransitionLayer && !m_resultScoreLayer && !m_resultScorePending);
        if (m_menu) {
            corum::ranked::ui::cancelMenuTouch(m_menu);
            m_menu->setTouchEnabled(!m_rankTransitionLayer && !m_resultScoreLayer && !m_resultScorePending);
        }
        if (m_settingsMenu) m_settingsMenu->setTouchEnabled(!m_rankTransitionLayer && !m_resultScoreLayer && !m_resultScorePending);
        CCLayerColor::onEnter();

        auto const resumedFromRankedChild = m_rankedChildScenePushed;
        if (resumedFromRankedChild) {
            // The scene pushed by this layer has finished. Release both launch
            // guards before refresh() so maybeEnterLevel() can open the next
            // legal attempt or read-only spectator view on the same map. The
            // separate runtime predicates remain authority for fourth Death
            // Match attempts, results, and closed attempt windows.
            m_rankedChildScenePushed = false;
            m_enteringSongGate = false;
            m_enteringLevel = false;
            log::info("Ranked child scene returned; automatic entry guard released");
        }

        // Native PlayLayer scenes are pushed over this Ranked scene. The
        // corresponding onExit() intentionally stops polling and input while a
        // player is inside those scenes, so every return through popScene() must
        // restore them. Without this lifecycle pair, the retained Ranked layer
        // comes back frozen on its pre-launch "OPENING LEVEL / STARTS IN 05"
        // frame and can never observe ROUND_RESULT or the following Round.
        schedule(schedule_selector(CorumRankedLayer::refresh), 0.20f);
        // The queue radar (sweep, contacts, panel fades) animates every frame.
        schedule(schedule_selector(CorumRankedLayer::radarFrame), 0.0f);
        // Stream health is independent of the HTTP/UI refresh. A starving
        // decode buffer must be muted before it repeatedly replays old data.
        schedule(schedule_selector(CorumRankedLayer::audioFrame), 0.02f);

        // Reconcile any Attempt ACK or match poll that completed while the
        // Ranked scene was covered instead of waiting for another visual frame.
        refresh(0.0f);
        auto* handler = m_menu ? CCDirector::sharedDirector()->getTouchDispatcher()->findHandler(m_menu) : nullptr;
        log::info("[RankedInput] lobby entered resumed={} menuTouch={} menuState={} menuPriority={}",
            resumedFromRankedChild, m_menu && m_menu->isTouchEnabled(),
            m_menu ? static_cast<int>(m_menu->m_eState) : -1, handler ? handler->m_nPriority : 0);
    }

    void audioFrame(float) { RankedAudioManager::get().tick(); }
    void radarFrame(float) { drawQueueArena(); updateSeasonTimers(); }
    void updateSeasonTimers() {
        auto const& view = RankedRuntime::get().view();
        auto const now = RankedRuntime::get().serverNowMillis();
        auto present = [&](corum::ranked::ui::SeasonTimer* timer, corum::ranked::SeasonView const& season, bool enabled) {
            if (timer && timer->getParent()) timer->present(season, enabled, now);
        };
        present(m_seasonShared.data(), view.soloSeason, view.soloEnabled);
        present(m_seasonSolo.data(), view.soloSeason, view.soloEnabled);
        present(m_seasonSurvival.data(), view.survivalSeason, view.survivalEnabled);
    }
    bool seasonShared(RuntimeView const& view) const {
        return corum::ranked::seasonTimerShared(view.soloSeason, view.soloEnabled, view.survivalSeason, view.survivalEnabled);
    }

    void refresh(float) {
        auto& runtime = RankedRuntime::get();
        syncLobbyPopulation();
        runtime.tick();
        auto& audio = RankedAudioManager::get();
        audio.configure(runtime.view().client);
        audio.tick();
        syncResources();
        syncMapDetails();
        syncPhase();
        updateAutomation();
        resumeModeSelectionAfterQueue(runtime.view());
        leaveMapPoolOnMatch(runtime.view());

        if (m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending) {
            updateDynamicUi(); syncAudioMode(); return;
        }

        if (soloMatchFoundHold()) { updateQueuePanel(); syncAudioMode(); return; }
        auto const uiKey = presentationKey();
        if (prepareUiTransition(uiKey)) {
            render();
            applyFadeInIfNeeded();
        }
        updateDynamicUi();
        syncQueuePresentation();
        updateHistoryThumbnail();
        updateSoloThumbnails();
        updateHistoryIcons();
        syncLobbyPopulation();
        syncAudioMode();
        maybeEnterLevel();
    }

    bool resourceWorkMissing() const {
        return RankedAudioManager::get().requiresResourceDownload() ||
            corum::ranked::ui::RankAssetPackManager::get().requiresDownload();
    }

    bool showResourceGate() const {
        return resourceWorkMissing() || (m_resourceGateInitialized && !m_resourceGateAcknowledged);
    }

    void syncResources() {
        if (RankedRuntime::get().view().stage == RuntimeStage::AwaitingUpdate) return;
        auto& audio = RankedAudioManager::get();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        assets.configure(RankedRuntime::get().view().client.assets);
        auto const missing = resourceWorkMissing();
        if (!m_resourceGateInitialized) {
            m_resourceGateInitialized = true;
            m_resourceGateAcknowledged = !missing;
        }
        if (missing && !m_resourceGateWasMissing) {
            m_resourceGateAcknowledged = false;
            m_resourceGateMenuRestored = false;
        }
        m_resourceGateWasMissing = missing;
        if (showResourceGate() && !m_resourceGateMenuRestored) {
            audio.restoreMenuMusic();
            m_resourceGateMenuRestored = true;
        }
        if (!showResourceGate()) m_resourceGateMenuRestored = false;
    }

    double resourceProgress() const {
        auto const audio = RankedAudioManager::get().downloadView();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        auto const total = audio.total + (assets.required() ? 1 : 0);
        if (!total) return 1;
        auto const audioPart = audio.ready + (audio.state == RankedResourceDownloadState::Downloading
            ? audio.activeProgress / 100.0 : 0.0);
        return std::clamp((audioPart + (assets.required() ? assets.view().progress : 0.0)) / total, 0.0, 1.0);
    }

    std::pair<int, int> resourceCounts() const {
        auto const audio = RankedAudioManager::get().downloadView();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        return {audio.ready + (assets.required() && assets.ready() ? 1 : 0),
            audio.total + (assets.required() ? 1 : 0)};
    }

    void syncMapDetails() {
        auto const& view = RankedRuntime::get().view();
        std::vector<int> ids;
        if (view.match.currentMap) ids.push_back(view.match.currentMap->levelId);
        for (auto const& map : view.match.candidateMaps) ids.push_back(map.levelId);
        if (m_page == Page::PlacementPoolSelection)
            for (auto const& map : view.placement.referenceMaps) ids.push_back(map.levelId);
        // Map pool rows the server could not read from GD (custom non-demons).
        if (m_page == Page::MapPool && m_mapPoolView)
            if (auto pool = m_mapPoolView->lock()) for (auto id : pool->unresolvedLevels()) ids.push_back(id);
        corum::ranked::RankedMapDetails::get().request(std::move(ids));
    }

    void addMapDifficulty(RankedMapView const& map, CCPoint center, float width, float height = 21) {
        auto* badge = corum::ranked::ui::mapDifficultyBadge(map.levelId,map.metadata,width,height);
        badge->setPosition({center.x - width / 2, center.y - height / 2});
        m_root->addChild(badge, 7);
    }

    void addJukeboxButton(CCPoint position, float scale = 0.32f) {
        m_jukeboxButton = addButton("Jukebox", position, menu_selector(CorumRankedLayer::onJukebox), false, scale, true);
        m_jukeboxButton->setID("ranked-jukebox-button"_spr);
        auto const loaded = corum::ranked::ui::rankedJukeboxLoaded();
        if (!loaded) corum::ranked::ui::grayButtonVisual(m_jukeboxButton->getNormalImage());
        m_jukeboxButton->setEnabled(!loaded || findPlayableMap() != nullptr);
    }

    std::string preparationPresentationKey() const {
        auto const& p = corum::ranked::RankedRoundPreparation::get().view();
        auto* level = findPlayableMap();
        return fmt::format(":prepare:{}:{}:{}:{}:{}:{}:{}:{}:{}", p.mapReady, p.audioReady,
            p.songsDone, p.songsTotal, p.sfxDone, p.sfxTotal, p.error,
            level && level->m_lowDetailModeToggled, level && level->m_disableShakeToggled);
    }

    std::string presentationKey() const {
        auto const& view = RankedRuntime::get().view();
        auto const detailsKey = fmt::format(":details:{}:assets:{}", corum::ranked::RankedMapDetails::get().revision(),
            corum::ranked::ui::RankAssetPackManager::get().view().activeVersion);
        if (view.stage == RuntimeStage::AwaitingUpdate) {
            auto const& update = corum::ranked::ui::RankedModUpdateManager::get().view();
            return fmt::format("mod-update:{}:{}:{}:{}", static_cast<int>(update.state), update.targetVersion, update.error,
                corum::ranked::ui::RankedModUpdateManager::get().channel());
        }
        if (view.environmentWarning.active) {
            return fmt::format("environment-warning:{}", view.revision);
        }
        if (showResourceGate()) {
            auto const resources = RankedAudioManager::get().downloadView();
            auto const assets = corum::ranked::ui::RankAssetPackManager::get().view();
            return fmt::format(
                "resources:{}:{}:{}:{}:{}",
                static_cast<int>(resources.state),
                static_cast<int>(assets.state),
                assets.activeVersion, assets.targetVersion, assets.verifying
            );
        }
        if (m_page == Page::HistoryList) {
            return fmt::format(
                "history:list:{}:{}:{}:{}:{}",
                view.historyLoading,
                view.historyError,
                view.history.size() + view.placement.stageResults.size(),
                static_cast<int>(m_historyScope), view.historyRevision
            );
        }
        // The map pool page loads and redraws itself: render it once.
        if (m_page == Page::MapPool) return "map-pool";
        if (m_page == Page::HistoryDetail) return fmt::format("history:detail:{}:{}:{}:{}:{}",m_historySelectedId,m_historyRound,m_historyTabPage,view.historyRevision,view.historyDetailError);
        if (m_page == Page::Achievements) {
            return fmt::format(
                "achievements:{}:{}:{}:{}:{}",
                view.achievements.loaded,
                view.achievements.loading,
                view.achievements.unclaimedRewardCount,
                m_achievementPage,
                m_titleSelectorAchievement + ":" + m_achievementScope
            ) + fmt::format(":{}", view.revision);
        }
        if (m_page == Page::PlacementPoolSelection) {
            return fmt::format("placement:pool:{}:{}:{}:{}:{}", RankedRuntime::get().view().revision, m_selectedPlacementStageCount, m_selectedPlacementPool, m_placementPoolStep, m_localMessage) + detailsKey;
        }
        if (m_page == Page::PlacementSkipConfirm) return "placement:skip-confirm";
        if (view.stage != RuntimeStage::Matched) {
            return fmt::format(
                "stage:{}:placement:{}:{}:standing:{}:{}",
                static_cast<int>(view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue ? RuntimeStage::Ready : view.stage),
                view.placement.required,
                view.placement.active,
                view.standing.rank.value_or(0), view.standing.total
            ) + fmt::format(":mode:{}:solo:{}:sur:{}:{}:{}:{}:{}:{}:title:{}:{}:ap:{}:{}:size:{}:{}",
                m_modeTransition.selected() ? static_cast<int>(*m_modeTransition.selected()) : -1,
                view.soloEnabled,view.survivalEnabled,view.survival.loaded,view.survival.error,
                view.survival.profile.visibleRankedScore.value_or(-1),
                fmt::format("{}{}{}",view.survival.profile.demotionWarning,view.survival.profile.promotion.active,view.survival.profile.promotion.results.size()),
                view.survival.standing.rank.value_or(0),view.survival.standing.total,
                view.achievements.equippedTitle.achievementId,view.achievements.equippedTitle.level.value_or(0),
                view.achievements.totalAp,view.achievements.unclaimedRewardCount,
                CCDirector::sharedDirector()->getWinSize().width,CCDirector::sharedDirector()->getWinSize().height) + fmt::format(":sur-record:{}:{}:format:{}",view.survival.wins,view.survival.losses,view.survivalPlayerCount)
                + fmt::format(":season:{}:{}:{}",seasonShared(view),view.soloSeason.targetAtMs.has_value(),view.survivalSeason.targetAtMs.has_value())
                + fmt::format(":tierbar:{}:{}:{}:{}:{}:{}",view.profileScore,view.standing.target?view.standing.target->score:-1,view.soloTierBands.size(),
                    view.survival.tierBands.size(),view.survival.standing.target?view.survival.standing.target->score:-1,view.survival.standing.rank.value_or(0));
        }
        if (view.match.placement) {
            if (view.placement.state == "STAGE_RESULT" && !view.placement.stageResults.empty()) {
                auto const& result = view.placement.stageResults.back();
                return fmt::format(
                    "placement:stage-result:{}:{}:{}:{}:{}",
                    view.placement.runId,
                    result.stageIndex,
                    result.stageScore,
                    result.stageGrade,
                    result.nextPool
                );
            }
            return fmt::format(
                "placement:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
                view.placement.state,
                view.placement.currentStageIndex,
                view.placement.currentMapIndex,
                view.placement.currentMapScore,
                view.placement.provisionalMapScore,
                view.placement.currentMapGrade,
                view.placement.currentMapClears,
                view.placement.stageResults.size(),
                view.placement.finalResult.displayedTier,
                view.error
            ) + preparationPresentationKey() + detailsKey;
        }
        auto key = fmt::format(
            "match:{}:{}:{}:spectator:{}",
            view.match.state,
            view.match.roundNumber,
            view.match.deathmatchSequence,
            view.match.spectatorActive
        );
        if (view.match.state == "BAN_PHASE") {
            key += fmt::format(
                ":{}:{}:{}",
                m_selectedBan,
                m_pendingBan,
                view.match.ownBanConfirmed
            );
        }
        return key + preparationPresentationKey() + detailsKey;
    }

    double uiFadeInSeconds() const {
        return std::clamp(RankedRuntime::get().view().client.ui.fadeInSeconds, 0.0, 3.0);
    }

    double uiFadeOutSeconds() const {
        return std::clamp(RankedRuntime::get().view().client.ui.fadeOutSeconds, 0.0, 3.0);
    }

    bool prepareUiTransition(std::string const& key) {
        // A match replaces the lobby: never wait for a lobby queue/mode motion.
        auto const matched = RankedRuntime::get().view().stage == RuntimeStage::Matched;
        if (m_modeTransition.moving() || (!matched && m_queueAnimating)) { m_pendingUiKey = key; return false; }
        // Solo intro / series roll: the board is rebuilt once they finish.
        if (soloHoldActive()) { m_pendingUiKey = key; return false; }
        auto const now = SteadyClock::now();
        if (m_renderedUiKey.empty()) {
            m_renderedUiKey = key;
            m_fadeInNextRender = true;
            return true;
        }

        if (m_uiFadingOut) {
            m_pendingUiKey = key;
            if (now < m_uiTransitionDeadline) return false;
            m_uiFadingOut = false;
            m_renderedUiKey = m_pendingUiKey;
            m_fadeInNextRender = true;
            return true;
        }

        // Rebuilding the CCMenu every 200 ms invalidated its raw selected-item
        // pointer between touch-began and touch-ended on iOS. The visible UI tree
        // now changes only when its presentation key changes; countdowns and live
        // values are updated in place by updateDynamicUi().
        if (key == m_renderedUiKey) { m_stableKeyCandidate.clear(); return false; }
        m_pendingUiKey = key;
        // A key must hold for a moment before the tree is rebuilt. Keys that
        // flap between polls (resource/spectator projections) otherwise rebuilt
        // every menu several times a second, so taps never landed and a tap on
        // a rebuilt menu could crash.
        auto const enteringMatch = matched && m_renderedUiKey.rfind("match:", 0) != 0;
        if (!enteringMatch && key != m_stableKeyCandidate) { m_stableKeyCandidate = key; m_stableKeySince = now; return false; }
        if (!enteringMatch && now - m_stableKeySince < std::chrono::milliseconds(300)) return false;
        m_stableKeyCandidate.clear();
        if (RankedRuntime::get().view().stage == RuntimeStage::Matched)
            log::info("[RankedUi] rebuild {} -> {}", m_renderedUiKey, key);
        auto const fadeOut = uiFadeOutSeconds();
        if (fadeOut <= 0.0 || !m_fadeRoot) {
            m_renderedUiKey = key;
            m_fadeInNextRender = true;
            return true;
        }

        m_uiFadingOut = true;
        m_uiTransitionDeadline = now + std::chrono::milliseconds(
            static_cast<int>(std::round(fadeOut * 1000.0))
        );
        m_fadeRoot->stopAllActions();
        m_fadeRoot->runAction(CCFadeTo::create(static_cast<float>(fadeOut), 0));
        return false;
    }

    void applyFadeInIfNeeded() {
        if (!m_fadeInNextRender || !m_fadeRoot) return;
        m_fadeInNextRender = false;
        auto const fadeIn = uiFadeInSeconds();
        m_fadeRoot->stopAllActions();
        if (fadeIn <= 0.0) {
            m_fadeRoot->setOpacity(255);
            return;
        }
        m_fadeRoot->setOpacity(0);
        m_fadeRoot->runAction(CCFadeTo::create(static_cast<float>(fadeIn), 255));
    }

    void syncAudioMode() {
        auto& audio = RankedAudioManager::get();
        auto* director=CCDirector::sharedDirector();
        auto* scene=director->getRunningScene();
        // An outgoing lobby can still refresh during a native scene fade.
        // It must not restart its BGM after history has handed audio to GD.
        // onEnter/refresh restores the selected mode when this scene returns.
        if (!isRunning() || scene != getParent() || director->getNextScene()) return;
        if(scene && scene->getChildByIDRecursive("survival-game-coordinator"_spr))return;
        if (showResourceGate() || RankedRuntime::get().view().stage == RuntimeStage::AwaitingUpdate) return;

        auto const& view = RankedRuntime::get().view();
        auto const lobbyMode = [&view]() {
            // A promotion series is the stronger lobby state. In normal operation
            // promotion and demotion warning are mutually exclusive, but this
            // precedence keeps the audio deterministic if stale profile data ever
            // exposes both flags during a refresh.
            // Either mode's series/warning changes the shared lobby BGM.
            if (view.promotion.active || view.survival.profile.promotion.active) return RankedAudioMode::LobbyPromotion;
            if (view.demotionWarning || view.survival.profile.demotionWarning) return RankedAudioMode::LobbyDemotionWarning;
            return RankedAudioMode::Lobby;
        };

        if (view.stage == RuntimeStage::Matched) {
            auto const& state = view.match.state;
            auto const gameplayTransition =
                state == "ROUND_PREPARE" ||
                state == "DEATHMATCH_PREPARE" ||
                state == "ROUND_ENTERING" ||
                state == "DEATHMATCH_ENTERING" ||
                state == "ROUND_PLAYING" ||
                state == "FINAL_ATTEMPT_WINDOW" ||
                state == "LAST_ATTEMPT_WINDOW" ||
                state == "DEATHMATCH_PLAYING" ||
                state == "MAP_PREPARE" ||
                state == "MAP_PLAYING" ||
                state == "MAP_SETTLING";
            if ((m_enteringSongGate || m_enteringLevel) && gameplayTransition) return;
        }

        if (m_page != Page::Live || view.stage != RuntimeStage::Matched) {
            audio.setMode(lobbyMode());
            return;
        }

        if (view.match.state == "MATCH_RESULT") {
            audio.setMode(view.match.winnerSide == view.match.side
                ? RankedAudioMode::ResultWin
                : RankedAudioMode::ResultLose);
            return;
        }

        if (view.match.state == "CANCELLED") {
            audio.setMode(lobbyMode());
            return;
        }

        if (view.match.placement && view.match.state == "STAGE_RESULT") {
            if (view.placement.stageResults.empty()) return;
            auto const& result = view.placement.stageResults.back();
            auto const key = fmt::format("{}:{}", view.placement.runId, result.stageIndex);
            if (m_stageResultAudioPlayedKey == key) {
                audio.setMode(
                    result.stageGrade == "S" || result.stageGrade == "A" || result.stageGrade == "B"
                        ? RankedAudioMode::ResultWin
                        : RankedAudioMode::ResultLose
                );
            }
            return;
        }

        auto const& state = view.match.state;
        auto const isDeathmatchWait =
            state == "DEATHMATCH_PREPARE" ||
            state == "DEATHMATCH_ENTERING" ||
            state == "DEATHMATCH_PLAYING" ||
            state == "DEATHMATCH_RESULT";
        auto const isRoundThree =
            view.match.roundNumber >= 3 &&
            (
                state == "ROUND_PREPARE" ||
                state == "ROUND_ENTERING" ||
                state == "ROUND_PLAYING" ||
                state == "FINAL_ATTEMPT_WINDOW" ||
                state == "LAST_ATTEMPT_WINDOW" ||
                state == "ROUND_SETTLING" ||
                state == "ROUND_RESULT"
            );

        audio.setMode(isDeathmatchWait || isRoundThree
            ? RankedAudioMode::Round3DeathmatchWait
            : RankedAudioMode::Round12);
    }

    void syncPhase() {
        corum::ranked::RankedLevelPreparation::get().observeContext();
        auto const& view = RankedRuntime::get().view();
        if (view.stage == RuntimeStage::Matched && m_lastMatchId != view.match.matchId) {
            m_lastMatchId = view.match.matchId;
            m_selectedBan.clear();
            m_pendingBan.clear();
            m_localMessage.clear();
            m_resourceKey.clear();
            m_matchFoundReadySent = false;
        }
        // Ban acknowledgement/error can change independently of the visible phase.
        // Reconcile it before the phase-key early return.
        if (view.stage == RuntimeStage::Matched) {
            if (view.match.ownBanConfirmed) {
                m_selectedBan = view.match.ownBanCanonicalLevelId.empty() ? std::string(kNoBanChoice) : view.match.ownBanCanonicalLevelId;
                m_pendingBan.clear();
            } else if (!view.error.empty() && !m_pendingBan.empty()) {
                m_pendingBan.clear();
            }
        }

        auto const key = view.stage == RuntimeStage::Matched
            ? fmt::format("{}:{}:{}", view.match.matchId, view.match.state, view.match.stateVersion)
            : fmt::format("stage:{}", static_cast<int>(view.stage));
        if (m_phaseKey == key) return;
        auto const stateChanged = m_phaseKey.empty() ||
            (view.stage == RuntimeStage::Matched && m_phaseKey.find(view.match.state) == std::string::npos);
        m_phaseKey = key;
        if (stateChanged) {
            m_phaseStartedAt = SteadyClock::now();
            m_matchFoundReadySent = false;
        }

        if (view.stage == RuntimeStage::Matched) {
            auto const newResourceKey = resourceKey(view.match);
            if (newResourceKey != m_resourceKey) {
                m_resourceKey = newResourceKey;
                m_readySent = false;
                m_lastReadySubmitAt = {};
                m_enteringSongGate = false;
                RankedRuntime::get().setSongBypassAllowed(false);
                m_enteringLevel = false;
                m_songIds.clear();
                m_levelLookupError.clear();
                m_levelLookupStartedAt = SteadyClock::now();
                m_prepareLoadVisual = 0.0f;
            }

        }
    }

    bool soloMatchFoundHold() {
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched || view.match.state != "MATCHED") { m_soloFoundHoldUntil.reset(); return false; }
        if (m_soloFoundHoldMatch != view.match.matchId) {
            m_soloFoundHoldMatch = view.match.matchId;
            m_soloFoundHoldUntil.reset();
            if (m_page == Page::Live && m_queueShown && m_queueContents && !m_queueAnimating &&
                m_modeTransition.selected() == corum::ranked::RankedMode::SoloRanked) {
                m_soloFoundHoldUntil = SteadyClock::now() + std::chrono::milliseconds(corum::ranked::SurvivalMatchFoundHoldMs);
                // The match-found countdown and versus intro start after the hold.
                m_phaseStartedAt = *m_soloFoundHoldUntil;
            }
        }
        if (!m_soloFoundHoldUntil) return false;
        if (SteadyClock::now() >= *m_soloFoundHoldUntil) { m_soloFoundHoldUntil.reset(); return false; }
        return true;
    }

    double phaseSeconds() const {
        if (m_phaseStartedAt == SteadyClock::time_point{}) return 0.0;
        return std::chrono::duration<double>(SteadyClock::now() - m_phaseStartedAt).count();
    }

    void resetDynamicUiPointers() {
        m_jukeboxButton = nullptr;
        m_resourceCountLabel = nullptr;
        m_resourceProgressFill = nullptr;
        m_resourceProgressLabel = nullptr;
        m_resourceProgressWidth = 0.0f;
        m_mainStatusLabel = nullptr;
        m_matchFoundCountdownLabel = nullptr;
        m_banTimerLabel = nullptr;
        m_prepareFooterLabel = nullptr;
        m_placementPrepareTimerLabel = nullptr;
        m_prepareLoadStageLabel = nullptr;
        m_prepareLoadPercentLabel = nullptr;
        m_prepareLoadFill = nullptr;
        m_prepareLoadWidth = 0.0f;
        m_spectatorStateLabel = nullptr;
        m_spectatorProgressLabel = nullptr;
        m_spectatorScoreLabel = nullptr;
        m_spectatorHintLabel = nullptr;
        m_soloTabLabel = m_soloClockLabel = m_soloClockCaption = m_soloOwnScore = m_soloOppScore = nullptr;
        m_soloClockPanel = m_soloFooterPlate = nullptr;
        m_soloPips.clear(); m_spectatorBarFill = nullptr; m_spectatorBarWidth = 0.0f; m_soloSpectateButton = nullptr;
        m_soloThumbs.clear(); m_soloIntroRunning = false;
    }

    void suspendHistoryThumbnail() {
        if(m_historyThumbnailContext.levelId)corum::ranked::SurvivalThumbnailProvider::history().release(m_historyThumbnailContext);
    }
    void resetHistoryUi() {
        ++m_historyUiGeneration;suspendHistoryThumbnail();m_historyThumbnail=nullptr;m_historyThumbnailPlaceholder=nullptr;m_historyTexture=nullptr;m_historyListScroll=nullptr;m_historyBoardScroll=nullptr;m_historyProfiles.clear();m_historyIcons.clear();
    }
    // Motion groups carry their own CCMenu; release any tracked touch before the
    // tree is destroyed so a selected item is never activated after removal.
    static void cancelNestedMenuTouches(CCNode* node) {
        if (!node) return;
        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto* menu = typeinfo_cast<CCMenu*>(child)) corum::ranked::ui::cancelMenuTouch(menu);
            cancelNestedMenuTouches(child);
        }
    }
    void clearUi() {
        resetHistoryUi();
        if(m_lobbyHover)m_lobbyHover->detach();
        m_lobbyHover=nullptr;m_lobbyContents=nullptr;
        if(auto* settings=m_settingsMenu?m_settingsMenu->getChildByID("ranked-settings-button"_spr):nullptr) {
            auto const size=CCDirector::sharedDirector()->getWinSize();settings->setPosition({size.width-20-settings->getContentSize().width/2,27});
        }
        setLobbyNodeInput(m_lobbySurvival,false); setLobbyNodeInput(m_lobbySolo,false); setLobbyNodeInput(m_lobbyPanel,false);
        setLobbyNodeInput(m_lobbyToolbar,false); m_lobbyToolbar = nullptr;
        m_lobbySurvival = m_lobbySolo = m_lobbyPanel = nullptr;
        m_queueContents = nullptr; m_queueMenu = nullptr; m_queuePlayers = m_queueWait = m_queueMinimum = m_queueHint = m_queueHeading = nullptr;
        m_queueRingCyan = m_queueRingGold = m_queueSweep = m_queueOpponentBox = nullptr; m_queueCenter = m_queueOpponentMark = m_queueOpponentLabel = nullptr;
        m_queueRadar = nullptr; m_queueRangeLabel = nullptr;
        m_queueSlots.clear(); m_queueFillBar = nullptr; m_queueLeave = m_queueFill = nullptr; m_queueShown = m_queueAnimating = false;
        resetDynamicUiPointers();
        corum::ranked::ui::cancelMenuTouch(m_menu);
        cancelNestedMenuTouches(m_root);
        m_root->removeAllChildrenWithCleanup(true);
        m_menu->removeAllChildrenWithCleanup(true);
    }

    void updateHistoryReconnect(RuntimeView const& noticeView) {
        auto const history=m_page==Page::HistoryList||m_page==Page::HistoryDetail;
        auto const expired=corum::ranked::historyReconnectNeeded(history,noticeView.stage==RuntimeStage::Error,!RankedRuntime::get().spectatorLiveSessionToken().empty(),noticeView.accountActiveElsewhere);
        if(expired)closeHistoryFilter();
        if(!expired){
            m_historyReconnectShown=false;
            if(m_historyReconnect)if(auto popup=m_historyReconnect->lock())popup->cancel();
            m_historyReconnect.reset();
            return;
        }
        if(expired && !m_historyReconnectShown){
            auto* popup=corum::ranked::ui::RankedConfirmPopup::create(tr("history-connection-expired","RANKED CONNECTION EXPIRED"),tr("history-connection-help","Reconnect to continue viewing your match history."),
                [weak=WeakRef(this)](bool accepted){if(auto owner=weak.lock();owner && owner->isRunning()){
                    owner->m_historyReconnect.reset();owner->m_page=Page::Live;owner->m_renderedUiKey.clear();
                    if(accepted)RankedRuntime::get().begin();
                }},tr("history-reconnect","RECONNECT"));
            if(popup){m_historyReconnectShown=true;m_historyReconnect.emplace(popup);popup->setID("ranked-history-reconnect"_spr);popup->show();popup->setZOrder(1000000);}
        }
    }

    void updateDynamicUi() {
        if(m_lobbyHover) {
            auto const modal = [](auto const& reference){if(!reference)return false;auto owner=reference->lock();return owner && owner->getParent();};
            m_lobbyHover->setActive(m_page==Page::Live && !m_modeTransition.moving() && !m_uiFadingOut && !m_modeNoticeOpen &&
                !m_rankTransitionLayer && !m_resultScoreLayer && !m_resultScorePending && !modal(m_settings) && !modal(m_leaderboard) && !modal(m_jukebox));
        }
        auto const& noticeView = RankedRuntime::get().view();
        if (m_modeNoticeOpen && (m_page != Page::Live || m_uiFadingOut ||
            noticeView.stage != RuntimeStage::Ready || noticeView.accountActiveElsewhere || showResourceGate())) {
            closeModeNotice();
            syncLobbyInput();
        }
        if (!RankedRuntime::get().view().accountActiveElsewhere) m_accountConflictShown = false;
        else if (!m_accountConflictShown) {
            if (auto* popup = corum::ranked::ui::RankedSessionConflictPopup::create()) {
                m_accountConflictShown = true; m_sessionConflict.emplace(popup); popup->show();popup->setZOrder(1000000);
            }
        }
        updateHistoryReconnect(noticeView);
        if (m_jukeboxButton) m_jukeboxButton->setEnabled(!corum::ranked::ui::rankedJukeboxLoaded() || findPlayableMap() != nullptr);
        if (m_jukebox) if (auto popup = m_jukebox->lock()) {
            auto const& match = RankedRuntime::get().view().match;
            auto const left = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
            auto const text = corum::ranked::roundPrepareState(match.state)
                ? fmt::format("{} {:02d}", tr("starts-in", "STARTS IN"), left)
                : tr("starting", "STARTING...");
            popup->setCountdown(text);
        }
        if (m_settingsLabel) {
            m_settingsLabel->setString(tr("settings", "Settings").c_str());
            corum::ranked::ui::fitMenuLabelWidth(m_settingsLabel, 78, 0.27f, 0.18f);
        }
        if (m_settingsMenu) {
            auto const& view = RankedRuntime::get().view();
            auto const lobby = m_page == Page::Live && (view.stage == RuntimeStage::Ready ||
                view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue);
            auto const allowed = !m_rankTransitionLayer && !m_resultScoreLayer && !m_resultScorePending &&
                m_titleSelectorAchievement.empty() && !m_modeTransition.moving() && !m_modeNoticeOpen && (lobby || m_page == Page::Achievements ||
                    false);
            if (!allowed && m_settingsMenu->isVisible()) corum::ranked::ui::cancelMenuTouch(m_settingsMenu);
            m_settingsMenu->setVisible(allowed); m_settingsMenu->setTouchEnabled(allowed);
        }
        auto const& runtime = RankedRuntime::get();
        auto const& noticeMatch = runtime.view().match;
        if (m_placementPrepareTimerLabel) {
            auto const remaining = runtime.deadlineSeconds();
            m_placementPrepareTimerLabel->setString(remaining
                ? fmt::format("{} {}s", tr("starts-in", "STARTS IN"), *remaining).c_str()
                : tr("starting", "STARTING...").c_str());
            m_placementPrepareTimerLabel->setColor(remaining && *remaining <= 3 ? kRed : kGold);
            corum::ranked::ui::fitMenuLabelWidth(m_placementPrepareTimerLabel, 70.0f, fontScale(15.0f), 0.09f);
        }
        auto const opponentSurrender = m_page == Page::Live && runtime.view().stage == RuntimeStage::Matched &&
            !runtime.view().environmentWarning.active && !showResourceGate() &&
            runtime.opponentConceded(noticeMatch.roundNumber, noticeMatch.deathmatchSequence);
        if (m_concessionNotice) {
            m_concessionNotice->setVisible(opponentSurrender);
            if (opponentSurrender) {
                auto const text = noticeMatch.surrenderScope == "MATCH"
                    ? tr("opponent-surrender-match", "OPPONENT SURRENDERED THE MATCH")
                    : tr("opponent-surrender-round", "OPPONENT SURRENDERED THE ROUND");
                m_concessionNoticeText->setString(text.c_str());
                corum::ranked::ui::fitMenuLabelWidth(m_concessionNoticeText,
                    m_concessionNotice->getContentSize().width - 16, 0.30f, 0.20f);
            }
        }
        if (RankedRuntime::get().view().stage == RuntimeStage::AwaitingUpdate) {
            auto const progress = std::clamp(static_cast<float>(corum::ranked::ui::RankedModUpdateManager::get().view().progress), 0.0f, 1.0f);
            if (m_resourceProgressLabel) m_resourceProgressLabel->setString(fmt::format("{}%", static_cast<int>(std::round(progress * 100))).c_str());
            if (m_resourceProgressFill) m_resourceProgressFill->setContentSize({m_resourceProgressWidth * progress, 10.0f});
            return;
        }
        if (RankedRuntime::get().view().environmentWarning.active) return;
        if (showResourceGate()) {
            auto const [ready, total] = resourceCounts();
            if (m_resourceCountLabel) {
                m_resourceCountLabel->setString(fmt::format("{} / {} {}", ready, total, tr("ready", "READY")).c_str());
                m_resourceCountLabel->setColor(ready == total ? kGreen : corum::ranked::ui::Text);
            }
            auto const progress = resourceProgress();
            if (m_resourceProgressFill) m_resourceProgressFill->setContentSize({
                static_cast<float>(m_resourceProgressWidth * progress), 10.0f});
            if (m_resourceProgressLabel) m_resourceProgressLabel->setString(
                fmt::format("{}%", static_cast<int>(std::round(progress * 100.0))).c_str());
            return;
        }

        if (m_page != Page::Live) return;
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) {
            if (m_mainStatusLabel) {
                auto detail = lobbyMessage(view);
                m_mainStatusLabel->setString(shorten(detail, 90).c_str());
                auto const width = CCDirector::sharedDirector()->getWinSize().width;
                corum::ranked::ui::fitMenuLabelWidth(m_mainStatusLabel,m_statusTextWidth>0?m_statusTextWidth:std::max(100.0f,width-264),0.24f,0.14f);
                m_mainStatusLabel->setColor(view.error.empty() ? ccc3(220, 228, 244) : kRed);
            }
            return;
        }

        auto const& match = view.match;
        if (match.state == "MATCHED" && m_matchFoundCountdownLabel) {
            auto const left = std::max(0, 5 - static_cast<int>(std::floor(phaseSeconds())));
            m_matchFoundCountdownLabel->setString(fmt::format("{}", left).c_str());
            m_matchFoundCountdownLabel->setColor(left <= 2 ? kSideRed : kGold);
        }

        if (match.state == "BAN_PHASE" && m_banTimerLabel) {
            auto const remaining = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
            m_banTimerLabel->setString(fmt::format("{}", remaining).c_str());
            m_banTimerLabel->setColor(remaining <= 3 ? kSideRed : ccc3(245, 248, 255));
        }

        if (m_prepareFooterLabel && !m_soloIntroRunning) {
            ccColor3B accent = kGold;
            auto const footer = soloFooterText(match, accent);
            if (footer != m_prepareFooterLabel->getString()) m_prepareFooterLabel->setString(footer.c_str());
            if (m_soloFooterPlate) corum::ranked::ui::setSurfaceAccent(m_soloFooterPlate, accent);
        }
        updateSoloClock(match);

        if (
            match.state == "ROUND_PREPARE" ||
            match.state == "DEATHMATCH_PREPARE" ||
            match.state == "ROUND_ENTERING" ||
            match.state == "DEATHMATCH_ENTERING" ||
            match.state == "MAP_PREPARE"
        ) {
            auto const target = preparationProgressTarget();
            auto const delta = std::max(0.0f, target - m_prepareLoadVisual);
            m_prepareLoadVisual = std::clamp(
                m_prepareLoadVisual + std::max(0.0025f, delta * 0.26f),
                0.0f,
                target
            );
            if (m_prepareLoadFill) {
                m_prepareLoadFill->setContentSize({m_prepareLoadWidth * m_prepareLoadVisual, 8.0f});
            }
            if (m_prepareLoadPercentLabel) {
                m_prepareLoadPercentLabel->setString(
                    fmt::format("{}%", static_cast<int>(std::round(m_prepareLoadVisual * 100.0f))).c_str()
                );
            }
            if (m_prepareLoadStageLabel) {
                m_prepareLoadStageLabel->setString(preparationStageText().c_str());
                m_prepareLoadStageLabel->setColor(
                    m_levelLookupError.empty() ? kAccent : kGold
                );
            }
        }

        if (m_spectatorProgressLabel) {
            auto const ownA = match.side == "A";
            auto const ownScore = ownA ? match.scoreA : match.scoreB;
            auto const opponentScore = ownA ? match.scoreB : match.scoreA;
            auto const opponentAttemptActive = match.spectatorCurrentProgress.has_value();
            if (m_spectatorStateLabel) {
                auto const stateText = opponentAttemptActive
                    ? tr("live-progress", "LIVE PROGRESS")
                    : tr("waiting-start", "WAITING TO START");
                m_spectatorStateLabel->setString(stateText.c_str());
            }
            m_spectatorProgressLabel->setString(
                opponentAttemptActive
                    ? fmt::format("{}%", match.spectatorCurrentProgress.value_or(0)).c_str()
                    : "--"
            );
            if (m_soloSpectateButton) m_soloSpectateButton->setEnabled(RankedRuntime::get().canEnterCurrentLevelAsSpectator());
            if (m_spectatorBarFill)
                m_spectatorBarFill->setContentSize({m_spectatorBarWidth * std::clamp(match.spectatorCurrentProgress.value_or(0) / 100.0f, 0.0f, 1.0f), 7.0f});
            if (m_spectatorScoreLabel) {
                m_spectatorScoreLabel->setString(
                    scoreComparison(ownScore, opponentScore,match.deathmatchSequence>0).c_str()
                );
            }
            if (m_spectatorHintLabel) {
                std::string hint = tr("round-resolve-hint", "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS");
                if (!opponentAttemptActive && match.state == "LAST_ATTEMPT_WINDOW") {
                    auto const remaining = std::max<std::int64_t>(
                        0,
                        RankedRuntime::get().deadlineSeconds().value_or(0)
                    );
                    hint = fmt::format(fmt::runtime(tr("last-attempt-seconds","OPPONENT HAS {}s TO START THE LAST ATTEMPT")),remaining);
                }
                m_spectatorHintLabel->setString(hint.c_str());
            }
        }
    }

    void render() {
        if (m_resultScoreLayer || m_resultScorePending || m_modeTransition.moving()) return;
        auto const stage = RankedRuntime::get().view().stage;
        if (stage == RuntimeStage::Matched || stage == RuntimeStage::Queued || stage == RuntimeStage::JoiningQueue) m_modeTransition.recoverSolo();
        else if (corum::ranked::SurvivalRuntime::get().queueing()) m_modeTransition.recoverSurvival();
        clearUi();
        auto const& view = RankedRuntime::get().view();
        if (view.stage == RuntimeStage::AwaitingUpdate) {
            renderModUpdate();
            return;
        }
        if (view.environmentWarning.active) {
            renderEnvironmentWarning(view);
            return;
        }
        if (showResourceGate()) {
            renderResources();
            return;
        }
        if (m_page == Page::HistoryList) {
            renderHistoryList(view);
            return;
        }
        if (m_page == Page::HistoryDetail) {
            renderHistoryDetail(view);
            return;
        }
        if (m_page == Page::MapPool) {
            renderMapPool();
            return;
        }
        if (m_page == Page::Achievements) {
            renderAchievements(view);
            return;
        }
        if (m_page == Page::PlacementPoolSelection) {
            renderPlacementPoolSelection(view);
            return;
        }
        if (m_page == Page::PlacementSkipConfirm) {
            renderPlacementSkipConfirm();
            return;
        }

        if (view.stage == RuntimeStage::Matched) {
            renderMatch(view);
        } else {
            renderMain(view);
        }
    }

    void addTopBack(bool closeLayer = true) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* sprite = corum::ranked::ui::surface({28, 28}, {}, kSteel, kDeepPanel2, 250, false);
        auto* mark = corum::ranked::ui::chevron(11.0f, 2.5f, corum::ranked::ui::Text);
        mark->setScaleX(-1.0f); mark->setPosition({13.0f, 14.0f}); sprite->addChild(mark, 5);
        auto* button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            closeLayer
                ? menu_selector(CorumRankedLayer::onCloseLayer)
                : menu_selector(CorumRankedLayer::onHistoryBack)
        );
        button->setPosition({24.0f, size.height - 24.0f});
        m_menu->addChild(button);
    }

    void addScreenHeader(
        std::string const& titleText,
        std::string const& eyebrowText,
        ccColor3B titleColor = corum::ranked::ui::Text,
        ccColor3B accent = kAccent
    ) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* title = makeLabel(
            titleText,
            0.70f,
            {size.width / 2.0f, size.height - 28.0f},
            titleColor,
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(title, size.width - 170.0f, 0.70f, 0.40f);
        corum::ranked::ui::leanLabel(title);
        title->setID("ranked-screen-title"_spr);
        m_root->addChild(title, 6);

        auto* eyebrow = makeLabel(
            eyebrowText,
            0.26f,
            {size.width / 2.0f, size.height - 53.0f},
            accent,
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(eyebrow, 238.0f, 0.26f, 0.18f);
        eyebrow->setID("ranked-screen-eyebrow"_spr);
        m_root->addChild(eyebrow, 6);

        auto const lineWidth = std::max(24.0f, std::min(92.0f, size.width / 2.0f - 122.0f));
        for (auto const x : {size.width / 2.0f - 108.0f - lineWidth, size.width / 2.0f + 108.0f}) {
            auto* line = CCLayerColor::create({accent.r, accent.g, accent.b, 150});
            line->setContentSize({lineWidth, 1.5f});
            line->setPosition({x, size.height - 53.75f});
            line->setSkewX(40.0f);
            m_root->addChild(line, 4);
        }
    }

    CCMenuItemSpriteExtra* addButton(
        std::string const& text,
        CCPoint position,
        SEL_MenuHandler selector,
        bool danger = false,
        float scale = 0.72f,
        bool secondary = false
    ) {
        auto* visual = makeActionButtonVisual(text, danger, false, secondary);
        visual->setScale(scale);
        auto* item = CCMenuItemSpriteExtra::create(visual, this, selector);
        item->setPosition(position);
        m_menu->addChild(item);
        return item;
    }

    void addLanguageSelector() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const selected = corum::ranked::i18n::language();

        auto* englishVisual = makeActionButtonVisual(
            "English",
            false,
            selected == corum::ranked::i18n::Language::English,
            true
        );
        englishVisual->setScale(0.48f);
        auto* english = CCMenuItemSpriteExtra::create(
            englishVisual,
            this,
            menu_selector(CorumRankedLayer::onEnglish)
        );
        english->setPosition({77.0f, size.height - 28.0f});
        english->setID("ranked-language-english"_spr);
        m_menu->addChild(english);

        auto* koreanVisual = makeActionButtonVisual(
            corum::ranked::catalogText("language-korean",true),
            false,
            selected == corum::ranked::i18n::Language::Korean,
            true
        );
        koreanVisual->setScale(0.48f);
        auto* korean = CCMenuItemSpriteExtra::create(
            koreanVisual,
            this,
            menu_selector(CorumRankedLayer::onKorean)
        );
        auto const languageGap = (englishVisual->getContentSize().width * englishVisual->getScaleX() +
            koreanVisual->getContentSize().width * koreanVisual->getScaleX()) / 2 + 7;
        korean->setPosition({77.0f + languageGap, size.height - 28.0f});
        korean->setID("ranked-language-korean"_spr);
        m_menu->addChild(korean);
    }

    void addStatusPill(std::string const& text, CCPoint position, ccColor3B color) {
        auto* panel = makePanel({106.0f, 28.0f}, position, color, 190);
        m_root->addChild(panel, 1);
        auto* label = makeLabel(text, 0.32f, position, corum::ranked::ui::Text, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(label, 96.0f, 0.32f, 0.20f);
        m_root->addChild(label, 2);
    }

    void renderEnvironmentWarning(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        addLanguageSelector();
        // Q13 board: the red brand with the RANKED ACCESS BLOCKED chip, the
        // 20 px line, one ruled panel per problem (red: not allowed, gold:
        // missing or incompatible, neutral: CBF), the action note and CHECK AGAIN.
        addGateHeader(tr("mod-check-required", "MOD CHECK REQUIRED"), "", tr("ranked-access-blocked", "RANKED ACCESS BLOCKED"), kRed, corum::ranked::ui::AccentInk, kRed);
        auto const x0 = px(28.0f), x1 = size.width - px(28.0f);
        float y = size.height - kLobbyHeader - 12.0f;
        auto* body = makeLabel(tr("environment-warning-body", "RANKED CANNOT BE USED WITH THE CURRENT MOD SETUP."), fontScale(20.0f), {x0, y},
            corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(body, x1 - x0, fontScale(20.0f), 0.14f); m_root->addChild(body, 4);
        y -= 16.0f;
        auto addSection = [&](std::string const& heading, std::string const& detail, ccColor3B rule) {
            if (detail.empty()) return;
            constexpr float h = 38.0f;
            m_root->addChild(makeNeonPanel({x1 - x0, h}, {(x0 + x1) / 2.0f, y - h / 2.0f}, rule, kDeepPanel, 255), 2);
            auto const headingTone = rule.r == kDeepPanel.r && rule.g == kDeepPanel.g ? corum::ranked::ui::TextMuted : rule;
            m_root->addChild(makeLabel(heading, fontScale(14.0f), {x0 + 9.0f, y - 11.0f}, headingTone, "extrabold", {0.0f, 0.5f}), 4);
            auto* detailLabel = makeLabel(detail, fontScale(15.0f), {x0 + 9.0f, y - 27.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(detailLabel, x1 - x0 - 18.0f, fontScale(15.0f), 0.09f); m_root->addChild(detailLabel, 4);
            y -= h + 6.0f;
        };
        addSection(tr("unauthorized-active-mods", "UNAUTHORIZED ACTIVE MODS"), joinEnvironmentMods(view.environmentWarning.unauthorizedMods, false), kRed);
        addSection(tr("missing-required-mods", "MISSING OR INACTIVE REQUIRED MODS"), joinEnvironmentMods(view.environmentWarning.missingRequiredMods, true), kGold);
        addSection(tr("incompatible-mod-versions", "INCOMPATIBLE MOD VERSIONS"), joinEnvironmentMods(view.environmentWarning.versionViolations, true), kGold);
        addSection(tr("cbf-settings-required", "CBF SETTINGS REQUIRED"), cbfSettingsText(view.environmentWarning.cbfIssues), kDeepPanel);
        auto* action = makeLabel(tr("environment-warning-action", "FIX THE ITEMS BELOW, RESTART THE GAME, THEN CHECK AGAIN."), fontScale(13.0f),
            {x0, std::max(52.0f, y - 6.0f)}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(action, x1 - x0, fontScale(13.0f), 0.08f); m_root->addChild(action, 4);
        boardButton(tr("retry-check", "CHECK AGAIN"), {170.0f, px(54.0f)}, {x0 + 85.0f, 26.0f}, menu_selector(CorumRankedLayer::onEnvironmentRecheck), 20.0f, true, "ranked-environment-recheck"_spr);
    }

    void addRatingMarks(
        std::vector<std::string> const& results,
        int slots,
        CCPoint center,
        float spacing = 13.0f,
        float scale = 0.24f,
        std::vector<int> const* ranks = nullptr
    ) {
        slots = std::clamp(slots, 1, 9);
        auto const totalWidth = spacing * static_cast<float>(slots - 1);
        for (int index = 0; index < slots; ++index) {
            std::string text = "-";
            ccColor3B color = ccc3(145, 151, 166);
            if (index < static_cast<int>(results.size())) {
                // Survival series show the achieved rank instead of O/X.
                auto const rank = ranks && index < static_cast<int>(ranks->size()) ? (*ranks)[static_cast<std::size_t>(index)] : 0;
                if (results[index] == "WIN") {
                    text = rank > 0 ? std::to_string(rank) : "O";
                    color = kGreen;
                } else if (results[index] == "LOSS") {
                    text = rank > 0 ? std::to_string(rank) : "X";
                    color = kRed;
                }
            }
            auto* label = makeLabel(
                text,
                scale,
                {center.x - totalWidth / 2.0f + spacing * static_cast<float>(index), center.y},
                color,
                "black"
            );
            m_root->addChild(label, 6);
        }
    }

    void addPlacementProgress(
        int completed,
        int slots,
        CCPoint center,
        float spacing = 17.0f,
        float scale = 0.30f
    ) {
        slots = std::clamp(slots, 1, 9);
        completed = std::clamp(completed, 0, slots);
        auto const totalWidth = spacing * static_cast<float>(slots - 1);
        for (int index = 0; index < slots; ++index) {
            auto* mark = makeLabel(
                index < completed ? "●" : "○",
                scale,
                {center.x - totalWidth / 2.0f + spacing * static_cast<float>(index), center.y},
                index < completed ? kGreen : ccc3(145, 151, 166),
                "black"
            );
            m_root->addChild(mark, 6);
        }
    }

    void addPromotionAura(CCPoint center, CCSize size, bool perfect = false) {
        auto const accent = perfect ? kGold : ccc3(255, 137, 58);
        auto const bottom = center.y - size.height / 2.0f + 4.0f;

        // A restrained light rail replaces the repeated sticker-like fire PNGs.
        // It preserves the special-state cue without covering trophies or text.
        auto* glow = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(perfect ? 160 : 118)});
        glow->setContentSize({std::max(24.0f, size.width - 24.0f), perfect ? 2.0f : 1.5f});
        glow->setPosition({center.x - glow->getContentSize().width / 2.0f, bottom});
        glow->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(0.75f, perfect ? 245 : 205),
            CCFadeTo::create(0.75f, perfect ? 125 : 88),
            nullptr
        )));
        glow->setID("ranked-promotion-aura-glow"_spr);
        m_root->addChild(glow, 3);

        for (auto const x : {center.x - size.width / 2.0f + 5.0f, center.x + size.width / 2.0f - 6.5f}) {
            auto* rail = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(perfect ? 185 : 132)});
            rail->setContentSize({1.5f, perfect ? 24.0f : 17.0f});
            rail->setPosition({x, bottom});
            if (x > center.x) rail->setID("ranked-promotion-aura-rail-right"_spr);
            m_root->addChild(rail, 3);
        }
    }

    void addRankStatus(RankedProfileView const& profile, CCPoint position, float scale, float width, bool lobbyFallback = false,
        bool showPromotion = true) {
        std::string text;
        auto color = kAccent;
        if (std::abs(profile.resultStreak) >= 2) {
            text = corum::ranked::i18n::streak(std::abs(profile.resultStreak),profile.resultStreak > 0);
            color = profile.resultStreak > 0 ? kGold : kRed;
        }
        std::string state;
        if (profile.promotion.active) {
            // Survival series: the requirement and slots already describe it; the
            // status line keeps only a streak (or stays empty).
            if (!showPromotion) lobbyFallback = false;
            else {
                state = corum::ranked::i18n::promotionMatch(profile.promotion.winsRequired);
                color = ccc3(145,255,125);
            }
        } else if (profile.demotionWarning) {
            state = tr("demotion-warning","DEMOTION WARNING"); color = kRed;
        }
        if (!state.empty()) text += (text.empty() ? "" : " · ") + state;
        if (text.empty()) {
            if (!lobbyFallback) return;
            text = tr("your-rank","YOUR RANK");
        }
        auto* label = makeLabel(text,scale,position,color,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(label,width,scale,0.17f);
        label->setID("ranked-profile-status-line"_spr); m_root->addChild(label,8);
    }

    struct PlayerCardRefs {
        CCLabelBMFont* scoreLabel = nullptr;
        CCNode* tierPlate = nullptr;
        CCNode* tierTrophy = nullptr;
    };

    void addPlayerCard(
        CCPoint center,
        std::string const& name,
        RankedProfileView const& profile,
        bool self,
        int accountId,
        PlayerCardRefs* outRefs = nullptr,
        bool combineTierAndScore = false,
        float stripHeight = 0.0f
    ) {
        auto const& tier = profile.displayedTier;
        auto const accent = tierColor(upper(tier));
        // stripHeight extends the card downwards for the VS board round state.
        auto const panelCenter = CCPoint{center.x, center.y - stripHeight / 2.0f};
        auto* panel = makeNeonPanel({150.0f, 184.0f + stripHeight}, panelCenter, accent, kDeepPanel, 246);
        m_root->addChild(panel, 1);
        if (profile.promotion.active) addPromotionAura(panelCenter, {150.0f, 184.0f + stripHeight});

        auto* ribbon = makeTextPlate(
            self ? tr("you", "YOU") : tr("opponent", "OPPONENT"),
            {74.0f, 18.0f},
            {center.x, center.y + 78.0f},
            accent,
            0.20f,
            self ? kGold : ccc3(220, 228, 242)
        );
        m_root->addChild(ribbon, 2);

        auto const& match = RankedRuntime::get().view().match;
        if (!RankedRuntime::get().isPlacement() && !match.matchId.empty()) {
            auto* face = makeTextPlate(tr("info", "INFO"), {34, 18}, {}, kAccent, 0.18f);
            auto* info = CCMenuItemSpriteExtra::create(face, this, menu_selector(CorumRankedLayer::onPlayerInfo));
            info->setID("ranked-player-info-button"_spr);
            info->setTag(self ? (match.side == "A" ? 0 : 1) : (match.side == "A" ? 1 : 0));
            info->setPosition({center.x + (self ? -55 : 55), center.y + 78}); m_menu->addChild(info, 10);
        }

        auto* iconPlate = makeNeonPanel({66.0f, 66.0f}, {center.x, center.y + 29.0f}, accent, {15, 16, 21}, 248);
        m_root->addChild(iconPlate, 2);
        if (corum::ranked::ui::hasPersistentRankAmbient(tier)) {
            auto* ambient = corum::ranked::ui::createRankAmbientFx(
                tier,
                {54.0f, 54.0f},
                match.state == "MATCH_RESULT" ? corum::ranked::ui::RankBadgeDisplay::Compact
                    : corum::ranked::ui::RankBadgeDisplay::Matched
            );
            ambient->setPosition({center.x, center.y + 29.0f});
            m_root->addChild(ambient, 3);
        }
        auto* icon = self
            ? corum::ranked::ui::PlayerIcon::createLocal()
            : corum::ranked::ui::PlayerIcon::createRemote(accountId);
        if (icon) {
            icon->setScale(1.12f);
            icon->setPosition({center.x, center.y + 29.0f});
            m_root->addChild(icon, 4);
        }
        addRankStatus(profile,{center.x,center.y + 66.0f},0.22f,136.0f);

        auto* nameLabel = makeLabel(shorten(upper(name), 15), 0.38f, {center.x, center.y - 11.0f}, corum::ranked::ui::Text, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(nameLabel, 126.0f, 0.38f, 0.25f);
        m_root->addChild(nameLabel, 4);

        auto const hasTitle = profile.equippedTitle.active;
        if (hasTitle) {
            auto* titleLabel = makeLabel(
                achievementTitleText(profile.equippedTitle),
                0.20f,
                {center.x, center.y - 27.0f},
                achievementTitleColor(profile.equippedTitle),
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(titleLabel, 134.0f, 0.20f, 0.14f);
            m_root->addChild(titleLabel, 5);
            corum::ranked::ui::styleAchievementText(titleLabel,profile.equippedTitle.level.value_or(corum::ranked::achievementRarityTier(profile.equippedTitle.rarity)));
        }

        auto const placement = profile.placementGames < profile.placementGamesRequired;
        auto const captionText = placement ? tr("placement", "PLACEMENT") : tr("rating", "RATING");
        auto const captionY = center.y - (hasTitle ? 40.0f : 32.0f);
        auto const valueY = center.y - (hasTitle ? 54.0f : 47.0f);
        // Put the warning in the status row rather than over the tier plate.
        auto* lpCaption = makeLabel(captionText, 0.23f, {center.x, captionY}, kAccent, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(lpCaption, 134.0f, 0.23f, 0.14f);
        m_root->addChild(lpCaption, 4);
        if (placement) {
            addPlacementProgress(profile.placementGames, std::max(1, profile.placementGamesRequired), {center.x, valueY});
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {center.x, valueY});
        } else {
            auto* lpLabel = makeLabel(
                combineTierAndScore
                    ? fmt::format("{} - {} LP", corum::ranked::rankTierDisplay(upper(tier)), profile.visibleRankedScore.value_or(0))
                    : fmt::format("{} LP", profile.visibleRankedScore.value_or(0)),
                0.35f,
                {center.x, valueY},
                ccc3(240, 244, 250),
                "black"
            );
            corum::ranked::ui::fitMenuLabelWidth(lpLabel, 124.0f, 0.35f, 0.22f);
            m_root->addChild(lpLabel, 4);
            if (outRefs) outRefs->scoreLabel = lpLabel;
        }

        auto* tierPlate = makeTextPlate(corum::ranked::rankTierDisplay(upper(tier)), {96.0f, 24.0f}, {center.x, center.y - 74.0f}, accent, 0.28f, accent, "black");
        m_root->addChild(tierPlate, 3);
        auto* tierTrophy = corum::ranked::ui::createAnimatedRankBadge(
            tier,
            {18.0f, 18.0f},
            corum::ranked::ui::RankBadgeDisplay::Compact
        );
        tierTrophy->setPosition({center.x - 58.0f, center.y - 74.0f});
        m_root->addChild(tierTrophy, 5);
        if (outRefs) {
            outRefs->tierPlate = tierPlate;
            outRefs->tierTrophy = tierTrophy;
        }
    }

    // Q board header for the gate screens: back, the leaning brand and a chip.
    void addGateHeader(std::string const& brandText, std::string const& lime, std::string const& chip, ccColor3B chipFill, ccColor3B chipInk, ccColor3B brandColor) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const headY = size.height - kLobbyHeader / 2.0f;
        auto* brand = makeLabel(brandText, fontScale(30.0f), {px(28.0f) + 24.0f + 8.0f, headY}, brandColor, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(brand); corum::ranked::ui::fitMenuLabelWidth(brand, size.width * 0.4f, fontScale(30.0f), 0.2f);
        brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
        auto x = brand->getPositionX() + brand->getScaledContentSize().width;
        if (!lime.empty()) {
            auto* accent = makeLabel(lime, fontScale(30.0f), {x + 6.0f, headY}, corum::ranked::ui::Accent, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(accent); m_root->addChild(accent, 6); x = accent->getPositionX() + accent->getScaledContentSize().width;
        }
        if (!chip.empty()) addChip(m_root, chip, {x + 10.0f, headY}, chipFill, chipInk, 12.0f);
    }
    // Q11 / Q12: the Ranked guide fills the screen while a download runs;
    // the dock along the bottom holds the state, the live bar and the actions.
    void addGuideCarousel() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const top = size.height - kLobbyHeader - 4.0f, bottom = 66.0f;
        if (auto* guide = corum::ranked::ui::RankedGuideCarousel::create({size.width - 2.0f * px(28.0f), top - bottom})) {
            guide->setPosition({px(28.0f), bottom}); m_root->addChild(guide, 3);
        }
    }
    // Dock: the state line over the fat live bar with its percent, a note under
    // it, and up to two actions on the right. The bar and percent are updated
    // every frame from update().
    void addDownloadDock(std::string const& state, ccColor3B stateTone, std::string const& note, ccColor3B noteTone, float progress, ccColor3B barTone) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const x0 = px(28.0f), x1 = size.width - px(28.0f) - 224.0f;
        auto* stateLabel = makeLabel(state, fontScale(14.0f), {x0, 50.0f}, stateTone, "extrabold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(stateLabel, x1 - x0 - 112.0f, fontScale(14.0f), 0.09f); m_root->addChild(stateLabel, 4);
        m_resourceProgressLabel = makeLabel(fmt::format("{}%", static_cast<int>(std::round(std::clamp(progress, 0.0f, 1.0f) * 100.0f))), fontScale(14.0f), {x1, 50.0f},
            corum::ranked::ui::Text, "black", {1.0f, 0.5f});
        m_root->addChild(m_resourceProgressLabel, 4);
        m_resourceProgressWidth = x1 - x0;
        auto* track = CCLayerColor::create({32, 35, 46, 255}, m_resourceProgressWidth, 10.0f); track->setPosition({x0, 33.0f}); track->setSkewX(20.0f); m_root->addChild(track, 3);
        m_resourceProgressFill = CCLayerColor::create({barTone.r, barTone.g, barTone.b, 255}, m_resourceProgressWidth * std::clamp(progress, 0.0f, 1.0f), 10.0f);
        m_resourceProgressFill->setPosition({x0, 33.0f}); m_resourceProgressFill->setSkewX(20.0f); m_root->addChild(m_resourceProgressFill, 4);
        if (!note.empty()) {
            auto* noteLabel = makeLabel(note, fontScale(13.0f), {x0, 18.0f}, noteTone, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(noteLabel, x1 - x0, fontScale(13.0f), 0.08f); m_root->addChild(noteLabel, 4);
        }
    }
    // Dock action slot 0 (rightmost) or 1 (left of it).
    CCMenuItemSpriteExtra* dockButton(std::string const& text, int slot, SEL_MenuHandler selector, bool filled, std::string const& id, bool enabled = true) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        constexpr float w = 106.0f;
        auto const x = size.width - px(28.0f) - w / 2.0f - static_cast<float>(slot) * (w + 6.0f);
        return boardButton(text, {w, px(54.0f)}, {x, 36.0f}, selector, 16.0f, filled, id, enabled);
    }
    void renderModUpdate() {
        using corum::ranked::ui::ModUpdateState;
        auto const& update = corum::ranked::ui::RankedModUpdateManager::get().view();
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        auto const restart = update.state == ModUpdateState::RestartRequired;
        auto const busy = update.state == ModUpdateState::Downloading || update.state == ModUpdateState::Verifying;
        // Q11: RANKED with the MOD UPDATE chip and the channel on the
        // right; the Ranked guide in the middle; the download dock below.
        addGateHeader("RANKED", "", busy ? tr("mod-updating", "UPDATING") : tr("mod-update", "MOD UPDATE"), corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, corum::ranked::ui::Accent);
        boardButton(corum::ranked::ui::RankedModUpdateManager::get().channel() == "debug" ? "DEBUG" : "RELEASE", {72.0f, px(34.0f)},
            {size.width - px(28.0f) - 36.0f, size.height - kLobbyHeader / 2.0f}, menu_selector(CorumRankedLayer::onUpdateChannel), 13.0f, false, "ranked-update-channel"_spr, !busy && !restart);
        addGuideCarousel();
        auto const message = restart ? tr("mod-restart-ready", "Update installed. Press Restart to apply it.") :
            !update.error.empty() ? update.error : busy ? tr("mod-verifying", "Downloading and verifying the mod...") :
            tr("mod-update-required", "Update the mod before entering Ranked.");
        auto const versions = fmt::format("v{} > v{}", update.installedVersion.empty() ? std::string("-") : update.installedVersion, update.targetVersion.empty() ? std::string("-") : update.targetVersion);
        addDownloadDock(fmt::format("{} · {}", message, versions), update.error.empty() ? corum::ranked::ui::Text : kRed,
            busy || restart ? tr("mod-update-auto-restart", "The game restarts as soon as the download finishes.") : std::string(), kGold,
            static_cast<float>(update.progress), corum::ranked::ui::Accent);
        if (restart) {
            // Installed: restart right away, as announced.
            runAction(CCSequence::create(CCDelayTime::create(1.2f), CallFuncExt::create([] {
                corum::ranked::ui::RankedModUpdateManager::get().restart();
            }), nullptr));
        }
        if (busy) {
            // Free play while it downloads: leave Ranked and keep playing; the
            // watcher restarts the game as soon as the install completes.
            dockButton(tr("free-play", "FREE PLAY"), 0, menu_selector(CorumRankedLayer::onModUpdateFreePlay), true, "ranked-mod-update-free-play"_spr);
        } else {
            auto const canDownload = corum::ranked::ui::RankedModUpdateManager::get().canDownload();
            dockButton(restart ? tr("restart", "RESTART") : canDownload ? tr("mod-update-download", "DOWNLOAD UPDATE") : tr("retry", "RETRY"), 0,
                restart ? menu_selector(CorumRankedLayer::onRestartModUpdate) : canDownload ? menu_selector(CorumRankedLayer::onDownloadModUpdate) : menu_selector(CorumRankedLayer::onRetry),
                true, "ranked-mod-update-action"_spr);
        }
    }

    void onUpdateChannel(CCObject*){
        if(RankedRuntime::get().view().stage!=RuntimeStage::AwaitingUpdate)return;
        if(m_updateChannelPopup)if(auto popup=m_updateChannelPopup->lock();popup&&popup->getParent())return;
        if(auto* popup=corum::ranked::ui::RankedUpdateChannelPopup::create([weak=WeakRef(this)](std::string channel){
            auto owner=weak.lock();if(!owner||!owner->isRunning()||RankedRuntime::get().view().stage!=RuntimeStage::AwaitingUpdate)return;
            owner->m_updateChannelPopup.reset();
            if(corum::ranked::ui::RankedModUpdateManager::get().selectChannel(std::move(channel))){
                owner->render();
                RankedRuntime::get().begin();
            }
        })){m_updateChannelPopup.emplace(popup);popup->show();}
    }
    void onDownloadModUpdate(CCObject*) { corum::ranked::ui::RankedModUpdateManager::get().download(); }
    void onModUpdateFreePlay(CCObject* sender) {
        ModUpdateRestartWatcher::arm();
        onCloseLayer(sender);
    }
    void onRestartModUpdate(CCObject*) { corum::ranked::ui::RankedModUpdateManager::get().restart(); }

    void renderResources() {
        addTopBack(true);
        auto const audio = RankedAudioManager::get().downloadView();
        auto& manager = corum::ranked::ui::RankAssetPackManager::get();
        auto const assets = manager.view();
        auto const downloading = audio.state == RankedResourceDownloadState::Downloading ||
            assets.state == corum::ranked::ui::RankAssetPackState::Downloading;
        auto const failed = audio.state == RankedResourceDownloadState::Failed ||
            assets.state == corum::ranked::ui::RankAssetPackState::Failed;
        auto const complete = !resourceWorkMissing();
        // Q12: RANKED RESOURCES with its state chip; the Ranked guide in the
        // middle; the dock below (state, n / m READY, the live bar, the
        // per-resource line and the actions).
        addGateHeader(tr("ranked-resources", "RANKED RESOURCES"), "",
            complete ? tr("all-resources-ready", "ALL RESOURCES READY") : tr("additional-downloads", "ADDITIONAL DOWNLOADS"),
            complete ? corum::ranked::ui::AccentFill : ccc3(32, 35, 46), complete ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, corum::ranked::ui::Text);
        addGuideCarousel();
        auto subtitle = failed ? tr("retry-before-continuing", "Retry the failed download before continuing.") :
            assets.verifying ? tr("assets-verifying", "Verifying and installing image pack...") :
            downloading ? tr("keep-open", "Keep this screen open until the download is complete.") :
            complete ? tr("everything-ready", "Everything required for Ranked is ready.") :
            tr("assets-one-download", "Download all required resources in one step.");
        auto const [ready, total] = resourceCounts();
        auto const images = !manager.required() || manager.ready() ? tr("ready", "READY") : assets.state == corum::ranked::ui::RankAssetPackState::Failed ? tr("resources-failed", "DOWNLOAD FAILED") :
            assets.verifying ? tr("assets-verifying-short", "VERIFYING") : downloading ? tr("downloading-resources", "DOWNLOADING") : tr("resources-required", "DOWNLOAD REQUIRED");
        auto note = fmt::format("{} {} · {} {} / {}", tr("assets-images", "Images"), images, tr("assets-music", "Music"), audio.ready, audio.total);
        if (failed && !assets.error.empty()) note = assets.error;
        addDownloadDock(subtitle, failed ? kRed : corum::ranked::ui::Text, note, failed ? kRed : corum::ranked::ui::TextMuted,
            static_cast<float>(resourceProgress()), failed ? kRed : corum::ranked::ui::Accent);
        // n / m READY beside the percent (updated live).
        {   auto const size = CCDirector::sharedDirector()->getWinSize();
            m_resourceCountLabel = makeLabel(fmt::format("{} / {} {}", ready, total, tr("ready", "READY")), fontScale(13.0f), {size.width - px(28.0f) - 224.0f - 44.0f, 50.0f},
                ready == total ? kGreen : corum::ranked::ui::TextMuted, "extrabold", {1.0f, 0.5f});
            m_root->addChild(m_resourceCountLabel, 4);
        }
        if (complete) {
            dockButton(tr("continue", "CONTINUE"), 0, menu_selector(CorumRankedLayer::onContinueResources), true, "ranked-resources-continue"_spr);
        } else if (downloading) {
            // Practise while it downloads: free play, or a random Ranked map
            // without an official record.
            dockButton(tr("free-play", "FREE PLAY"), 0, menu_selector(CorumRankedLayer::onResourceFreePlay), true, "ranked-resources-free-play"_spr);
            dockButton(tr("random-ranked-map", "RANDOM RANKED MAP"), 1, menu_selector(CorumRankedLayer::onRandomRankedMap), false, "ranked-resources-random-map"_spr);
        } else {
            dockButton(failed ? tr("retry-failed", "RETRY FAILED") : tr("download-all", "DOWNLOAD ALL"), 0, menu_selector(CorumRankedLayer::onDownloadAllResources),
                true, "ranked-resources-download"_spr);
        }
    }

    // P1 / P2 (placement setup), from the boards. Step 1 chooses the length
    // (3 or 5 stages) on two option panels; step 2 chooses the first stage's
    // pool beside that pool's map list and starts the run.
    void renderPlacementPoolSelection(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const headerY = size.height - kLobbyHeader / 2.0f;
        auto const stages = m_selectedPlacementStageCount;
        auto const brandX = px(28.0f) + px(48.0f) + px(16.0f);
        if (!m_placementPoolStep) {
            addTopBack(true);
            auto* brand = makeLabel(tr("placement-setup-ko", "PLACEMENT SETUP"), fontScale(30.0f), {brandX, headerY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
            m_root->addChild(makeLabel("PLACEMENT SETUP", fontScale(13.0f), {brandX + brand->getScaledContentSize().width + 10.0f, headerY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
            if (auto* badge = corum::ranked::ui::createAnimatedRankBadge("UNRANKED", {24.0f, 24.0f}, corum::ranked::ui::RankBadgeDisplay::Matched)) {
                badge->setPosition({size.width - px(28.0f) - 80.0f, headerY}); m_root->addChild(badge, 6);
            }
            m_root->addChild(makeLabel("UNRANKED", fontScale(24.0f), {size.width - px(28.0f), headerY}, ccc3(145, 160, 184), "black", {1.0f, 0.5f}), 6);
            // Title block.
            auto const titleY = size.height - kLobbyHeader - 8.0f;
            m_root->addChild(makeLabel(tr("placement-count", "CHOOSE PLACEMENT LENGTH"), fontScale(22.0f), {px(28.0f), titleY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f}), 5);
            auto* hint = makeLabel(tr("placement-count-lock", "SELECT 3 OR 5 STAGES. THIS CANNOT BE CHANGED AFTER START."), fontScale(13.0f), {px(28.0f), titleY - 13.0f},
                corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(hint, size.width - px(56.0f), fontScale(13.0f), 0.1f); m_root->addChild(hint, 5);
            // Two option panels.
            auto const top = titleY - 22.0f - px(20.0f), bottom = px(108.0f);
            auto const gap = px(20.0f), panelW = (size.width - px(56.0f) - gap) / 2.0f;
            for (int option = 0; option < 2; ++option) {
                auto const count = option == 0 ? 3 : 5;
                auto const chosen = stages == count;
                auto const x0 = px(28.0f) + static_cast<float>(option) * (panelW + gap);
                auto* face = CCNode::create(); face->setContentSize({panelW, top - bottom});
                auto* fill = CCLayerColor::create({23, 25, 34, 255}, panelW, top - bottom); face->addChild(fill, 0);
                if (chosen) {
                    auto const lime = corum::ranked::ui::Accent;
                    for (auto const& edge : std::array<CCRect, 4>{CCRect{0, 0, panelW, 1.5f}, CCRect{0, top - bottom - 1.5f, panelW, 1.5f}, CCRect{0, 0, 1.5f, top - bottom}, CCRect{panelW - 1.5f, 0, 1.5f, top - bottom}}) {
                        auto* line = CCLayerColor::create({lime.r, lime.g, lime.b, 255}, edge.size.width, edge.size.height); line->setPosition(edge.origin); face->addChild(line, 3);
                    }
                }
                auto const pad = px(24.0f);
                float y = top - bottom - px(22.0f) - 14.0f;
                auto* big = makeLabel(fmt::format(fmt::runtime(tr("placement-times", "{} STAGES")), count), fontScale(56.0f), {pad, y}, chosen ? corum::ranked::ui::Accent : corum::ranked::ui::Text, "black", {0.0f, 0.5f});
                corum::ranked::ui::leanLabel(big); face->addChild(big, 4);
                auto const tag = option == 0 ? tr("placement-quick", "QUICK") : chosen ? tr("placement-recommended-selected", "RECOMMENDED · SELECTED") : tr("placement-recommended", "RECOMMENDED");
                auto* tagLabel = makeLabel(tag, fontScale(13.0f), {}, corum::ranked::ui::AccentInk, "black");
                addChip(face, tag, {panelW - pad - tagLabel->getScaledContentSize().width - 10.0f, y}, option == 0 ? kRed : chosen ? corum::ranked::ui::AccentFill : ccc3(32, 35, 46),
                    option == 0 || chosen ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
                y -= 26.0f;
                auto* stageChip = makeLabel(fmt::format("{} STAGE", count), fontScale(15.0f), {}, corum::ranked::ui::TextMuted, "black");
                addChip(face, fmt::format("{} STAGE", count), {pad, y}, ccc3(32, 35, 46), corum::ranked::ui::TextMuted, 15.0f);
                addChip(face, fmt::format(fmt::runtime(tr("placement-map-count", "{} MAPS")), count * 2), {pad + stageChip->getScaledContentSize().width + 20.0f, y}, ccc3(32, 35, 46), corum::ranked::ui::TextMuted, 15.0f);
                y -= 30.0f;
                // Description panel (red rule for the 3-stage warning).
                auto const descH = 34.0f;
                auto* desc = CCLayerColor::create({32, 35, 46, 255}, panelW - pad * 2.0f, descH); desc->setPosition({pad, y - descH / 2.0f}); face->addChild(desc, 2);
                if (option == 0) { auto* rule = CCLayerColor::create({kRed.r, kRed.g, kRed.b, 255}, panelW - pad * 2.0f, 1.0f); rule->setPosition({pad, y + descH / 2.0f - 1.0f}); face->addChild(rule, 3); }
                auto* line1 = makeLabel(option == 0 ? tr("placement-three-detail", "3 STAGES LOWERS YOUR MAX PLACEMENT.") : tr("placement-five-detail", "STAGE GRADES ADD UP TO YOUR PLACEMENT TIER."),
                    fontScale(15.0f), {pad + 7.0f, y + 7.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(line1, panelW - pad * 2.0f - 14.0f, fontScale(15.0f), 0.1f); face->addChild(line1, 4);
                auto* line2 = makeLabel(fmt::format(fmt::runtime(tr("placement-max-tier", "MAX PLACEMENT: {}")), option == 0 ? "AQUA" : "BRONZE"), fontScale(15.0f), {pad + 7.0f, y - 8.0f},
                    option == 0 ? kRed : tierColor("BRONZE"), "extrabold", {0.0f, 0.5f});
                face->addChild(line2, 4);
                // Range: the tiers this length can place into.
                auto const rangeY = px(22.0f) + 8.0f;
                auto* rangeCap = makeLabel(tr("placement-range", "PLACEMENT RANGE"), fontScale(13.0f), {pad, rangeY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                face->addChild(rangeCap, 4);
                float chipX = pad + rangeCap->getScaledContentSize().width + 8.0f;
                for (auto const* tier : option == 0 ? std::vector<char const*>{"RED", "AQUA"} : std::vector<char const*>{"RED", "AQUA", "BRONZE"}) {
                    addChip(face, tier, {chipX, rangeY}, tierColor(tier), corum::ranked::ui::AccentInk);
                    chipX += makeLabel(tier, fontScale(13.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + 18.0f;
                }
                auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(CorumRankedLayer::onPlacementStageCount));
                item->setTag(count); item->setPosition({x0 + panelW / 2.0f, bottom + (top - bottom) / 2.0f}); m_menu->addChild(item);
                item->setID(fmt::format("{}/placement-length-{}", GEODE_MOD_ID, count));
            }
            // Footer: SKIP PLACEMENT, the summary, NEXT.
            auto const footY = px(54.0f);
            boardButton(tr("skip-placement", "SKIP PLACEMENT"), {px(210.0f), px(42.0f)}, {px(28.0f) + px(105.0f), footY}, menu_selector(CorumRankedLayer::onOpenPlacementSkip), 16.0f, false,
                "ranked-placement-skip"_spr);
            auto* summary = makeLabel(stages == 3 ? tr("placement-three-stage-warning", "WARNING: 3 STAGES LOWERS YOUR MAX PLACEMENT. MAX: AQUA. CANNOT CHANGE AFTER START.")
                    : stages == 5 ? tr("placement-five-stage-info", "5 STAGES · 10 MAPS · MAX PLACEMENT BRONZE · CANNOT CHANGE AFTER START.") : tr("select-placement-count-first", "SELECT 3 OR 5 STAGES FIRST."),
                fontScale(13.0f), {size.width / 2.0f, footY}, stages == 3 ? kRed : corum::ranked::ui::TextMuted, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(summary, size.width - px(56.0f) - px(210.0f) - px(240.0f) - 20.0f, fontScale(13.0f), 0.09f); m_root->addChild(summary, 5);
            boardButton(tr("next", "NEXT"), {px(240.0f), px(76.0f)}, {size.width - px(28.0f) - px(120.0f), footY}, menu_selector(CorumRankedLayer::onPlacementNextStep), 30.0f, true,
                "ranked-placement-next"_spr, stages == 3 || stages == 5);
        } else {
            // Step 2: back returns to the length step.
            auto* backFace = corum::ranked::ui::surface({28, 28}, {}, kSteel, kDeepPanel2, 250, false);
            auto* mark = corum::ranked::ui::chevron(11.0f, 2.5f, corum::ranked::ui::Text); mark->setScaleX(-1.0f); mark->setPosition({13.0f, 14.0f}); backFace->addChild(mark, 5);
            auto* back = CCMenuItemSpriteExtra::create(backFace, this, menu_selector(CorumRankedLayer::onPlacementStepBack));
            back->setPosition({24.0f, size.height - 24.0f}); m_menu->addChild(back);
            auto* brand = makeLabel(tr("first-stage-pool", "FIRST STAGE POOL"), fontScale(30.0f), {brandX, headerY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
            addChip(m_root, fmt::format(fmt::runtime(tr("placement-stage-chip", "{} STAGES · STAGE 1 / {}")), stages, stages),
                {brandX + brand->getScaledContentSize().width + 12.0f, headerY}, ccc3(32, 35, 46), corum::ranked::ui::TextMuted, 15.0f);
            // Left: pool grid (3 x 2), note and START.
            auto const leftW = px(470.0f), top = size.height - kLobbyHeader, bottom = px(22.0f);
            m_root->addChild(makeLabel(tr("choose-start-pool", "CHOOSE YOUR STARTING POOL."), fontScale(13.0f), {px(28.0f), top - 6.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
            auto const cellGap = px(10.0f), cellW = (leftW - cellGap * 2.0f) / 3.0f, cellH = px(74.0f);
            static constexpr std::array<char const*, 6> ratings{"1~4", "4~7", "7~10", "10~13", "13~16", "16+"};
            for (int pool = 1; pool <= 6; ++pool) {
                auto const col = (pool - 1) % 3, row = (pool - 1) / 3;
                auto const chosen = m_selectedPlacementPool == pool;
                auto* face = corum::ranked::ui::slab({cellW, cellH}, {}, chosen ? corum::ranked::ui::AccentFill : ccc3(58, 63, 78), !chosen, corum::ranked::ui::Ink);
                face->addChild(makeLabel(fmt::format("POOL {}", pool), fontScale(20.0f), {cellW / 2.0f, cellH / 2.0f + 6.0f}, chosen ? corum::ranked::ui::AccentInk : corum::ranked::ui::Text, "black"), 5);
                face->addChild(makeLabel(fmt::format(fmt::runtime(tr("pool-rating", "RATING {}")), ratings[static_cast<std::size_t>(pool - 1)]), fontScale(13.0f),
                    {cellW / 2.0f, cellH / 2.0f - 8.0f}, chosen ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, "extrabold"), 5);
                auto* item = CCMenuItemSpriteExtra::create(face, this, menu_selector(CorumRankedLayer::onPlacementPoolSelect));
                item->setTag(pool); item->setPosition({px(28.0f) + cellW / 2.0f + static_cast<float>(col) * (cellW + cellGap), top - 14.0f - cellH / 2.0f - static_cast<float>(row) * (cellH + cellGap)});
                m_menu->addChild(item);
            }
            auto const noteTop = top - 14.0f - cellH * 2.0f - cellGap - px(14.0f);
            auto* note = CCLayerColor::create({23, 25, 34, 255}, leftW, 34.0f); note->setPosition({px(28.0f), noteTop - 34.0f}); m_root->addChild(note, 1);
            m_root->addChild(makeLabel(tr("pool-note-1", "THIS ONLY SETS STAGE 1'S STARTING POOL."), fontScale(15.0f), {px(28.0f) + 8.0f, noteTop - 10.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f}), 5);
            m_root->addChild(makeLabel(tr("pool-note-2", "YOUR FINAL RANK COMES FROM THE WHOLE RUN."), fontScale(15.0f), {px(28.0f) + 8.0f, noteTop - 24.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 5);
            auto const pool = m_selectedPlacementPool;
            boardButton(pool ? fmt::format(fmt::runtime(tr("start-with-pool", "START WITH POOL {}")), pool) : tr("start-placement", "START PLACEMENT"),
                {leftW, px(76.0f)}, {px(28.0f) + leftW / 2.0f, bottom + px(38.0f)}, menu_selector(CorumRankedLayer::onStartPlacement), 30.0f, true, "ranked-placement-start"_spr,
                pool >= 1 && pool <= 6);
            // Right: the chosen pool's maps.
            auto const rx = px(28.0f) + leftW + px(20.0f), rw = size.width - px(28.0f) - rx;
            auto* panel = CCLayerColor::create({23, 25, 34, 255}, rw, top - bottom); panel->setPosition({rx, bottom}); m_root->addChild(panel, 1);
            m_root->addChild(makeLabel(pool ? fmt::format(fmt::runtime(tr("pool-maps", "POOL {} MAPS")), pool) : tr("choose-start-pool", "CHOOSE YOUR STARTING POOL."), fontScale(14.0f),
                {rx + px(18.0f), top - px(16.0f) - 6.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 5);
            auto* drawNote = makeLabel(tr("pool-maps-note", "2 MAPS ARE DRAWN AT RANDOM EACH STAGE"), fontScale(13.0f), {rx + rw - px(18.0f), top - px(16.0f) - 6.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(drawNote, rw * 0.55f, fontScale(13.0f), 0.09f); m_root->addChild(drawNote, 5);
            float rowY = top - px(16.0f) - 26.0f;
            for (auto const& map : view.placement.referenceMaps) {
                if (map.pool != pool || rowY < bottom + 14.0f) continue;
                auto* rowFill = CCLayerColor::create({32, 35, 46, 255}, rw - px(36.0f), 22.0f); rowFill->setPosition({rx + px(18.0f), rowY - 11.0f}); m_root->addChild(rowFill, 2);
                addMapDifficulty(map, {rx + px(18.0f) + 26.0f, rowY}, 48.0f, 14.0f);
                auto* title = makeLabel(map.title, fontScale(15.0f), {rx + px(18.0f) + 56.0f, rowY}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(title, rw - px(36.0f) - 100.0f, fontScale(15.0f), 0.1f); m_root->addChild(title, 5);
                auto const lengthMetadata = corum::ranked::ui::displayedMapMetadata(map.levelId, map.metadata);
                m_root->addChild(makeLabel(corum::ranked::mapLengthText(lengthMetadata), fontScale(13.0f), {rx + rw - px(18.0f) - 4.0f, rowY}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f}), 5);
                rowY -= 25.0f;
            }
        }
        auto detail = !m_localMessage.empty() ? m_localMessage : (view.error.empty() ? view.status : view.error);
        if (!detail.empty() && (!view.error.empty() || !m_localMessage.empty())) {
            auto* status = makeLabel(shorten(detail, 96), fontScale(13.0f), {size.width / 2.0f, 7.0f}, view.error.empty() ? kGold : kRed, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(status, size.width - 90.0f, fontScale(13.0f), 0.1f);
            m_mainStatusLabel = status; m_root->addChild(status, 5);
        }
    }

    void onPlacementNextStep(CCObject*) {
        if (m_selectedPlacementStageCount != 3 && m_selectedPlacementStageCount != 5) {
            m_localMessage = tr("select-placement-count-first", "SELECT 3 OR 5 STAGES FIRST.");
            return;
        }
        m_localMessage.clear(); m_placementPoolStep = true;
    }

    void onPlacementStepBack(CCObject*) { m_localMessage.clear(); m_placementPoolStep = false; }

    void renderPlacementSkipConfirm() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        addLanguageSelector();
        addScreenHeader(
            tr("skip-placement", "SKIP PLACEMENT"),
            tr("irreversible-choice", "IRREVERSIBLE CHOICE"),
            kRed,
            kRed
        );
        auto* panel = makeNeonPanel(
            {std::min(390.0f, size.width - 74.0f), 170.0f},
            {size.width / 2.0f, size.height / 2.0f - 2.0f},
            kRed,
            kDeepPanel,
            248
        );
        m_root->addChild(panel, 1);
        auto* warning = makeLabel(
            tr("skip-placement-warning", "SKIPPING ENDS PLACEMENT IMMEDIATELY AND CANNOT BE UNDONE."),
            0.29f,
            {size.width / 2.0f, size.height / 2.0f + 46.0f},
            corum::ranked::ui::Text,
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(warning, 330.0f, 0.29f, 0.18f);
        m_root->addChild(warning, 5);
        auto* result = makeLabel(
            tr("skip-placement-result", "YOU WILL BE PLACED AT RED - 150 LP."),
            0.38f,
            {size.width / 2.0f, size.height / 2.0f + 4.0f},
            kRed,
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(result, 320.0f, 0.38f, 0.22f);
        m_root->addChild(result, 5);
        addButton(
            tr("cancel", "CANCEL"),
            {size.width / 2.0f - 75.0f, size.height / 2.0f - 55.0f},
            menu_selector(CorumRankedLayer::onPlacementPoolBack),
            false,
            0.60f
        );
        addButton(
            tr("confirm-skip", "SKIP AND PLACE"),
            {size.width / 2.0f + 75.0f, size.height / 2.0f - 55.0f},
            menu_selector(CorumRankedLayer::onConfirmPlacementSkip),
            true,
            0.56f
        );
    }

    void setLobbyNodeInput(CCNode* node, bool enabled) {
        if (!node) return;
        if (auto* menu = typeinfo_cast<CCMenu*>(node)) {
            if (!enabled) corum::ranked::ui::cancelMenuTouch(menu);
            menu->setTouchEnabled(enabled);
        }
        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) setLobbyNodeInput(child,enabled);
    }

    void syncLobbyInput() {
        auto const ready = !m_modeTransition.moving() && !m_uiFadingOut && !m_modeNoticeOpen;
        auto const selected = m_modeTransition.selected();
        setLobbyNodeInput(m_lobbySurvival,ready && !selected);
        setLobbyNodeInput(m_lobbySolo,ready && !selected);
        setLobbyNodeInput(m_lobbyPanel,ready && selected.has_value() && !m_queueShown && !m_queueAnimating);
        if (m_queueMenu) setLobbyNodeInput(m_queueMenu,ready && m_queueShown && !m_queueAnimating);
        setLobbyNodeInput(m_lobbyToolbar,ready);
        if (m_menu) m_menu->setTouchEnabled(ready);
    }

    CCLayerColor* makeLobbyGroup(CCPoint position, float scale, std::string const& id) {
        auto* group = CCLayerColor::create({0,0,0,255},0,0);
        group->setPosition(position); group->setScale(scale);
        group->setCascadeOpacityEnabled(false); group->setID(id);
        m_root->addChild(group,3); return group;
    }

    // ---- L row, ported from the boards: artifact px / 2 = points, and a CSS
    // font size of N px is label scale N / 64. Lobby groups sit at the origin
    // at scale 1 and draw in screen points.
    static constexpr float kLobbyHeader = 36.0f, kLobbyFooter = 38.0f;
    static float px(float value) { return value / 2.0f; }
    static float fontScale(float cssPx) { return cssPx / 64.0f; }

    // A slanted panel from 2-point colour strips (lobby fades reach them).
    // `topInsetLeft/BottomInsetLeft` cut the left edge, the right ones the right edge.
    void addSlantedFill(CCNode* parent, float x0, float x1, float y0, float y1, float cutRightBottom, float cutLeftTop,
        ccColor3B color, GLubyte alpha = 255, int z = 1) {
        constexpr float step = 2.0f;
        for (float y = y0; y < y1; y += step) {
            auto const t = (y + step / 2.0f - y0) / std::max(1.0f, y1 - y0);
            auto const left = x0 + cutLeftTop * t, right = x1 - cutRightBottom * (1.0f - t);
            auto* strip = CCLayerColor::create({color.r, color.g, color.b, alpha}, std::max(0.0f, right - left), std::min(step, y1 - y));
            strip->setPosition({left, y}); parent->addChild(strip, z);
        }
    }

    // Skewed progress bar (.bar / .ladder segment): track and fill.
    void addSkewBar(CCNode* parent, CCPoint leftCenter, float width, float height, float fill, ccColor3B color, int z = 6) {
        auto* track = CCLayerColor::create({32, 35, 46, 255}, width, height);
        track->setPosition({leftCenter.x, leftCenter.y - height / 2.0f}); track->setSkewX(20.0f); parent->addChild(track, z);
        auto const filled = std::clamp(fill, 0.0f, 1.0f) * width;
        if (filled > 0.5f) {
            auto* bar = CCLayerColor::create({color.r, color.g, color.b, 255}, filled, height);
            bar->setPosition({leftCenter.x, leftCenter.y - height / 2.0f}); bar->setSkewX(20.0f); parent->addChild(bar, z + 1);
        }
    }

    // III · II · I ladder: passed divisions full, the current one partly.
    void addDivisionLadder(CCNode* parent, CCPoint leftCenter, float width, int division, float fill, ccColor3B color, bool numerals) {
        constexpr float gap = 2.5f, height = 6.0f;
        auto const segment = (width - 2.0f * gap) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            auto const amount = i + 1 < division ? 1.0f : i + 1 == division ? fill : 0.0f;
            addSkewBar(parent, {leftCenter.x + static_cast<float>(i) * (segment + gap), leftCenter.y}, segment, height, amount, color);
            if (numerals) {
                auto const lit = i + 1 == division;
                parent->addChild(makeLabel(i == 0 ? "III" : i == 1 ? "II" : "I", fontScale(13.0f),
                    {leftCenter.x + static_cast<float>(i) * (segment + gap) + segment / 2.0f, leftCenter.y - 9.0f},
                    lit ? corum::ranked::ui::Accent : corum::ranked::ui::TextMuted, lit ? "black" : "semibold"), 6);
            }
        }
    }

    // A chip (.chip / .chip.sm): a skewed fill with dark or muted text.
    void addChip(CCNode* parent, std::string const& text, CCPoint leftCenter, ccColor3B fill, ccColor3B ink, float cssPx = 13.0f) {
        auto* label = makeLabel(text, fontScale(cssPx), {0, 0}, ink, "black");
        auto const width = label->getScaledContentSize().width + px(20.0f), height = px(cssPx * 1.6f);
        auto* face = CCLayerColor::create({fill.r, fill.g, fill.b, 255}, width, height);
        face->setPosition({leftCenter.x, leftCenter.y - height / 2.0f}); face->setSkewX(8.0f); parent->addChild(face, 6);
        label->setPosition({leftCenter.x + width / 2.0f, leftCenter.y}); parent->addChild(label, 7);
    }

    // The L1 halves, and the selected mode's left section on L2 / L3.
    void renderModeCard(RuntimeView const& source, corum::ranked::RankedMode mode, CCLayerColor* group) {
        auto const survival = mode == corum::ranked::RankedMode::Survival;
        auto view = source;
        if (survival) {
            auto const& profile = source.survival.profile;
            view.profileTier = source.survival.loaded ? profile.displayedTier : "UNRANKED";
            view.profileScore = profile.visibleRankedScore.value_or(0);
            view.profileScoreVisible = source.survival.loaded;
            view.standing = source.survival.standing;
            view.placementGames = view.placementGamesRequired = 0;view.resultStreak=source.survival.winStreak;
            view.placementResults.clear(); view.placement = {}; view.promotion = profile.promotion;
            view.demotionWarning = profile.demotionWarning;
        }
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        m_root = group; m_menu = CCMenu::create(); m_menu->setPosition(CCPointZero); group->addChild(m_menu,20);
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const selected = m_modeTransition.selected().has_value();
        auto const nowTier = upper(view.profileTier);
        auto const tone = tierColor(nowTier);
        auto const division = nowTier.size() > 1 && nowTier.back() >= '1' && nowTier.back() <= '3' ? '4' - nowTier.back() : 0;
        auto const placement = view.placementGames < view.placementGamesRequired;
        auto const standing = view.profileScoreVisible ? view.standing : corum::ranked::StandingView{};
        auto const progress = tierProgressFor(source, survival);
        // After any LP change since the last visit the LP digits roll.
        SeenScore seen;
        if (view.profileScoreVisible && !placement) { seen = readSeen("card", survival); writeSeen("card", survival, nowTier, view.profileScore); }
        auto const lpShift = seen.valid && seen.lp != view.profileScore;
        auto const addLp = [&](CCPoint at, float scale, CCPoint anchor) {
            if (!view.profileScoreVisible || placement) {
                m_root->addChild(makeLabel("-", scale, at, corum::ranked::ui::TextMuted, "black", anchor), 6); return;
            }
            if (lpShift) if (auto* rolling = corum::ranked::ui::RollingNumber::create(seen.lp, view.profileScore, "", scale, corum::ranked::ui::Text)) {
                rolling->setPosition({at.x + rolling->getContentSize().width * (0.5f - anchor.x), at.y}); m_root->addChild(rolling, 6); return;
            }
            m_root->addChild(makeLabel(fmt::format("{}", view.profileScore), scale, at, corum::ranked::ui::Text, "black", anchor), 6);
        };
        CCPoint hitCenter; CCSize hitSize;

        if (!selected) {
            // L1 half: Survival left (right edge slants in at the bottom), Solo
            // right (left edge slants in at the top); content stacks from the bottom.
            auto const halfW = (size.width + px(56.0f)) / 2.0f;
            auto const x0 = survival ? 0.0f : size.width - halfW, x1 = survival ? halfW : size.width;
            auto const y0 = kLobbyFooter, y1 = size.height - kLobbyHeader;
            addSlantedFill(m_root, x0, x1, y0, y1, survival ? px(64.0f) : 0.0f, survival ? 0.0f : px(64.0f), corum::ranked::ui::Surface);
            auto* band = CCLayerColor::create({tone.r, tone.g, tone.b, 41}, px(200.0f), y1 - y0);
            band->setPosition({x1 - (survival ? px(96.0f) : px(44.0f)) - px(200.0f), y0}); band->setSkewX(8.0f);
            band->setID("ranked-profile-tier-band"_spr); m_root->addChild(band, 2);
            auto const trophy = px(190.0f);
            CCPoint const emblemAt {x1 - (survival ? px(84.0f) : px(28.0f)) - trophy / 2.0f, y1 - px(14.0f) - trophy / 2.0f};
            // On a tier change since the last visit the old trophy hands over.
            auto const tierShift = seen.valid && !seen.tier.empty() && seen.tier != nowTier;
            CCNode* oldEmblem = nullptr; CCNode* bloom = nullptr;
            if (tierShift) {
                bloom = corum::ranked::ui::createRankBadgeBackdrop(nowTier, {trophy * 1.3f, trophy * 1.3f}, corum::ranked::ui::RankBadgeDisplay::Matched);
                bloom->setPosition(emblemAt); m_root->addChild(bloom, 3);
                oldEmblem = corum::ranked::ui::createAnimatedRankBadge(seen.tier, {trophy * 0.82f, trophy * 0.82f}, corum::ranked::ui::RankBadgeDisplay::Large);
                oldEmblem->setPosition(emblemAt); m_root->addChild(oldEmblem, 4);
            }
            auto* emblem = corum::ranked::ui::createAnimatedRankBadge(view.profileTier, {trophy * 0.82f, trophy * 0.82f}, corum::ranked::ui::RankBadgeDisplay::Large);
            emblem->setPosition(emblemAt); m_root->addChild(emblem, 4);
            auto const cx = x0 + (survival ? px(28.0f) : px(96.0f));
            // Bottom up: ENTER, the bar block, the stat row, the title, the number row.
            float y = y0 + px(26.0f) + px(27.0f);
            auto* enter = corum::ranked::ui::slab({px(140.0f), px(54.0f)}, {cx + px(70.0f), y}, corum::ranked::ui::AccentFill);
            enter->setID("ranked-profile-enter"_spr); m_root->addChild(enter, 6);
            m_root->addChild(makeLabel(tr("mode-enter","ENTER"), fontScale(20.0f), {cx + px(58.0f), y}, corum::ranked::ui::AccentInk, "black"), 7);
            if (auto* arrow = corum::ranked::ui::chevron(5.0f, 2.0f, corum::ranked::ui::AccentInk)) { arrow->setPosition({cx + px(108.0f), y}); m_root->addChild(arrow, 7); }
            if (source.stage != RuntimeStage::Ready) enter->setVisible(false);
            y += px(27.0f) + px(16.0f);
            // Bar block (300 px wide): caption row, bar, warning or series line.
            auto const blockW = px(300.0f);
            if (placement) {
                auto const total = std::max(1, view.placement.active ? view.placement.totalStages : view.placementGamesRequired);
                auto const done = std::max(view.placementGames, static_cast<int>(view.placement.stageResults.size()));
                m_root->addChild(makeLabel(fmt::format("{} {} / {}", tr("placement", "PLACEMENT"), done, total), fontScale(13.0f), {cx, y + 12.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
                addSkewBar(m_root, {cx, y + 3.0f}, blockW, px(12.0f), static_cast<float>(done) / static_cast<float>(total), corum::ranked::ui::Accent);
            } else if (survival && !source.survival.loaded) {
                m_root->addChild(makeLabel(source.survival.error.empty() ? tr("syncing", "SYNCING...") : tr("connection-issue", "CONNECTION ISSUE"),
                    fontScale(13.0f), {cx, y + 6.0f}, kGold, "semibold", {0.0f, 0.5f}), 6);
            } else if (view.promotion.active) {
                addChip(m_root, tr("promotion-short", "PROMOTION"), {cx, y + 14.0f}, kGold, corum::ranked::ui::AccentInk);
                auto* need = makeLabel(fmt::format("{} · {}", upper(view.promotion.targetTier), corum::ranked::i18n::promotionMatch(view.promotion.winsRequired)),
                    fontScale(13.0f), {cx + 50.0f, y + 14.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(need, blockW - 50.0f, fontScale(13.0f), 0.12f); m_root->addChild(need, 6);
                auto const slots = std::clamp(view.promotion.gamesRequired, 1, 9);
                for (int i = 0; i < slots; ++i) {
                    auto const result = i < static_cast<int>(view.promotion.results.size()) ? view.promotion.results[static_cast<std::size_t>(i)] : std::string();
                    auto const win = result == "WIN", loss = result == "LOSS";
                    addChip(m_root, win ? tr("win-short", "W") : loss ? tr("lose-short", "L") : std::string("-"), {cx + static_cast<float>(i) * 22.0f, y + 1.0f},
                        win ? corum::ranked::ui::AccentFill : loss ? kRed : ccc3(32, 35, 46), win || loss ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
                }
            } else if (progress.kind != corum::ranked::TierProgressKind::Hidden) {
                auto const caption = progress.nextTier.empty() ? fmt::format("{} LP", view.profileScore)
                    : fmt::format(fmt::runtime(tr("lp-to-next-tier", "{} LP TO NEXT TIER")), progress.remaining);
                m_root->addChild(makeLabel(caption, fontScale(13.0f), {cx, y + 16.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
                if (view.demotionWarning) addChip(m_root, tr("demotion-warning", "DEMOTION WARNING"), {cx + blockW - 52.0f, y + 16.0f}, kRed, corum::ranked::ui::AccentInk);
                addSkewBar(m_root, {cx, y + 6.0f}, blockW, px(12.0f), progress.fill, tone);
                if (survival && source.survival.warningRecoveryLp)
                    m_root->addChild(makeLabel(fmt::format(fmt::runtime(tr("lp-warning-recovery","Clears at {} LP")), *source.survival.warningRecoveryLp),
                        fontScale(13.0f), {cx, y - 4.0f}, kRed, "semibold", {0.0f, 0.5f}), 6);
            }
            y += px(62.0f) + px(10.0f);
            // Stat row: TIER / LP / RANK (Survival) or STREAK (Solo).
            auto const statGap = px(28.0f);
            auto const tierText = division ? fmt::format("{} {}", corum::ranked::rankTierBase(nowTier), division == 1 ? "III" : division == 2 ? "II" : "I")
                : corum::ranked::rankTierDisplay(nowTier);
            auto* tierValue = makeLabel(tierText, fontScale(24.0f), {cx, y + 6.0f}, tone, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(tierValue, 80.0f, fontScale(24.0f), 0.2f); m_root->addChild(tierValue, 6);
            auto const lpX = cx + tierValue->getScaledContentSize().width + statGap;
            m_root->addChild(makeLabel(tr("stat-tier", "TIER"), fontScale(13.0f), {cx, y + 20.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
            m_root->addChild(makeLabel(tr("stat-lp", "LP"), fontScale(13.0f), {lpX, y + 20.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
            addLp({lpX, y + 6.0f}, fontScale(24.0f), {0.0f, 0.5f});
            auto const thirdX = lpX + 46.0f + statGap;
            auto const streak = view.resultStreak;
            auto const third = survival ? (standing.rank ? fmt::format("#{}", *standing.rank) : std::string("-"))
                : (streak >= 2 ? corum::ranked::i18n::streak(streak, true) : standing.rank ? fmt::format("#{}", *standing.rank) : std::string("-"));
            m_root->addChild(makeLabel(survival || streak < 2 ? tr("stat-rank", "RANK") : tr("stat-streak", "STREAK"), fontScale(13.0f), {thirdX, y + 20.0f},
                corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
            auto* thirdValue = makeLabel(third, fontScale(24.0f), {thirdX, y + 6.0f}, !survival && streak >= 2 ? corum::ranked::ui::Accent : corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            thirdValue->setID("ranked-profile-standing"_spr); m_root->addChild(thirdValue, 6);
            if (tierShift && oldEmblem) {
                auto* oldTier = makeLabel(corum::ranked::rankTierDisplay(seen.tier), fontScale(24.0f), {cx, y + 6.0f}, tierColor(seen.tier), "black", {0.0f, 0.5f});
                m_root->addChild(oldTier, 6);
                auto* tier = tierValue;
                auto const up = corum::ranked::rankTierOrder(nowTier) > corum::ranked::rankTierOrder(seen.tier);
                if (auto* motion = corum::ranked::ui::LobbyTierTransition::create(group, oldEmblem, emblem, oldTier, tier, bloom, up))
                    m_root->addChild(motion);
            }
            y += px(48.0f) + px(10.0f);
            // The mode title (64 px, leaning), SOLO / RANKED on two lines.
            auto const addTitle = [&](char const* text, float lineY) {
                auto* line = makeLabel(text, fontScale(64.0f), {cx, lineY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
                corum::ranked::ui::leanLabel(line); corum::ranked::ui::fitMenuLabelWidth(line, x1 - cx - px(104.0f), fontScale(64.0f), 0.4f);
                m_root->addChild(line, 8);
            };
            if (survival) { addTitle("SURVIVAL", y + 16.0f); y += 32.0f; }
            else { addTitle("RANKED", y + 15.0f); addTitle("SOLO", y + 44.0f); y += 59.0f; }
            y += px(10.0f);
            // Number row: 01 / 02 chip and the mode's line.
            addChip(m_root, survival ? "01" : "02", {cx, y + 6.0f}, corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, 15.0f);
            auto* line = makeLabel(survival ? fmt::format(fmt::runtime(tr("survival-online-ready-format","2-{} PLAYERS")), source.survivalPlayerCount)
                : tr("solo-mode-line", "1V1 · BEST OF 3"), fontScale(15.0f), {cx + 24.0f, y + 6.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(line, emblemAt.x - trophy * 0.4f - cx - 24.0f, fontScale(15.0f), 0.12f); m_root->addChild(line, 6);
            // Separate seasons: each half carries its own timer at the top.
            auto const& cardSeason = survival ? source.survivalSeason : source.soloSeason;
            if (!seasonShared(source) && cardSeason.targetAtMs.has_value()) if (auto* timer = corum::ranked::ui::SeasonTimer::create(true)) {
                timer->setPosition({cx + 50.0f, y1 - 12.0f}); m_root->addChild(timer, 8);
                if (survival) m_seasonSurvival = timer; else m_seasonSolo = timer;
            }
            hitCenter = CCPoint{(x0 + x1) / 2.0f, (y0 + y1) / 2.0f}; hitSize = CCSize{x1 - x0 - px(32.0f), y1 - y0};
        } else {
            // L2 / L3 left section: trophy, season rank, tier name, division
            // ladder, LP, streak chip and record, from the bottom up.
            auto const x0 = 0.0f, x1 = px(420.0f), y0 = 0.0f, y1 = size.height - kLobbyHeader;
            addSlantedFill(m_root, x0, x1, y0, y1, px(64.0f), 0.0f, corum::ranked::ui::Surface);
            auto const trophy = px(250.0f);
            if (auto* emblem = corum::ranked::ui::createAnimatedRankBadge(view.profileTier, {trophy * 0.8f, trophy * 0.8f}, corum::ranked::ui::RankBadgeDisplay::Large)) {
                emblem->setPosition({px(64.0f) + trophy / 2.0f, y1 - px(18.0f) - trophy / 2.0f}); m_root->addChild(emblem, 4);
            }
            auto const cx = px(28.0f);
            // Above the footer row (SETTINGS / DEBUG / FREE PLAY stay reachable).
            float y = kLobbyFooter + 8.0f;
            // Streak chip and record.
            float recordX = cx;
            if (std::abs(view.resultStreak) >= 2) {
                auto const text = corum::ranked::i18n::streak(std::abs(view.resultStreak), view.resultStreak > 0);
                addChip(m_root, text, {cx, y}, view.resultStreak > 0 ? corum::ranked::ui::AccentFill : kRed, corum::ranked::ui::AccentInk);
                recordX += 52.0f;
            }
            if (view.demotionWarning) { addChip(m_root, tr("demotion-warning", "DEMOTION WARNING"), {recordX, y}, kRed, corum::ranked::ui::AccentInk); recordX += 76.0f; }
            auto* record = makeLabel(survival ? corum::ranked::survivalRecordText(source.survival.wins, source.survival.losses, corum::ranked::i18n::isKorean())
                : corum::ranked::matchRecordText(view.profileWins, view.profileLosses, corum::ranked::i18n::isKorean()), fontScale(13.0f), {recordX, y},
                corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(record, x1 - px(64.0f) - recordX - 8.0f, fontScale(13.0f), 0.12f);
            record->setID("ranked-profile-record"_spr); m_root->addChild(record, 6);
            y += 19.0f;
            // LP (34 px) and the LP unit.
            addLp({cx, y}, fontScale(34.0f), {0.0f, 0.5f});
            m_root->addChild(makeLabel("LP", fontScale(14.0f), {cx + 62.0f, y - 2.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 6);
            y += 22.0f;
            // Division ladder (200 px) with numerals.
            if (division) addDivisionLadder(m_root, {cx, y + 6.0f}, px(200.0f), division, progress.fill, tone, true);
            y += 22.0f;
            // Tier name (56 px, leaning) and the season rank above it.
            auto* tierName = makeLabel(division ? corum::ranked::rankTierBase(nowTier) : corum::ranked::rankTierDisplay(nowTier), fontScale(56.0f), {cx, y + 12.0f}, tone, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(tierName); corum::ranked::ui::fitMenuLabelWidth(tierName, x1 - px(104.0f) - cx, fontScale(56.0f), 0.3f); m_root->addChild(tierName, 6);
            y += 30.0f;
            m_root->addChild(makeLabel(standing.rank ? fmt::format(fmt::runtime(tr("season-rank-format", "SEASON RANK #{}")), *standing.rank) : tr("season-rank-none", "SEASON RANK -"),
                fontScale(13.0f), {cx, y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
            hitCenter = CCPoint{-1000.0f, -1000.0f}; hitSize = CCSize{1.0f, 1.0f};
        }
        // Whole-half hit area on L1 (entry and OFF browsing); none on L2 / L3.
        auto* hitArea = CCNode::create(); hitArea->setContentSize(hitSize);
        auto* select = CCMenuItemSpriteExtra::create(hitArea,this,menu_selector(CorumRankedLayer::onSelectMode));
        select->setTag(static_cast<int>(mode)); select->setPosition(hitCenter);
        select->setEnabled(source.stage == RuntimeStage::Ready && !selected); select->setVisible(!selected);
        select->setID(survival ? "survival-card-select"_spr : "solo-ranked-card-select"_spr); m_menu->addChild(select);
        m_root = savedRoot; m_menu = savedMenu;
    }

    // Three slanted segments for divisions III, II and I with their numerals
    // underneath; passed divisions are full, the current one is lit.
    void addDivisionBar(int current, ccColor3B color, CCPoint center, float width) {
        constexpr float gap = 3.0f, height = 3.0f;
        auto const segment = (width - 2 * gap) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            auto const passed = i + 1 < current, lit = i + 1 == current;
            auto const fill = passed || lit ? ccc4(color.r, color.g, color.b, static_cast<GLubyte>(lit ? 255 : 130)) : ccc4(45, 49, 63, 255);
            auto* bar = CCLayerColor::create(fill, segment, height);
            bar->setPosition({center.x - width / 2.0f + i * (segment + gap), center.y - height / 2.0f});
            bar->setSkewX(20.0f); m_root->addChild(bar, 6);
            auto* numeral = makeLabel(i == 0 ? "III" : i == 1 ? "II" : "I", 0.13f, {center.x - width / 2.0f + i * (segment + gap) + segment / 2.0f, center.y - 6.5f},
                lit ? corum::ranked::ui::Accent : corum::ranked::ui::TextMuted, "black");
            m_root->addChild(numeral, 6);
        }
    }

    // Last tier / LP the lobby showed per mode, so the next visit after any
    // LP change animates from it (digit roll, tier bar, emblem on a tier change).
    struct SeenScore { std::string tier; int lp = 0; bool valid = false; };
    static std::string seenKey(char const* what, bool survival) {
        auto* account = GJAccountManager::get();
        return fmt::format("lobby-seen-{}:{}:{}", what, account ? account->m_accountID : 0, survival ? "survival" : "solo");
    }
    static SeenScore readSeen(char const* what, bool survival) {
        auto const raw = Mod::get()->getSavedValue<std::string>(seenKey(what, survival));
        auto const bar = raw.find('|');
        if (bar == std::string::npos) return {};
        auto const lp = numFromString<int>(raw.substr(bar + 1));
        if (!lp) return {};
        return {raw.substr(0, bar), lp.unwrap(), true};
    }
    static void writeSeen(char const* what, bool survival, std::string const& tier, int lp) {
        Mod::get()->setSavedValue(seenKey(what, survival), fmt::format("{}|{}", tier, lp));
    }

    // Tier bar under the start button (Solo and Survival).
    corum::ranked::TierProgressView tierProgressFor(RuntimeView const& view, bool survival,
        std::optional<SeenScore> as = std::nullopt) const {
        corum::ranked::TierProgressInput input;
        if (survival) {
            auto const& sur = view.survival;
            if (!sur.loaded || sur.tierBands.empty()) return {};
            input.tier = upper(sur.profile.displayedTier);input.score = sur.profile.visibleRankedScore.value_or(0);input.scoreKnown = true;
            input.bands = sur.tierBands;
            input.promotionActive = sur.profile.promotion.active;input.promotionGames = sur.profile.promotion.gamesRequired;
            input.promotionResults = sur.profile.promotion.results;input.demotionWarning = sur.profile.demotionWarning;
            input.rank = sur.standing.rank;input.target = sur.standing.target;
            input.ladderTier = upper(input.bands.back().tier) == input.tier;
        } else {
            if (!view.profileScoreVisible || view.soloTierBands.empty()) return {};
            input.tier = upper(view.profileTier);input.score = view.profileScore;input.scoreKnown = true;
            input.bands = view.soloTierBands;
            input.promotionActive = view.promotion.active;input.promotionGames = view.promotion.gamesRequired;
            input.promotionResults = view.promotion.results;input.demotionWarning = view.demotionWarning;
            input.rank = view.standing.rank;input.target = view.standing.target;
            input.ladderTier = corum::ranked::goldRankTier(input.tier);
        }
        // The previous visit's tier/LP, for the bar's starting point.
        if (as) {
            input.tier = as->tier;input.score = as->lp;input.promotionActive = false;input.demotionWarning = false;
            input.ladderTier = survival ? upper(input.bands.back().tier) == input.tier : corum::ranked::goldRankTier(input.tier);
        }
        auto progress = corum::ranked::tierProgress(input);
        progress.style = corum::ranked::tierBarStyle(survival, input.tier, input.ladderTier, input.rank);
        return progress;
    }
    // `compact` (lobby mode card): bar and caption only, with its own "seen"
    // record so the card and the mode panel each animate an LP change once.
    void addTierProgress(RuntimeView const& view, bool survival, CCPoint at, float width = 166.0f, bool compact = false) {
        auto progress = tierProgressFor(view, survival);
        if (progress.kind == corum::ranked::TierProgressKind::Hidden) return;
        auto const target = survival ? view.survival.profile.promotion.targetTier : view.promotion.targetTier;
        auto const* what = compact ? "cardbar" : "bar";
        auto const seen = readSeen(what, survival);
        auto const nowTier = progress.tier;auto const nowLp = progress.score;
        if (auto* bar = corum::ranked::ui::TierProgressBar::create(progress, width, upper(target), compact)) {
            bar->setPosition(at);m_root->addChild(bar, 6);
            if (seen.valid && (seen.tier != nowTier || seen.lp != nowLp)) {
                auto from = tierProgressFor(view, survival, seen);
                auto const up = seen.tier == nowTier ? nowLp >= seen.lp : corum::ranked::rankTierOrder(nowTier) > corum::ranked::rankTierOrder(seen.tier);
                if (from.kind != corum::ranked::TierProgressKind::Hidden) bar->animateFrom(std::move(from), up);
            }
        }
        writeSeen(what, survival, nowTier, nowLp);
    }

    // A ghost / filled skewed button (.btn.ghost / .btn) of a fixed size.
    CCMenuItemSpriteExtra* boardButton(std::string const& text, CCSize box, CCPoint center, SEL_MenuHandler selector, float cssPx,
        bool filled, std::string const& id, bool enabled = true) {
        auto* face = corum::ranked::ui::slab(box, {}, filled ? corum::ranked::ui::AccentFill : ccc3(58, 63, 78), !filled, corum::ranked::ui::Ink);
        auto* label = makeLabel(text, fontScale(cssPx), {box.width / 2.0f, box.height / 2.0f},
            filled ? corum::ranked::ui::AccentInk : corum::ranked::ui::Text, filled ? "black" : "extrabold");
        corum::ranked::ui::fitMenuLabelWidth(label, box.width - 10.0f, fontScale(cssPx), 0.12f); face->addChild(label, 5);
        if (!enabled) corum::ranked::ui::grayButtonVisual(face);
        auto* item = CCMenuItemSpriteExtra::create(face, this, selector);
        item->setPosition(center); item->setEnabled(enabled); item->setID(id);
        m_menu->addChild(item); return item;
    }

    // L2 / L3 right section: name and title (or the demotion warning), the
    // season ladder, the hero START slab with its line, and the tool row.
    void renderModePanel(RuntimeView const& view, corum::ranked::RankedMode mode) {
        if (!m_lobbyPanel) return;
        setLobbyNodeInput(m_lobbyPanel,false);
        m_lobbyPanel->removeAllChildrenWithCleanup(true);
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        m_lobbyContents=CCLayerColor::create({0,0,0,255},0,0);m_lobbyContents->setID("ranked-mode-panel-contents"_spr);
        m_lobbyContents->setCascadeOpacityEnabled(true);m_lobbyPanel->addChild(m_lobbyContents,10);
        m_root = m_lobbyContents; m_menu = CCMenu::create(); m_menu->setPosition(CCPointZero); m_root->addChild(m_menu,20);
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const survival = mode == corum::ranked::RankedMode::Survival;
        auto const x0 = px(420.0f) + px(12.0f), x1 = size.width - px(28.0f), width = x1 - x0;
        auto const top = size.height - kLobbyHeader - px(8.0f);
        // Row 1: name and equipped title, or the demotion warning (Survival).
        auto const warning = survival ? view.survival.profile.demotionWarning : view.demotionWarning;
        if (survival && warning) {
            auto* rule = CCLayerColor::create({32, 35, 46, 255}, width, 30.0f); rule->setPosition({x0, top - 30.0f}); m_root->addChild(rule, 1);
            auto* edge = CCLayerColor::create({kRed.r, kRed.g, kRed.b, 255}, width, 1.0f); edge->setPosition({x0, top - 1.0f}); m_root->addChild(edge, 2);
            m_root->addChild(makeLabel(tr("demotion-warning", "DEMOTION WARNING"), fontScale(16.0f), {x0 + 8.0f, top - 10.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f}), 4);
            if (view.survival.warningRecoveryLp)
                m_root->addChild(makeLabel(fmt::format(fmt::runtime(tr("lp-warning-recovery","Clears at {} LP")), *view.survival.warningRecoveryLp),
                    fontScale(13.0f), {x0 + 8.0f, top - 22.0f}, kRed, "semibold", {0.0f, 0.5f}), 4);
        } else {
            auto* account = GJAccountManager::get();
            auto* player = makeLabel(account ? std::string(account->m_username) : std::string(), fontScale(34.0f), {x0, top - 9.0f}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(player, width * 0.45f, fontScale(34.0f), 0.2f);
            player->setID("ranked-panel-player-name"_spr); m_root->addChild(player, 4);
            if (view.achievements.equippedTitle.active) {
                auto* title = makeLabel(achievementTitleText(view.achievements.equippedTitle), fontScale(15.0f),
                    {x0 + player->getScaledContentSize().width + 7.0f, top - 11.0f}, achievementTitleColor(view.achievements.equippedTitle), "extrabold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(title, width * 0.3f, fontScale(15.0f), 0.1f); m_root->addChild(title, 4);
            }
        }
        // Header buttons on the right of row 1: HISTORY and ACHIEVEMENTS / BADGES.
        boardButton(tr("history", "HISTORY"), {px(110.0f), px(34.0f)}, {x1 - px(55.0f) - px(118.0f), top - 9.0f}, menu_selector(CorumRankedLayer::onHistory),
            14.0f, false, "ranked-panel-history"_spr);
        if (survival) boardButton(tr("badges", "BADGES"), {px(110.0f), px(34.0f)}, {x1 - px(55.0f), top - 9.0f}, menu_selector(CorumRankedLayer::onBadgeCollection),
            14.0f, false, "ranked-panel-badges"_spr);
        else boardButton(tr("achievement-trophy", "TROPHY"), {px(110.0f), px(34.0f)}, {x1 - px(55.0f), top - 9.0f}, menu_selector(CorumRankedLayer::onAchievements),
            14.0f, false, "ranked-panel-achievements"_spr);

        // Row 2: season ladder with the current and next tier marks.
        auto const progress = tierProgressFor(view, survival);
        auto const ladderTop = top - 50.0f;
        if (progress.kind != corum::ranked::TierProgressKind::Hidden) {
            m_root->addChild(makeLabel(tr("season-ladder", "SEASON LADDER"), fontScale(14.0f), {x0, ladderTop}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 4);
            if (!progress.nextTier.empty()) {
                auto* remaining = makeLabel(fmt::format(fmt::runtime(tr("lp-to-next-tier", "{} LP TO NEXT TIER")), progress.remaining), fontScale(16.0f),
                    {x1, ladderTop}, kGold, "extrabold", {1.0f, 0.5f});
                m_root->addChild(remaining, 4);
            }
            auto const iconSize = px(44.0f);
            if (auto* mark = corum::ranked::ui::createAnimatedRankBadge(progress.tier, {iconSize, iconSize}, corum::ranked::ui::RankBadgeDisplay::Matched)) {
                mark->setPosition({x0 + iconSize / 2.0f, ladderTop - 22.0f}); m_root->addChild(mark, 4);
            }
            if (!progress.nextTier.empty()) if (auto* next = corum::ranked::ui::createAnimatedRankBadge(progress.nextTier, {iconSize, iconSize}, corum::ranked::ui::RankBadgeDisplay::Matched)) {
                next->setPosition({x1 - iconSize / 2.0f, ladderTop - 22.0f}); m_root->addChild(next, 4);
            }
            auto const nowTier = upper(progress.tier);
            auto const division = nowTier.size() > 1 && nowTier.back() >= '1' && nowTier.back() <= '3' ? '4' - nowTier.back() : 0;
            auto const ladderX = x0 + iconSize + px(12.0f), ladderW = width - 2.0f * (iconSize + px(12.0f));
            if (division) addDivisionLadder(m_root, {ladderX, ladderTop - 19.0f}, ladderW, division, progress.fill, tierColor(nowTier), true);
            else addSkewBar(m_root, {ladderX, ladderTop - 19.0f}, ladderW, px(12.0f), progress.fill, tierColor(nowTier));
        }

        // Row 3: the hero START slab and its line.
        auto const heroY = size.height / 2.0f - 8.0f;
        auto const heroSize = CCSize{width, px(96.0f)};
        auto const startLabel = [&](std::string const& text, bool live) {
            auto* label = makeLabel(text, fontScale(34.0f), {}, live ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, "black");
            corum::ranked::ui::fitMenuLabelWidth(label, width - 70.0f, fontScale(34.0f), 0.2f); return label;
        };
        if (!survival) {
            if (view.stage == RuntimeStage::Ready || view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue) {
                auto const playLive = view.stage == RuntimeStage::Ready && (view.soloEnabled || view.placement.active);
                auto const startText = view.placement.required
                    ? (view.placement.active && view.placement.state == "STAGE_RESULT" ? tr("continue-placement", "CONTINUE PLACEMENT") : tr("start-placement", "START PLACEMENT"))
                    : tr("start-queue", "START QUEUE");
                auto* play = CCMenuItemSpriteExtra::create(corum::ranked::ui::heroSlab(heroSize, playLive, startLabel(startText, playLive)), this,
                    view.placement.required
                        ? (view.placement.active && view.placement.state == "STAGE_RESULT" ? menu_selector(CorumRankedLayer::onContinuePlacement) : menu_selector(CorumRankedLayer::onOpenPlacement))
                        : menu_selector(CorumRankedLayer::onJoin));
                play->setPosition({x0 + width / 2.0f, heroY}); play->setEnabled(playLive); play->setID("ranked-start-button"_spr); m_menu->addChild(play);
                auto* line = makeLabel(view.placement.required
                        ? (view.placement.active && view.placement.state == "STAGE_RESULT"
                            ? fmt::format("STAGE {} / {} SAVED", view.placement.currentStageIndex, view.placement.totalStages) : tr("choose-first-pool", "CHOOSE YOUR FIRST STAGE POOL"))
                        : tr("solo-start-line", "FIND A 1V1 MATCH · ONE PRIVATE BAN · BEST OF 3"),
                    fontScale(13.0f), {x0 + width / 2.0f, heroY - heroSize.height / 2.0f - 9.0f}, corum::ranked::ui::TextMuted, "semibold");
                corum::ranked::ui::fitMenuLabelWidth(line, width, fontScale(13.0f), 0.12f); m_root->addChild(line, 4);
            } else {
                m_root->addChild(makeLabel(tr("connection-issue", "CONNECTION ISSUE"), fontScale(24.0f), {x0 + width / 2.0f, heroY}, kRed, "black"), 4);
                addButton(tr("retry", "RETRY"), {x0 + width / 2.0f, heroY - 34.0f}, menu_selector(CorumRankedLayer::onRetry), false, 0.6f);
            }
        } else {
            auto const online = view.survivalEnabled && view.survival.onlineAvailable && view.stage == RuntimeStage::Ready;
            auto const heroW = width * 0.62f;
            auto* queue = CCMenuItemSpriteExtra::create(corum::ranked::ui::heroSlab({heroW, heroSize.height}, online, startLabel(tr("start-queue", "START QUEUE"), online)),
                this, menu_selector(CorumRankedLayer::onSurvivalQueue));
            queue->setPosition({x0 + heroW / 2.0f, heroY}); queue->setEnabled(online); queue->setID("survival-start-button"_spr); m_menu->addChild(queue);
            auto const sideX = x0 + heroW + px(36.0f);
            if (!view.survival.loaded || !view.survival.error.empty()) {
                addButton(tr("retry","RETRY"), {sideX + 40.0f, heroY}, menu_selector(CorumRankedLayer::onRetrySurvival), false, 0.44f);
            } else {
                auto* players = makeLabel(online ? fmt::format(fmt::runtime(tr("survival-online-ready-format","2-{} PLAYERS")), view.survivalPlayerCount) : tr("mode-disabled","MODE DISABLED"),
                    fontScale(16.0f), {sideX, heroY + 7.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(players, x1 - sideX, fontScale(16.0f), 0.12f); m_root->addChild(players, 4);
                auto* fieldNote = makeLabel(fmt::format(fmt::runtime(tr("survival-queue-short-format","AFTER {}s, SMALLER FIELDS MAY START")), corum::ranked::SurvivalRuntime::get().queue().maxWaitSeconds),
                    fontScale(13.0f), {sideX, heroY - 8.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(fieldNote, x1 - sideX, fontScale(13.0f), 0.1f); m_root->addChild(fieldNote, 4);
            }
        }

        // Row 4: tool buttons (.btn.ghost.sm, equal widths).
        auto const hudEnabled = view.stage == RuntimeStage::Ready && !lobbyQueueActive();
        std::vector<std::tuple<std::string, SEL_MenuHandler, std::string, bool>> tools;
        if (survival) {
            tools.emplace_back(tr("elim-fx", "ELIMINATION EFFECT"), menu_selector(CorumRankedLayer::onEliminationFx), "survival-elimination-fx-button", true);
            tools.emplace_back(tr("hud-editor", "UI SETTINGS"), menu_selector(CorumRankedLayer::onHudEditor), "survival-hud-editor-button", hudEnabled);
            tools.emplace_back(tr("leaderboard", "LEADERBOARD"), menu_selector(CorumRankedLayer::onLeaderboard), "survival-leaderboard-trophy", true);
        } else {
            tools.emplace_back(tr("map-pool", "MAP POOL"), menu_selector(CorumRankedLayer::onMapPool), "ranked-map-pool-button", true);
            tools.emplace_back(tr("hud-editor", "UI SETTINGS"), menu_selector(CorumRankedLayer::onHudEditor), "ranked-hud-editor-button", hudEnabled);
            tools.emplace_back(tr("tier-guide", "TIER GUIDE"), menu_selector(CorumRankedLayer::onTierGuide), "ranked-tier-guide-gold", true);
            tools.emplace_back(tr("leaderboard", "LEADERBOARD"), menu_selector(CorumRankedLayer::onLeaderboard), "ranked-leaderboard-trophy", true);
        }
        auto const gap = px(8.0f), toolW = (width - gap * static_cast<float>(tools.size() - 1)) / static_cast<float>(tools.size());
        for (std::size_t index = 0; index < tools.size(); ++index) {
            auto const& [text, selector, id, enabled] = tools[index];
            boardButton(text, {toolW, px(42.0f)}, {x0 + toolW / 2.0f + static_cast<float>(index) * (toolW + gap), kLobbyFooter + px(21.0f) + 4.0f}, selector, 16.0f, false,
                std::string(GEODE_MOD_ID) + "/" + id, enabled);
        }
        m_root = savedRoot; m_menu = savedMenu;
    }

    // In-game HUD editor entry, next to the mode's start button. Editing is
    // only offered while idle in the lobby (not queued, matched or playing).
    void addHudEditorButton(CCPoint position, bool enabled, std::string const& id) {
        auto* face = corum::ranked::ui::surface({34,34},{},kAccent,kDeepPanel2,250,true);
        auto* mark = makeLabel("UI",0.34f,{17,21},corum::ranked::ui::Text,"black");
        corum::ranked::ui::fitMenuLabelWidth(mark,28,0.34f,0.2f); face->addChild(mark,5);
        auto* caption = makeLabel(tr("hud-editor-short","EDIT"),0.17f,{17,9},kAccent,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(caption,30,0.17f,0.1f); face->addChild(caption,5);
        if (!enabled) corum::ranked::ui::grayButtonVisual(face);
        auto* item = CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumRankedLayer::onHudEditor));
        item->setID(id); item->setPosition(position); item->setEnabled(enabled);
        item->setCascadeOpacityEnabled(true); item->setOpacity(enabled?255:100);
        m_menu->addChild(item);
    }

    // Solo map pool: browsing is read-only, so it stays available while queued.
    void addMapPoolButton(CCPoint position) {
        auto* face = corum::ranked::ui::surface({34,34},{},kAccent,kDeepPanel2,250,true);
        // A list icon from color layers: lobby fades reach CCLayerColor
        // through propagateModeOpacity, unlike a CCDrawNode.
        for (int line = 0; line < 3; ++line) {
            auto const y = 13.0f + line * 5.0f;
            auto* bullet = CCLayerColor::create({kAccent.r,kAccent.g,kAccent.b,255},3,3);bullet->setPosition({8,y});face->addChild(bullet,5);
            auto* bar = CCLayerColor::create({242,247,255,255},14,3);bar->setPosition({13,y});face->addChild(bar,5);
        }
        auto* caption = makeLabel(tr("map-pool-short","MAPS"),0.17f,{17,6},kAccent,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(caption,30,0.17f,0.1f); face->addChild(caption,5);
        auto* item = CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumRankedLayer::onMapPool));
        item->setID("ranked-map-pool-button"_spr); item->setPosition(position);
        m_menu->addChild(item);
    }

    // Survival elimination effect: equipping is a profile setting, so it stays
    // available while queued (the match snapshot takes it at admission).
    void addEliminationFxButton(CCPoint position) {
        auto* face = corum::ranked::ui::surface({34,34},{},kGold,kDeepPanel2,250,true);
        // A slashed square from color layers (lobby fades reach CCLayerColor).
        auto* left = CCLayerColor::create({255,207,74,255},8,8);left->setPosition({7,14});face->addChild(left,5);
        auto* right = CCLayerColor::create({255,207,74,255},8,8);right->setPosition({19,22});face->addChild(right,5);
        auto* slash = CCLayerColor::create({242,247,255,255},26,2);slash->ignoreAnchorPointForPosition(false);
        slash->setAnchorPoint({0.5f,0.5f});slash->setPosition({17,22});slash->setRotation(-45);face->addChild(slash,6);
        auto* caption = makeLabel(tr("elim-fx-short","FX"),0.17f,{17,6},kGold,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(caption,30,0.17f,0.1f); face->addChild(caption,5);
        auto* item = CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumRankedLayer::onEliminationFx));
        item->setID("survival-elimination-fx-button"_spr); item->setPosition(position);
        m_menu->addChild(item);
    }

    void onEliminationFx(CCObject*) {
        if (m_modeTransition.moving() || m_uiFadingOut || m_page != Page::Live) return;
        if (m_eliminationFx) if (auto open = m_eliminationFx->lock()) return;
        if (auto* popup = corum::ranked::ui::RankedEliminationFxPopup::create()) { m_eliminationFx.emplace(popup); popup->show(); }
    }

    // The map pool page is reachable while queued: a found match takes the
    // player straight back to the lobby (ban phase, round entry).
    void leaveMapPoolOnMatch(RuntimeView const& view) {
        if (m_page != Page::MapPool || view.stage != RuntimeStage::Matched) return;
        if (m_mapPoolView) if (auto pool = m_mapPoolView->lock()) pool->closePopups();
        m_page = Page::Live;
    }

    // Its own screen, like match history; map info opens as a popup over it.
    void onMapPool(CCObject*) {
        if (m_modeTransition.moving() || m_uiFadingOut || m_page != Page::Live) return;
        if (m_modeTransition.selected() != corum::ranked::RankedMode::SoloRanked) return;
        m_page = Page::MapPool;
    }

    void renderMapPool() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        // H5 header: back, the leaning MAP POOL and its mode caption.
        {   auto const headY = size.height - kLobbyHeader / 2.0f;
            auto* brand = makeLabel(tr("map-pool","MAP POOL"), fontScale(30.0f), {px(28.0f) + 24.0f + 8.0f, headY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
            m_root->addChild(makeLabel(tr("solo-ranked","SOLO RANKED"), fontScale(13.0f), {brand->getPositionX() + brand->getScaledContentSize().width + 10.0f, headY},
                corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
        }
        if (auto* view = corum::ranked::ui::RankedMapPoolView::create(size)) {
            m_root->addChild(view,3); m_mapPoolView.emplace(view);
        }
    }

    std::string lobbyMessage(RuntimeView const& view) const {
        if(m_modeTransition.selected()==corum::ranked::RankedMode::Survival && !corum::ranked::SurvivalRuntime::get().view().error.empty())return corum::ranked::i18n::text(corum::ranked::SurvivalRuntime::get().view().error,corum::ranked::SurvivalRuntime::get().view().error);
        if (!m_localMessage.empty()) return corum::ranked::i18n::text(m_localMessage,m_localMessage);
        if (!view.error.empty()) return corum::ranked::i18n::text(view.error,view.error);
        auto const selected = m_modeTransition.selected();
        if (view.stage == RuntimeStage::Ready && !view.placement.active) {
            if (!selected) {
                auto hint = tr("click-mode","CLICK THE MODE YOU WANT TO ENTER");
                if (!view.survivalEnabled) hint += " · " + tr("survival","Survival") + " " + tr("mode-disabled","MODE DISABLED");
                if (!view.soloEnabled) hint += " · " + tr("solo-ranked","Solo Ranked") + " " + tr("mode-disabled","MODE DISABLED");
                return hint;
            }
            if (*selected == corum::ranked::RankedMode::Survival)
                return view.survivalEnabled && view.survival.onlineAvailable ? tr("Ranked session ready. Join the queue when prepared.","Ranked session ready. Join the queue when prepared.") : tr("mode-off-browse","MODE DISABLED. HISTORY, LEADERBOARD AND ACHIEVEMENTS ARE AVAILABLE.");
            if (!view.soloEnabled) return tr("mode-off-browse","MODE DISABLED. HISTORY, LEADERBOARD AND ACHIEVEMENTS ARE AVAILABLE.");
        }
        return corum::ranked::i18n::runtimeStatus(view.status);
    }

    // L header (.hdr, 72 px): back, then RANKED or the mode's number chip and
    // name; season in the middle; population and KO / EN on the right.
    void addLobbyHeader(RuntimeView const& view, std::optional<corum::ranked::RankedMode> mode) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const y = size.height - kLobbyHeader / 2.0f;
        float x = px(28.0f) + px(48.0f) + px(16.0f);
        if (mode) {
            addChip(m_root, *mode == corum::ranked::RankedMode::Survival ? "01" : "02", {x, y}, corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, 15.0f);
            x += 22.0f;
        }
        auto* brand = makeLabel(!mode ? "RANKED" : *mode == corum::ranked::RankedMode::Survival ? "SURVIVAL" : "SOLO RANKED", fontScale(30.0f), {x, y},
            corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
        // KO / EN (.lang): two 24 x 22 boxes at the right edge.
        auto const korean = corum::ranked::i18n::isKorean();
        auto const langBox = [&](char const* text, bool on, float cx, SEL_MenuHandler selector, char const* id) {
            auto* face = CCLayerColor::create(on ? ccc4(166, 222, 36, 255) : ccc4(0, 0, 0, 0), px(48.0f), px(44.0f));
            auto* border = CCLayerColor::create({43, 47, 60, 255}, px(48.0f), 1.0f); border->setPosition({0.0f, px(44.0f) - 1.0f}); face->addChild(border);
            auto* bottom = CCLayerColor::create({43, 47, 60, 255}, px(48.0f), 1.0f); face->addChild(bottom);
            face->addChild(makeLabel(text, fontScale(14.0f), {px(24.0f), px(22.0f)}, on ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, "black"), 2);
            auto* item = CCMenuItemSpriteExtra::create(face, this, selector);
            item->setPosition({cx, y}); item->setID(std::string(GEODE_MOD_ID) + "/" + id); m_menu->addChild(item);
        };
        langBox("EN", !korean, size.width - px(28.0f) - px(24.0f), menu_selector(CorumRankedLayer::onEnglish), "ranked-language-english");
        langBox("KO", korean, size.width - px(28.0f) - px(72.0f), menu_selector(CorumRankedLayer::onKorean), "ranked-language-korean");
        if (auto* population = corum::ranked::ui::LobbyPopulationView::create()) {
            population->setID("ranked-lobby-population"_spr); population->setPosition({size.width - px(28.0f) - px(96.0f) - 60.0f, y});
            population->updatePopulation({view.lobbyOffline, view.lobbyOnline, view.lobbyPopulationKnown}); m_root->addChild(population, 7);
        }
        // Season: the shared timer, or the open mode's own timer, centred.
        m_seasonShared = nullptr; m_seasonSolo = nullptr; m_seasonSurvival = nullptr;
        auto const& season = mode && *mode == corum::ranked::RankedMode::Survival ? view.survivalSeason : view.soloSeason;
        if ((seasonShared(view) || mode) && season.targetAtMs) if (auto* timer = corum::ranked::ui::SeasonTimer::create(false)) {
            timer->setPosition({size.width / 2.0f + 20.0f, y}); m_root->addChild(timer, 7);
            if (seasonShared(view)) m_seasonShared = timer;
            else if (*mode == corum::ranked::RankedMode::Survival) m_seasonSurvival = timer; else m_seasonSolo = timer;
        }
    }

    // Footer face (.foot button): fill, 3 px top rule and caption; resized in place.
    static void layoutFooterFace(CCNode* face, float width) {
        constexpr float height = 26.0f;
        face->setContentSize({width, height});
        if (auto* fill = typeinfo_cast<CCLayerColor*>(face->getChildByTag(1))) fill->setContentSize({width, height});
        if (auto* rule = typeinfo_cast<CCLayerColor*>(face->getChildByTag(2))) { rule->setContentSize({width, 1.5f}); rule->setPosition({0.0f, height - 1.5f}); }
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(face->getChildByTag(3))) {
            label->setPosition({width / 2.0f, height / 2.0f}); corum::ranked::ui::fitMenuLabelWidth(label, width - 12.0f, fontScale(16.0f), 0.12f);
        }
    }
    CCNode* makeFooterFace(std::string const& text) {
        auto* face = CCNode::create(); face->setAnchorPoint({0.5f, 0.5f});
        auto* fill = CCLayerColor::create({23, 25, 34, 255}, 10.0f, 26.0f); fill->setTag(1); face->addChild(fill, 0);
        auto* rule = CCLayerColor::create({43, 47, 60, 255}, 10.0f, 1.5f); rule->setTag(2); face->addChild(rule, 1);
        auto* label = makeLabel(text, fontScale(16.0f), {}, corum::ranked::ui::Text, "extrabold"); label->setTag(3); face->addChild(label, 2);
        layoutFooterFace(face, 100.0f); return face;
    }

    void renderLobbyTools(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        m_lobbyToolbar = CCMenu::create(); m_lobbyToolbar->setPosition(CCPointZero);
        m_lobbyToolbar->setID("ranked-mode-toolbar"_spr); m_root->addChild(m_lobbyToolbar,20);
        auto* savedMenu = m_menu; m_menu = m_lobbyToolbar;
        // L1 footer (.foot): HISTORY, ACHIEVEMENTS (with the unclaimed badge),
        // LEADERBOARD and SETTINGS share the width.
        auto* historyVisual = makeFooterFace(tr("history", "HISTORY"));
        auto* history = CCMenuItemSpriteExtra::create(historyVisual,this,menu_selector(CorumRankedLayer::onHistory));
        history->setID("ranked-lobby-history"_spr); m_menu->addChild(history);
        auto* trophyVisual = makeFooterFace(tr("achievements", "ACHIEVEMENTS"));
        auto* trophy = CCMenuItemSpriteExtra::create(trophyVisual,this,menu_selector(CorumRankedLayer::onAchievements));
        trophy->setID("ranked-achievement-trophy"_spr); m_menu->addChild(trophy);
        auto const badgeText = corum::ranked::achievementBadgeText(view.achievements.unclaimedRewardCount);
        if (!badgeText.empty()) {
            auto* badge = corum::ranked::ui::menuPill(badgeText,{badgeText == "99+" ? 30.0f : 20.0f,14},{0,0},kRed,0.22f,corum::ranked::ui::Text,true);
            badge->setID("ranked-achievement-notification-badge"_spr); badge->setTag(4); trophyVisual->addChild(badge,10);
        }
        auto* leaderboard = CCMenuItemSpriteExtra::create(makeFooterFace(tr("leaderboard", "LEADERBOARD")),this,menu_selector(CorumRankedLayer::onLeaderboard));
        leaderboard->setID("ranked-lobby-leaderboard"_spr); m_menu->addChild(leaderboard);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (view.stage == RuntimeStage::Ready) {
            auto* debug = addButton(tr("debug","DEBUG"),{96,30},menu_selector(CorumRankedLayer::onDebugMenu),true,0.54f);
            debug->setID("ranked-lobby-debug"_spr);
            debug->setVisible(m_modeTransition.selected().has_value());
        }
#endif
        if (view.stage == RuntimeStage::Ready) {
            auto* freePlay = addButton(tr("survival-free-play","FREE PLAY"),{96,30},menu_selector(CorumRankedLayer::onQueueFreePlay),false,0.54f,true);
            freePlay->setID("ranked-lobby-free-play"_spr); freePlay->setVisible(false);
        }
        m_lobbyToolbar->setVisible(true);
        m_menu = savedMenu;
        // Status or error line under the header (no status bar on the boards).
        auto detail=lobbyMessage(view);
        auto* status=makeLabel(shorten(detail,90),fontScale(13.0f),{size.width/2,size.height-kLobbyHeader-6.0f},view.error.empty()?corum::ranked::ui::TextMuted:kRed,"semibold");
        m_mainStatusLabel=status;status->setVisible(!view.error.empty());m_root->addChild(status,21);
        placeLobbyFooter();
    }

    // Footer row: on L1 the four navigation buttons; with a mode open they
    // give way to [DEBUG] and, while queued, [FREE PLAY] plus SETTINGS.
    void placeLobbyFooter() {
        if (!m_lobbyToolbar) return;
        auto const size=CCDirector::sharedDirector()->getWinSize();
        auto const mode=m_modeTransition.selected();
        std::vector<CCNode*> row;
        if(auto* debug=typeinfo_cast<CCMenuItemSpriteExtra*>(m_lobbyToolbar->getChildByID("ranked-lobby-debug"_spr)); debug && debug->isVisible()) row.push_back(debug);
        if(auto* freePlay=typeinfo_cast<CCMenuItemSpriteExtra*>(m_lobbyToolbar->getChildByID("ranked-lobby-free-play"_spr))) {
            auto const& survival=corum::ranked::SurvivalRuntime::get();
            auto const visible=m_page==Page::Live && mode && m_queueShown && !m_queueAnimating &&
                (*mode==corum::ranked::RankedMode::Survival ? survival.canQueueFreePlay() : survival.canSoloQueueFreePlay());
            if(freePlay->isVisible()!=visible)freePlay->setVisible(visible);
            if(visible) row.push_back(freePlay);
        }
        auto const navVisible=!mode;
        std::vector<CCMenuItemSpriteExtra*> nav;
        for(auto const* id:{"ranked-lobby-history","ranked-achievement-trophy","ranked-lobby-leaderboard"})
            if(auto* item=typeinfo_cast<CCMenuItemSpriteExtra*>(m_lobbyToolbar->getChildByID(std::string(GEODE_MOD_ID)+"/"+id))){item->setVisible(navVisible);if(navVisible)nav.push_back(item);}
        auto* settings=m_settingsMenu?typeinfo_cast<CCMenuItemSpriteExtra*>(m_settingsMenu->getChildByID("ranked-settings-button"_spr)):nullptr;
        auto const left=px(28.0f), right=size.width-px(28.0f), gap=4.0f, y=px(4.0f)+kLobbyFooter/2.0f-1.0f;
        auto const count=static_cast<int>(row.size()+nav.size())+(settings?1:0);
        auto const slot=(right-left-gap*static_cast<float>(std::max(0,count-1)))/static_cast<float>(std::max(1,count));
        float x=left;
        for(auto* item:row){item->setPosition({x+slot/2.0f,y});x+=slot+gap;}
        for(auto* item:nav){
            if(auto* face=item->getNormalImage()){layoutFooterFace(face,slot);item->setContentSize(face->getContentSize());face->setPosition({slot/2.0f,13.0f});
                if(auto* badge=face->getChildByTag(4))badge->setPosition({slot-12.0f,24.0f});}
            item->setPosition({x+slot/2.0f,y});x+=slot+gap;
        }
        if(settings)settings->setPosition({x+slot/2.0f,y});
        m_statusTextWidth=std::max(80.0f,size.width-120.0f);
        if(m_mainStatusLabel) corum::ranked::ui::fitMenuLabelWidth(m_mainStatusLabel,m_statusTextWidth,fontScale(13.0f),.12f);
    }

    void resumeModeSelectionAfterQueue(RuntimeView const& view) {
        if (!m_returnToModesAfterQueue) return;
        // Match assignment wins a concurrent Leave. Keep the recovery path.
        if (view.stage == RuntimeStage::Matched) { m_returnToModesAfterQueue = false; return; }
        if (view.stage != RuntimeStage::Ready || m_page != Page::Live || m_uiFadingOut ||
            m_modeTransition.moving() || !m_lobbyPanel || m_queueAnimating || m_queueShown || lobbyQueueActive()) return;
        m_returnToModesAfterQueue = false;
        auto const mode = m_modeTransition.selected();
        if (mode && m_modeTransition.back()) animateModeLobby(*mode,false);
    }

    void updateModeLobbyHeader() {
        if (m_lobbyToolbar) {
            m_lobbyToolbar->setVisible(true);
            if (auto* debug = m_lobbyToolbar->getChildByID("ranked-lobby-debug"_spr))
                debug->setVisible(m_modeTransition.selected().has_value());
        }
        placeLobbyFooter();
    }

    // L1 <-> L2 / L3 are separate pages: the lobby fades out, the page is
    // rebuilt for the new selection, and it fades in.
    void animateModeLobby(corum::ranked::RankedMode mode, bool opening) {
        (void)mode; (void)opening;
        if(m_lobbyHover)m_lobbyHover->setActive(false);
        setLobbyNodeInput(m_lobbySurvival,false); setLobbyNodeInput(m_lobbySolo,false); setLobbyNodeInput(m_lobbyPanel,false);
        if (m_menu) { corum::ranked::ui::cancelMenuTouch(m_menu); m_menu->setTouchEnabled(false); }
        if (m_settingsMenu) { corum::ranked::ui::cancelMenuTouch(m_settingsMenu); m_settingsMenu->setTouchEnabled(false); }
        setLobbyNodeInput(m_lobbyToolbar,false);
        m_renderedUiKey = presentationKey();
        constexpr float out = 0.16f;
        for (auto* group : {m_lobbySurvival, m_lobbySolo, m_lobbyPanel}) {
            if (!group || !group->isVisible()) continue;
            corum::ranked::ui::fadeModeLobbySurfaces(group, out, false);
            group->runAction(corum::ranked::ui::ModeLobbyFade::create(out, 0, corum::ranked::ui::ModeLobbyEase::SineIn));
        }
        runAction(CCSequence::create(CCDelayTime::create(out + 0.02f), CallFuncExt::create([weak=WeakRef(this)] {
            auto self = weak.lock(); if (!self || !self->getParent()) return;
            self->m_modeTransition.finish();
            self->m_renderedUiKey.clear();
            self->render();
            for (auto* group : {self->m_lobbySurvival, self->m_lobbySolo, self->m_lobbyPanel}) {
                if (!group || !group->isVisible()) continue;
                corum::ranked::ui::setModeLobbyOpacity(group, 0);
                corum::ranked::ui::fadeModeLobbySurfaces(group, 0.22f, true);
                group->runAction(corum::ranked::ui::ModeLobbyFade::create(0.22f, 255, corum::ranked::ui::ModeLobbyEase::SineOut));
            }
            self->syncLobbyInput(); self->refreshLobbyHover();
        }), nullptr));
    }

    void refreshLobbyHover() {
        if(m_lobbyHover){m_lobbyHover->detach();m_lobbyHover->removeFromParentAndCleanup(true);m_lobbyHover=nullptr;}
        m_lobbyHover=corum::ranked::ui::LobbyHover::create(m_root,m_settingsMenu,!m_modeTransition.selected());
        if(m_lobbyHover)m_root->addChild(m_lobbyHover,100);
    }

    void enterMode(corum::ranked::RankedMode mode) {
        if (m_uiFadingOut || m_page != Page::Live || !m_lobbyPanel ||
            RankedRuntime::get().view().stage != RuntimeStage::Ready) return;
        if (!m_modeTransition.select(mode)) return;
        renderModePanel(RankedRuntime::get().view(),mode);
        if (mode == corum::ranked::RankedMode::Survival && !RankedRuntime::get().view().survival.loaded) RankedRuntime::get().fetchSurvivalProfile();
        if (mode == corum::ranked::RankedMode::Survival) corum::ranked::SurvivalThumbnailProvider::get().prewarm();
        animateModeLobby(mode,true);
    }

    void onSelectMode(CCObject* sender) {
        if (m_uiFadingOut || m_page != Page::Live || !sender || !m_lobbyPanel ||
            m_modeNoticeOpen || m_modeTransition.moving() || m_modeTransition.selected()) return;
        if(m_lobbyHover)m_lobbyHover->press(typeinfo_cast<CCMenuItemSpriteExtra*>(sender));
        auto const mode = sender->getTag() == 0 ? corum::ranked::RankedMode::Survival : corum::ranked::RankedMode::SoloRanked;
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Ready) return;
        if (mode == corum::ranked::RankedMode::Survival ? view.survivalEnabled : view.soloEnabled) {
            enterMode(mode);
            return;
        }
        auto const generation = ++m_modeNoticeGeneration;
        auto* popup = corum::ranked::ui::RankedModeDisabledPopup::create(
            mode == corum::ranked::RankedMode::Survival ? "SURVIVAL" : "SOLO RANKED",
            [weak = WeakRef(this), mode, generation](bool browse) {
                auto self = weak.lock();
                if (!self || !self->getParent() || self->m_modeNoticeGeneration != generation) return;
                self->m_modeNoticeOpen = false;
                self->m_modeNotice.reset();
                if (browse) self->enterMode(mode);
                self->syncLobbyInput();
            });
        if (!popup) { m_localMessage = tr("mode-off-browse", "MODE DISABLED. HISTORY, LEADERBOARD AND ACHIEVEMENTS ARE AVAILABLE."); return; }
        m_modeNotice.emplace(popup);
        m_modeNoticeOpen = true;
        syncLobbyInput();
        if (m_settingsMenu) {
            corum::ranked::ui::cancelMenuTouch(m_settingsMenu);
            m_settingsMenu->setTouchEnabled(false);
        }
        popup->show();
    }

    void onRetrySurvival(CCObject*) { RankedRuntime::get().fetchSurvivalProfile(); }

    // ---- Queue expansion -------------------------------------------------
    bool lobbyQueueActive() const {
        auto const selected = m_modeTransition.selected();
        if (!selected) return false;
        if (*selected == corum::ranked::RankedMode::Survival) {
            auto const& survival = corum::ranked::SurvivalRuntime::get();
            auto const& status = survival.queue().status;
            return status == "QUEUED" || status == "JOINING" || (status == "MATCHED" && (!survival.active() || survival.matchFoundHold()));
        }
        auto const& view = RankedRuntime::get().view();
        if (view.debugBotCreating) return false;
        return view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue;
    }

    CCLayerColor* selectedLobbyCard() const {
        auto const selected = m_modeTransition.selected();
        if (!selected) return nullptr;
        return *selected == corum::ranked::RankedMode::Survival ? m_lobbySurvival : m_lobbySolo;
    }

    // Card-local x of the action card's centre; the expanded card spans both.
    float queueCenterX() const {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const layout = corum::ranked::modeLobbyLayout(size.width,size.height);
        return (layout.rightX - layout.leftX) / layout.scale;
    }
    float queueExpandedWidth() const { return queueCenterX() + 188.0f; }

    void setLobbyCardWidth(CCLayerColor* card, float width) { resizeLobbyCard(card,width); }

    // Widen the profile surface to the right, keeping its left edge fixed.
    static void resizeLobbyCard(CCLayerColor* card, float width) {
        if (!card) return;
        auto* panel = card->getChildByID("ranked-profile-panel"_spr);
        if (!panel) return;
        constexpr float height = 210.0f, left = -94.0f;
        corum::ranked::ui::layoutSurface(panel,{width,height});
        panel->setPosition({left + width / 2.0f,0});
        if (auto* glow = card->getChildByID("ranked-promotion-aura-glow"_spr)) {
            glow->setContentSize({std::max(24.0f,width - 24.0f),glow->getContentSize().height});
            glow->setPositionX(left + 12.0f);
        }
        if (auto* rail = card->getChildByID("ranked-promotion-aura-rail-right"_spr)) rail->setPositionX(left + width - 6.5f);
    }

    CCMenuItemSpriteExtra* queueButton(std::string const& caption, CCPoint position, SEL_MenuHandler selector, ccColor3B color, float width) {
        auto* face = corum::ranked::ui::surface({width,26},{},color,corum::ranked::ui::SurfaceRaised,255,true);
        auto* label = makeLabel(caption,0.26f,{width / 2.0f,13},corum::ranked::ui::Text,"extrabold");
        corum::ranked::ui::fitMenuLabelWidth(label,width - 10.0f,0.26f,0.16f); face->addChild(label,5);
        auto* item = CCMenuItemSpriteExtra::create(face,this,selector);
        item->setPosition(position); m_queueMenu->addChild(item); return item;
    }

    // L4 / L5 queue pages, ported from the boards in screen points. The page
    // covers the lobby below the header; the header gains the MATCHING chip.
    void buildQueueContents(corum::ranked::RankedMode mode, CCLayerColor* card) {
        (void)card;
        if (m_queueContents) m_queueContents->removeFromParentAndCleanup(true);
        m_queueSlots.clear(); m_queuePlayers = m_queueWait = m_queueMinimum = m_queueHint = m_queueHeading = nullptr; m_queueLeave = m_queueFill = nullptr;
        m_queueRingCyan = m_queueRingGold = m_queueSweep = m_queueOpponentBox = nullptr; m_queueCenter = m_queueOpponentMark = m_queueOpponentLabel = nullptr;
        m_queueRadar = nullptr; m_queueRangeLabel = nullptr; m_queueFillBar = nullptr;
        auto const win = CCDirector::sharedDirector()->getWinSize();
        auto const top = win.height - kLobbyHeader, bottom = kLobbyFooter;
        m_queueContents = CCLayerColor::create({0,0,0,255},0,0);
        m_queueContents->setCascadeOpacityEnabled(true); m_queueContents->setPosition({0.0f, 0.0f});
        m_queueContents->setID("ranked-queue-contents"_spr); m_root->addChild(m_queueContents,12);
        m_queueMenu = CCMenu::create(); m_queueMenu->setPosition(CCPointZero); m_queueMenu->setID("ranked-queue-menu"_spr);
        m_queueContents->addChild(m_queueMenu,20);
        auto* savedRoot = m_root; auto* savedMenu = m_menu; m_root = m_queueContents; m_menu = m_queueMenu;
        auto* ground = CCLayerColor::create({corum::ranked::ui::Ink.r,corum::ranked::ui::Ink.g,corum::ranked::ui::Ink.b,255},win.width,top);
        ground->setID("ranked-queue-page-ground"_spr); m_queueContents->addChild(ground,0);
        auto const survivalMode = mode == corum::ranked::RankedMode::Survival;
        // Header chip after the mode name: MATCHING / MATCH FOUND.
        {
            float chipX = win.width / 2.0f - 120.0f;
            if (auto* brand = savedRoot->getChildByID("ranked-screen-title"_spr)) chipX = brand->getPositionX() + brand->getScaledContentSize().width + 12.0f;
            auto* face = CCLayerColor::create({166, 222, 36, 255}, 46.0f, 12.0f);
            face->setPosition({chipX, win.height - kLobbyHeader / 2.0f - 6.0f}); face->setSkewX(8.0f); m_queueContents->addChild(face, 5);
            auto* heading = makeLabel(tr("matching-short", "MATCHING"), fontScale(13.0f), {chipX + 23.0f, win.height - kLobbyHeader / 2.0f},
                corum::ranked::ui::AccentInk, "black");
            corum::ranked::ui::fitMenuLabelWidth(heading, 40.0f, fontScale(13.0f), 0.1f); m_queueContents->addChild(heading, 6); m_queueHeading = heading;
        }
        auto* account = GJAccountManager::get();
        auto const& live = RankedRuntime::get().view();
        if (!survivalMode) {
            // ---- L4 (Solo): profile section | radar | search column.
            auto const x1 = px(330.0f);
            addSlantedFill(m_queueContents, 0.0f, x1, 0.0f, top, px(48.0f), 0.0f, corum::ranked::ui::Surface);
            auto const tier = live.profileTier;
            if (auto* badge = corum::ranked::ui::createAnimatedRankBadge(tier, {px(190.0f) * 0.82f, px(190.0f) * 0.82f}, corum::ranked::ui::RankBadgeDisplay::Large)) {
                badge->setPosition({px(50.0f) + px(95.0f), top - px(20.0f) - px(95.0f)}); m_queueContents->addChild(badge, 4);
            }
            auto const cx = px(28.0f);
            float y = bottom + px(24.0f) + 6.0f;
            m_queueContents->addChild(makeLabel(fmt::format("{}", live.profileScore), fontScale(24.0f), {cx, y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f}), 5);
            m_queueContents->addChild(makeLabel("LP", fontScale(14.0f), {cx + 44.0f, y - 1.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 5);
            y += 16.0f;
            auto* tierLabel = makeLabel(corum::ranked::rankTierDisplay(upper(tier)), fontScale(24.0f), {cx, y}, tierColor(upper(tier)), "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(tierLabel); m_queueContents->addChild(tierLabel, 5);
            y += 18.0f;
            auto* name = makeLabel(account ? std::string(account->m_username) : std::string(), fontScale(34.0f), {cx, y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(name, x1 - px(48.0f) - cx - 6.0f, fontScale(34.0f), 0.2f); m_queueContents->addChild(name, 5);
            // Radar (400 px) centred between the sections; drawQueueArena draws
            // its rings at radius 46, so the node is scaled up to 200 points.
            auto const rightW = px(300.0f);
            CCPoint const ring {(x1 + win.width - rightW) / 2.0f, (bottom + top) / 2.0f};
            m_queueRadar = CCDrawNode::create();
            m_queueRadar->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            m_queueRadar->setPosition(ring); m_queueRadar->setScale(std::min(100.0f, (top - bottom) / 2.0f - 8.0f) / 46.0f);
            m_queueContents->addChild(m_queueRadar,3);
            m_queueSurvivalArena = false;
            m_radarBlips.clear(); m_queueSweepAngle = 0.0f; m_radarFrameAt = {};
            m_queueWait = makeLabel("", fontScale(34.0f), {ring.x, ring.y + 5.0f}, corum::ranked::ui::Text, "black"); m_queueContents->addChild(m_queueWait, 5);
            m_queueContents->addChild(makeLabel(tr("waiting-time", "WAIT"), fontScale(13.0f), {ring.x, ring.y - 12.0f}, corum::ranked::ui::TextMuted, "semibold"), 5);
            // Search column (300 px, right padding 28 px), vertically centred.
            auto const rx = win.width - rightW, rw = rightW - px(28.0f);
            float ry = (bottom + top) / 2.0f + 70.0f;
            m_queueCenter = makeLabel(tr("searching","SEARCHING..."), fontScale(14.0f), {rx, ry}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f});
            m_queueContents->addChild(m_queueCenter, 5);
            auto* finding = makeLabel(tr("finding-opponent", "LOOKING FOR AN OPPONENT"), fontScale(20.0f), {rx, ry - 14.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(finding, rw, fontScale(20.0f), 0.14f); m_queueContents->addChild(finding, 5);
            m_queueRangeLabel = makeLabel("", fontScale(20.0f), {rx, ry - 28.0f}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f}); m_queueContents->addChild(m_queueRangeLabel, 5);
            ry -= 50.0f;
            m_queueContents->addChild(makeLabel(tr("search-range","SEARCH RANGE"), fontScale(13.0f), {rx, ry}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
            auto* track = CCLayerColor::create({32,35,46,255}, rw, px(12.0f)); track->setPosition({rx, ry - 11.0f}); track->setSkewX(20.0f); m_queueContents->addChild(track, 4);
            m_queueFillBar = CCLayerColor::create({corum::ranked::ui::Accent.r,corum::ranked::ui::Accent.g,corum::ranked::ui::Accent.b,255}, 0.0f, px(12.0f));
            m_queueFillBar->setPosition({rx, ry - 11.0f}); m_queueFillBar->setSkewX(20.0f); m_queueFillBar->setID("ranked-queue-range-bar"_spr); m_queueContents->addChild(m_queueFillBar, 5);
            m_queueFillWidth = rw;
            ry -= 34.0f;
            // Opponent panel: a dark cube, OPPONENT, revealed when matched.
            auto* panel = CCLayerColor::create({23,25,34,255}, rw, 28.0f); panel->setPosition({rx, ry - 14.0f}); m_queueContents->addChild(panel, 3);
            m_queueOpponentBox = CCLayerColor::create({43,47,60,255}, 14.0f, 14.0f); m_queueOpponentBox->setPosition({rx + 8.0f, ry - 7.0f}); m_queueContents->addChild(m_queueOpponentBox, 5);
            m_queueOpponentMark = makeLabel("?", fontScale(16.0f), {rx + 15.0f, ry}, corum::ranked::ui::TextMuted, "black"); m_queueContents->addChild(m_queueOpponentMark, 6);
            m_queueOpponentLabel = makeLabel(tr("opponent","OPPONENT"), fontScale(16.0f), {rx + 30.0f, ry + 4.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f});
            m_queueContents->addChild(m_queueOpponentLabel, 5);
            m_queueContents->addChild(makeLabel(tr("revealed-on-match", "REVEALED WHEN MATCHED"), fontScale(13.0f), {rx + 30.0f, ry - 6.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
            ry -= 30.0f;
            auto* note = makeLabel(tr("match-moves-now", "A FOUND MATCH STARTS RIGHT AWAY"), fontScale(13.0f), {rx + rw / 2.0f, ry}, corum::ranked::ui::TextMuted, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(note, rw, fontScale(13.0f), 0.1f); m_queueContents->addChild(note, 5);
            m_queueLeave = boardButton(tr("cancel-matching", "CANCEL MATCHING"), {rw, px(42.0f)}, {rx + rw / 2.0f, ry - 20.0f}, menu_selector(CorumRankedLayer::onLeave),
                16.0f, false, "ranked-queue-leave"_spr);
        } else {
            // ---- L5 (Survival): counts row, fat bar, seat grid, cancel row.
            auto const x0 = px(28.0f), x1 = win.width - px(28.0f), width = x1 - x0;
            auto const maximum = std::clamp(corum::ranked::SurvivalRuntime::get().queue().maximumPlayers,2,64);
            m_queueSeatMaximum = maximum;
            float y = top - px(4.0f);
            auto const caption = [&](std::string const& text, float x, CCPoint anchor) {
                m_queueContents->addChild(makeLabel(text, fontScale(14.0f), {x, y - 4.0f}, corum::ranked::ui::TextMuted, "extrabold", anchor), 5);
            };
            caption(tr("survival-queue-players","PLAYERS"), x0, {0.0f, 0.5f});
            caption(tr("waiting-time", "WAIT"), (x0 + x1) / 2.0f, {0.5f, 0.5f});
            caption(tr("survival-queue-minimum","MINIMUM PLAYERS"), x1, {1.0f, 0.5f});
            m_queuePlayers = makeLabel("", fontScale(56.0f), {x0, y - 24.0f}, corum::ranked::ui::Text, "black", {0.0f, 0.5f}); m_queueContents->addChild(m_queuePlayers, 5);
            auto* maxLabel = makeLabel(fmt::format("/ {}", maximum), fontScale(24.0f), {x0 + 40.0f, y - 28.0f}, corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f});
            maxLabel->setID("survival-queue-maximum"_spr); m_queueContents->addChild(maxLabel, 5);
            m_queueWait = makeLabel("", fontScale(34.0f), {(x0 + x1) / 2.0f, y - 20.0f}, corum::ranked::ui::Text, "black"); m_queueContents->addChild(m_queueWait, 5);
            m_queueMinimum = makeLabel("", fontScale(24.0f), {x1, y - 18.0f}, corum::ranked::ui::Text, "black", {1.0f, 0.5f}); m_queueContents->addChild(m_queueMinimum, 5);
            m_queueHint = makeLabel("", fontScale(13.0f), {x1, y - 31.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f}); m_queueContents->addChild(m_queueHint, 5);
            y -= 44.0f;
            auto* track = CCLayerColor::create({32,35,46,255}, width, px(20.0f)); track->setPosition({x0, y - px(20.0f)}); track->setSkewX(20.0f); m_queueContents->addChild(track, 4);
            m_queueFillBar = CCLayerColor::create({corum::ranked::ui::Accent.r,corum::ranked::ui::Accent.g,corum::ranked::ui::Accent.b,255}, 0.0f, px(20.0f));
            m_queueFillBar->setPosition({x0, y - px(20.0f)}); m_queueFillBar->setSkewX(20.0f); m_queueFillBar->setID("survival-queue-fill-bar"_spr); m_queueContents->addChild(m_queueFillBar, 5);
            m_queueFillWidth = width;
            y -= px(20.0f) + 8.0f;
            // Seats: 8 per row at 50 points for up to 16 players; compact rows
            // of 16 above that (cube only) so 64 fit.
            auto const compact = maximum > 16;
            auto const columns = compact ? 16 : 8;
            auto const gap = compact ? 3.0f : px(10.0f);
            auto const rows = (maximum + columns - 1) / columns;
            auto const gridBottom = bottom + 34.0f;
            auto const tileW = (width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            auto const tileH = compact ? std::min(tileW, (y - gridBottom - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows)) : px(100.0f);
            auto const cubeSize = std::min(px(44.0f), tileH * 0.5f);
            for (int index = 0; index < maximum; ++index) {
                auto const col = index % columns, row = index / columns;
                auto* slot = CCLayerColor::create({23,25,34,255},tileW,tileH);
                slot->ignoreAnchorPointForPosition(false); slot->setAnchorPoint({0.5f,0.5f});
                slot->setPosition({x0 + col * (tileW + gap) + tileW / 2.0f, y - tileH / 2.0f - row * (tileH + gap)});
                if (index < corum::ranked::SurvivalRuntime::get().queue().minimumPlayers) {
                    auto* edge = CCLayerColor::create({kGold.r,kGold.g,kGold.b,150},tileW,1.0f); edge->setPosition({0.0f,tileH - 1.0f}); slot->addChild(edge,1);
                }
                auto* icon = SimplePlayer::create(1);
                icon->setColors({255,255,255},{255,255,255}); icon->setScale(cubeSize / 30.0f);
                icon->setCascadeOpacityEnabled(true); icon->setPosition({tileW / 2.0f, compact ? tileH / 2.0f : tileH / 2.0f + 6.0f}); icon->setVisible(false); icon->setTag(11);
                slot->addChild(icon,3);
                if (!compact) {
                    auto* seatName = makeLabel(fmt::format(fmt::runtime(tr("survival-queue-seat","PLAYER {}")),index + 1),fontScale(13.0f),{tileW / 2.0f,9.0f},corum::ranked::ui::Text,"semibold");
                    corum::ranked::ui::fitMenuLabelWidth(seatName,tileW - 6.0f,fontScale(13.0f),0.09f); seatName->setVisible(false); seatName->setTag(12); slot->addChild(seatName,3);
                    auto* number = makeLabel(std::to_string(index + 1),fontScale(22.0f),{tileW / 2.0f,tileH / 2.0f},ccc3(111,116,134),"black"); number->setTag(13); slot->addChild(number,3);
                }
                m_queueContents->addChild(slot,4); m_queueSlots.push_back(slot);
            }
            // Bottom row: the reveal note and CANCEL MATCHING.
            auto* note = makeLabel(tr("seat-fade-note", "NEW PLAYERS FADE INTO OPEN SEATS · NAMES SHOW WHEN THE MATCH STARTS"), fontScale(13.0f),
                {x0, bottom + 14.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(note, width - 120.0f, fontScale(13.0f), 0.1f); m_queueContents->addChild(note, 5);
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
            m_queueFill = boardButton(tr("survival-fill-bots","FILL WITH BOTS"), {px(170.0f), px(42.0f)}, {x1 - px(85.0f) - px(180.0f), bottom + 14.0f},
                menu_selector(CorumRankedLayer::onSurvivalFillBots), 16.0f, false, "survival-queue-fill-bots"_spr);
#endif
            m_queueLeave = boardButton(tr("cancel-matching", "CANCEL MATCHING"), {px(170.0f), px(42.0f)}, {x1 - px(85.0f), bottom + 14.0f},
                menu_selector(CorumRankedLayer::onSurvivalLeaveQueue), 16.0f, false, "survival-queue-leave"_spr);
        }
        m_root = savedRoot; m_menu = savedMenu;
        setLobbyNodeInput(m_queueMenu,false);
    }


    // Radar contacts: a new one blinks, one that leaves the range or the queue
    // fades out, one that gets matched flashes yellow and vanishes ("taken").
    void updateRadarBlips(float dt, int range, int maximum, float outer) {
        auto const& radar = RankedRuntime::get().view().queueRadar;
        for (auto& [key, blip] : m_radarBlips) if (!blip.matched) blip.leaving = true;
        for (auto const& contact : radar.contacts) {
            if (contact.key.empty()) continue;
            auto found = m_radarBlips.find(contact.key);
            auto const inRange = contact.distance <= range;
            if (found == m_radarBlips.end()) {
                if (!inRange || contact.matched) continue;
                found = m_radarBlips.emplace(contact.key, RadarBlip{}).first;
            }
            auto& blip = found->second;
            blip.radius = outer * static_cast<float>(contact.distance) / static_cast<float>(std::max(1, maximum));
            blip.angle = contact.angle * 0.01745329252f;
            if (contact.matched) { if (!blip.matched) { blip.matched = true; blip.matchedFor = 0; } }
            else if (inRange) { blip.leaving = false; blip.fade = std::max(0.0f, blip.fade - dt / 0.25f); }
        }
        for (auto it = m_radarBlips.begin(); it != m_radarBlips.end();) {
            auto& blip = it->second; blip.age += dt;
            if (blip.matched) blip.matchedFor += dt; else if (blip.leaving) blip.fade += dt / 0.4f;
            if ((blip.matched && blip.matchedFor >= 0.8f) || blip.fade >= 1.0f) it = m_radarBlips.erase(it); else ++it;
        }
    }

    void drawQueueCircle(float radius, ccColor4F color, float width, int segments = 60) {
        for (int i = 0; i < segments; ++i) {
            auto const a = 6.28318530718f * static_cast<float>(i) / segments, b = 6.28318530718f * static_cast<float>(i + 1) / segments;
            m_queueRadar->drawSegment({std::cos(a) * radius, std::sin(a) * radius}, {std::cos(b) * radius, std::sin(b) * radius}, width, color);
        }
    }

    // Every frame, also while the panel fades: CCDrawNode has no opacity, so
    // each colour is scaled by the queue contents' current opacity.
    void drawQueueArena() {
        auto const now = SteadyClock::now();
        auto const dt = m_radarFrameAt == SteadyClock::time_point{} ? 0.0f
            : std::min(0.1f, std::chrono::duration<float>(now - m_radarFrameAt).count());
        m_radarFrameAt = now;
        if (!m_queueRadar || !m_queueContents) return;
        m_queueRadar->clear();
        if (!m_queueContents->isVisible()) return;
        auto const k = static_cast<float>(m_queueContents->getOpacity()) / 255.0f;
        auto const found = m_queueSurvivalArena ? corum::ranked::SurvivalRuntime::get().matchFoundHold() : m_soloFoundHoldUntil.has_value();
        // The centre hub holds the SEARCHING caption and the wait clock; the
        // search ring, sweep and contacts stay in the band outside it, so the
        // growing ring never crosses the text.
        constexpr float outer = 46.0f, inner = 24.8f, hub = 23.0f;
        auto const& radar = RankedRuntime::get().view().queueRadar;
        auto const maximum = std::max(1, radar.maximumRange);
        auto const range = std::clamp(radar.searchRange, 0, maximum);
        auto const search = m_queueSurvivalArena || radar.maximumRange <= 0 ? 44.0f
            : inner + 1.5f + (outer - inner - 1.5f) * static_cast<float>(range) / static_cast<float>(maximum);
        if (found) drawQueueCircle(outer, {1.0f, 0.81f, 0.29f, 0.85f * k}, 0.8f);
        else if (!m_queueSurvivalArena) drawQueueCircle(outer, {0.78f, 1.0f, 0.18f, 0.30f * k}, 0.6f);
        if (!found) {
            drawQueueCircle(inner, {0.78f, 1.0f, 0.18f, 0.12f * k}, 0.4f, 40);
            // 48-degree sweep, one turn per 3.6 s, fading from its leading edge.
            m_queueSweepAngle = std::fmod(m_queueSweepAngle + dt * 1.74532925f, 6.28318530718f);
            constexpr int steps = 12;
            for (int i = 0; i < steps; ++i) {
                auto const a = m_queueSweepAngle - 0.8377580f * static_cast<float>(steps - i) / steps;
                auto const b = m_queueSweepAngle - 0.8377580f * static_cast<float>(steps - i - 1) / steps;
                CCPoint points[] {{std::cos(a) * hub, std::sin(a) * hub}, {std::cos(a) * search, std::sin(a) * search},
                    {std::cos(b) * search, std::sin(b) * search}, {std::cos(b) * hub, std::sin(b) * hub}};
                m_queueRadar->drawPolygon(points, 4, {0.78f, 1.0f, 0.18f, 0.20f * k * static_cast<float>(i + 1) / steps}, 0, {0, 0, 0, 0});
            }
        }
        {
            // Dark hub disc behind the centre text.
            CCPoint disc[40];
            for (int i = 0; i < 40; ++i) { auto const a = 6.28318530718f * static_cast<float>(i) / 40.0f; disc[i] = CCPoint{std::cos(a) * hub, std::sin(a) * hub}; }
            m_queueRadar->drawPolygon(disc, 40, {0.051f, 0.055f, 0.071f, 0.92f * k}, 0, {0, 0, 0, 0});
        }
        if (!m_queueSurvivalArena) {
            if (!found) drawQueueCircle(search, {0.78f, 1.0f, 0.18f, 0.75f * k}, 0.7f);
            updateRadarBlips(dt, range, maximum, outer);
            for (auto const& [key, blip] : m_radarBlips) {
                // Contacts keep their relative distance, mapped into the band.
                auto const r = inner + 1.5f + (outer - inner - 1.5f) * std::clamp(blip.radius / outer, 0.0f, 1.0f);
                CCPoint const at {std::cos(blip.angle) * r, std::sin(blip.angle) * r};
                if (blip.matched) {
                    auto const t = blip.matchedFor / 0.8f;
                    auto const on = static_cast<int>(blip.matchedFor * 12.0f) % 2 == 0;
                    m_queueRadar->drawDot(at, 1.8f, {1.0f, 0.81f, 0.29f, (on ? 1.0f : 0.25f) * (1.0f - t) * k});
                    for (int i = 0; i < 12; ++i) {
                        auto const a = 0.5235988f * i, b = 0.5235988f * (i + 1), r = 2.0f + 8.0f * t;
                        m_queueRadar->drawSegment(at + CCPoint{std::cos(a) * r, std::sin(a) * r}, at + CCPoint{std::cos(b) * r, std::sin(b) * r}, 0.4f, {1.0f, 0.81f, 0.29f, 0.8f * (1.0f - t) * k});
                    }
                    continue;
                }
                auto alpha = 1.0f - std::clamp(blip.fade, 0.0f, 1.0f);
                auto size = 1.6f;
                if (blip.age < 0.6f) { alpha *= 0.35f + 0.65f * std::abs(std::cos(blip.age * 16.0f)); size += 1.4f * (1.0f - blip.age / 0.6f); }
                m_queueRadar->drawDot(at, size, {1.0f, 0.32f, 0.41f, alpha * k});
            }
            if (m_queueRangeLabel) {
                auto const text = !found && range > 0 ? fmt::format(fmt::runtime(tr("score-range-format", "(SCORE RANGE +/-{})")), range) : std::string();
                if (text != m_queueRangeLabel->getString()) m_queueRangeLabel->setString(text.c_str());
            }
        }
    }

    void updateQueuePanel() {
        if (!m_queueContents) return;
        auto const selected = m_modeTransition.selected();
        auto const& survival = corum::ranked::SurvivalRuntime::get();
        if (selected == corum::ranked::RankedMode::Survival) {
            auto const& queue = survival.queue();
            // The seat grid follows the field size the server reports (up to 64);
            // the page may have been built before the first queue snapshot.
            if (!m_queueAnimating && m_queueSeatMaximum != std::clamp(queue.maximumPlayers,2,64)) {
                if (auto* card = selectedLobbyCard()) {
                    auto const visible = m_queueContents->isVisible();
                    buildQueueContents(corum::ranked::RankedMode::Survival, card);
                    m_queueContents->setVisible(visible);
                    setLobbyNodeInput(m_queueMenu, true);
                    syncLobbyInput();
                }
            }
            auto const waiting = survival.queueWaitingSeconds();
            // MATCH FOUND holds here for a second before the roster intro opens.
            auto const found = survival.matchFoundHold();
            if (m_queueHeading) {
                auto const heading = found ? tr("match-found","MATCH FOUND") : tr("matchmaking","MATCHMAKING");
                if (heading != m_queueHeading->getString()) {
                    m_queueHeading->setString(heading.c_str()); m_queueHeading->setColor(corum::ranked::ui::AccentInk);
                    corum::ranked::ui::fitMenuLabelWidth(m_queueHeading,40.0f,fontScale(13.0f),0.1f);
                    if (found) m_queueHeading->runAction(CCSequence::create(CCScaleBy::create(.12f,1.12f),CCScaleBy::create(.18f,1/1.12f),nullptr));
                }
            }
            if (found) {
                for (auto* slot : m_queueSlots) slot->setColor(slot->getTag() == 1 ? ccc3(110,90,30) : ccc3(50,44,24));
                if (m_queueWait) m_queueWait->setString(tr("match-found","MATCH FOUND").c_str());
                if (m_queueLeave) m_queueLeave->setEnabled(false);
                if (m_queueFill) m_queueFill->setEnabled(false);
                return;
            }
            if (m_queuePlayers) {
                m_queuePlayers->setString(fmt::format("{}",queue.players).c_str());
                m_queuePlayers->setColor(queue.players >= queue.minimumPlayers ? corum::ranked::ui::Green : corum::ranked::ui::Text);
                corum::ranked::ui::fitMenuLabelWidth(m_queuePlayers,60.0f,fontScale(56.0f),0.3f);
                if (auto* maxLabel = m_queueContents->getChildByID("survival-queue-maximum"_spr)) maxLabel->setPositionX(m_queuePlayers->getPositionX() + m_queuePlayers->getScaledContentSize().width + 5.0f);
            }
            if (m_queueFillBar) m_queueFillBar->setContentSize({m_queueFillWidth * std::clamp(static_cast<float>(queue.players) / static_cast<float>(std::max(1,queue.maximumPlayers)),0.0f,1.0f),4.0f});
            for (std::size_t index = 0; index < m_queueSlots.size(); ++index) {
                auto* slot = m_queueSlots[index];
                auto const filled = static_cast<int>(index) < queue.players;
                slot->setColor(filled ? ccc3(43,47,60) : ccc3(23,25,34));
                // A joining player's white cube and name fade in (their own
                // opacity, under the panel's); the open seat number hides.
                auto* icon = slot->getChildByTag(11); auto* name = slot->getChildByTag(12); auto* number = slot->getChildByTag(13);
                if (filled && slot->getTag() != 1) {
                    slot->setTag(1);
                    for (auto* part : {icon, name}) if (auto* rgba = dynamic_cast<CCRGBAProtocol*>(part)) {
                        part->setVisible(true); part->stopAllActions(); rgba->setOpacity(0); part->runAction(CCFadeTo::create(0.35f,255));
                    } else if (part) part->setVisible(true);
                    if (number) number->setVisible(false);
                } else if (!filled && slot->getTag() == 1) {
                    slot->setTag(0);
                    if (icon) icon->setVisible(false);
                    if (name) name->setVisible(false);
                    if (number) number->setVisible(true);
                }
            }
            if (m_queueWait) m_queueWait->setString(fmt::format("{:02}:{:02}",waiting / 60,waiting % 60).c_str());
            if (m_queueMinimum) m_queueMinimum->setString(fmt::format(fmt::runtime(tr("players-count-format","{} PLAYERS")),queue.minimumPlayers).c_str());
            if (m_queueHint) {
                m_queueHint->setString((queue.error.empty() ? fmt::format(fmt::runtime(tr("survival-queue-short-format","AFTER {}s, SMALLER FIELDS MAY START")),queue.maxWaitSeconds)
                    : corum::ranked::i18n::text(queue.error,queue.error)).c_str());
                m_queueHint->setColor(queue.error.empty() ? corum::ranked::ui::TextMuted : kRed);
                corum::ranked::ui::fitMenuLabelWidth(m_queueHint,180.0f,fontScale(13.0f),0.1f);
            }
            auto const waitingQueue = !survival.creating() && queue.status == "QUEUED";
            if (m_queueLeave) m_queueLeave->setEnabled(waitingQueue);
            if (m_queueFill) m_queueFill->setEnabled(waitingQueue);
        } else {
            auto const waiting = survival.soloQueueWaitingSeconds();
            auto const found = m_soloFoundHoldUntil.has_value();
            if (m_queueHeading) {
                auto const heading = found ? tr("match-found","MATCH FOUND") : tr("matchmaking","MATCHMAKING");
                if (heading != m_queueHeading->getString()) {
                    m_queueHeading->setString(heading.c_str()); m_queueHeading->setColor(corum::ranked::ui::AccentInk);
                    corum::ranked::ui::fitMenuLabelWidth(m_queueHeading,40.0f,fontScale(13.0f),0.1f);
                    if (found) m_queueHeading->runAction(CCSequence::create(CCScaleBy::create(.12f,1.12f),CCScaleBy::create(.18f,1/1.12f),nullptr));
                }
            }
            if (m_queueCenter) {
                auto const center = found ? tr("match-found","MATCH FOUND") : tr("searching","SEARCHING...");
                if (center != m_queueCenter->getString()) { m_queueCenter->setString(center.c_str()); corum::ranked::ui::fitMenuLabelWidth(m_queueCenter,130.0f,fontScale(14.0f),0.1f); }
            }
            // Still anonymous on MATCH FOUND: only the slot turns gold.
            if (m_queueOpponentMark) m_queueOpponentMark->setColor(found ? kGold : corum::ranked::ui::TextMuted);
            if (m_queueOpponentLabel) m_queueOpponentLabel->setColor(found ? kGold : corum::ranked::ui::TextMuted);
            if (m_queueWait) { m_queueWait->setString(fmt::format("{:02}:{:02}",waiting / 60,waiting % 60).c_str()); corum::ranked::ui::fitMenuLabelWidth(m_queueWait,80.0f,fontScale(34.0f),0.3f); }
            if (m_queueFillBar) {
                auto const& radar = RankedRuntime::get().view().queueRadar;
                auto const share = radar.maximumRange > 0 ? std::clamp(static_cast<float>(radar.searchRange) / static_cast<float>(radar.maximumRange),0.0f,1.0f) : 0.0f;
                m_queueFillBar->setContentSize({m_queueFillWidth * (found ? 1.0f : share),3.0f});
                m_queueFillBar->setColor(found ? kGold : corum::ranked::ui::Accent);
            }
            if (m_queueLeave) m_queueLeave->setEnabled(!found && RankedRuntime::get().view().stage == RuntimeStage::Queued);
        }
    }

    // Open: the action card fades out, then the profile card widens, then the
    // queue fades in. Close runs the same three steps in reverse. Surfaces are
    // CCLayerColor chrome that ignores cascaded opacity, so they fade explicitly.
    void animateQueue(bool open) {
        auto* card = selectedLobbyCard();
        auto const selected = m_modeTransition.selected();
        if (!card || !selected || !m_lobbyPanel) return;
        auto const mode = *selected;
        m_queueAnimating = true; m_queueShown = open;
        syncLobbyInput(); placeLobbyFooter();
        if (m_lobbyHover) m_lobbyHover->setActive(false);
        auto weak = WeakRef(this);
        auto finish = [weak] {
            auto self = weak.lock(); if (!self || !self->getParent()) return;
            self->m_queueAnimating = false;
            if (self->m_lobbyPanel) self->m_lobbyPanel->setVisible(!self->m_queueShown);
            if (!self->m_queueShown && self->m_queueContents) {
                self->m_queueContents->removeFromParentAndCleanup(true); self->m_queueContents = nullptr; self->m_queueMenu = nullptr;
                self->m_queueSlots.clear(); self->m_queuePlayers = self->m_queueWait = self->m_queueMinimum = self->m_queueHint = self->m_queueHeading = nullptr;
                self->m_queueLeave = self->m_queueFill = nullptr;
                // beta.152 crash: the rings died with the contents but their
                // pointers survived into the next open's syncQueueRings().
                self->m_queueRingCyan = self->m_queueRingGold = self->m_queueSweep = self->m_queueOpponentBox = nullptr;
                self->m_queueCenter = self->m_queueOpponentMark = self->m_queueOpponentLabel = nullptr;
                self->m_queueRadar = nullptr; self->m_queueRangeLabel = nullptr; self->m_queueFillBar = nullptr;
            }
            self->syncLobbyInput(); self->placeLobbyFooter(); self->refreshLobbyHover();
        };
        if (open) {
            buildQueueContents(mode,card); updateQueuePanel();
            m_queueContents->setVisible(false);
            auto* contents = m_queueContents;
            auto* panel = m_lobbyPanel;
            panel->stopAllActions();
            corum::ranked::ui::fadeModeLobbySurfaces(panel,0.20f,false);
            panel->runAction(CCSequence::create(corum::ranked::ui::ModeLobbyFade::create(0.20f,0,corum::ranked::ui::ModeLobbyEase::SineIn),
                CallFuncExt::create([panel] { panel->setVisible(false); }),nullptr));
            // The queue is its own page: it fades in over the lobby (no widening).
            card->runAction(CCSequence::create(
                CCDelayTime::create(0.22f),
                CallFuncExt::create([contents, finish] {
                    // The page root sits on the lobby centre line; it rises 6 points into it.
                    auto const restY = contents->getPositionY();
                    corum::ranked::ui::setModeLobbyOpacity(contents,0); contents->setVisible(true); contents->setPositionY(restY - 6.0f);
                    corum::ranked::ui::fadeModeLobbySurfaces(contents,0.28f,true);
                    contents->runAction(CCSequence::create(
                        CCSpawn::create(corum::ranked::ui::ModeLobbyFade::create(0.28f,255,corum::ranked::ui::ModeLobbyEase::SineOut),
                            CCEaseSineOut::create(CCMoveTo::create(0.28f,{0.0f,restY})),nullptr),
                        CallFuncExt::create(finish),nullptr));
                }),nullptr));
        } else {
            auto* contents = m_queueContents;
            auto collapse = CCSequence::create(
                CCDelayTime::create(0.02f),
                CallFuncExt::create([weak, mode, finish] {
                    auto self = weak.lock(); if (!self || !self->m_lobbyPanel) return;
                    // Rebuild the action card so its chrome fades in from its own opacities.
                    self->renderModePanel(RankedRuntime::get().view(), mode);
                    auto* panel = self->m_lobbyPanel;
                    panel->stopAllActions(); panel->setVisible(true);
                    corum::ranked::ui::setModeLobbyOpacity(panel,0);
                    corum::ranked::ui::fadeModeLobbySurfaces(panel,0.24f,true);
                    panel->runAction(CCSequence::create(corum::ranked::ui::ModeLobbyFade::create(0.24f,255,corum::ranked::ui::ModeLobbyEase::SineOut),
                        CallFuncExt::create(finish),nullptr));
                }),nullptr);
            if (contents) {
                auto keep = Ref<CCAction>(collapse);
                corum::ranked::ui::fadeModeLobbySurfaces(contents,0.18f,false);
                contents->runAction(CCSequence::create(corum::ranked::ui::ModeLobbyFade::create(0.18f,0,corum::ranked::ui::ModeLobbyEase::SineIn),
                    CallFuncExt::create([card, contents, keep] { contents->setVisible(false); card->runAction(keep.data()); }),nullptr));
            } else card->runAction(collapse);
        }
    }

    void syncQueuePresentation() {
        if (m_page != Page::Live || m_uiFadingOut || m_modeTransition.moving() || m_queueAnimating ||
            m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending || !m_lobbyPanel) return;
        auto& survival = corum::ranked::SurvivalRuntime::get();
        if (!m_modeTransition.selected()) {
            // A recovered/duplicated Survival queue opens its mode first.
            if (survival.queueing() && RankedRuntime::get().view().stage == RuntimeStage::Ready && !m_modeNoticeOpen)
                enterMode(corum::ranked::RankedMode::Survival);
            return;
        }
        if (!selectedLobbyCard()) return;
        auto const want = lobbyQueueActive();
        if (want != m_queueShown) { animateQueue(want); return; }
        updateQueuePanel();
        placeLobbyFooter();
    }

    void onSurvivalLeaveQueue(CCObject*) {
        if (m_queueAnimating) return;
        corum::ranked::SurvivalRuntime::get().leaveQueue();
    }

    void onSurvivalFillBots(CCObject*) {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        auto& runtime = corum::ranked::SurvivalRuntime::get();
        if (!m_queueAnimating && !runtime.creating() && runtime.queue().status == "QUEUED") corum::ranked::showSurvivalFillBotsPopup();
#endif
    }

    void onQueueFreePlay(CCObject*) {
        if (m_queueAnimating || m_uiFadingOut) return;
        auto& survival = corum::ranked::SurvivalRuntime::get();
        if (m_modeTransition.selected() == corum::ranked::RankedMode::SoloRanked) survival.beginSoloQueueFreePlay();
        else survival.beginQueueFreePlay();
    }
    void onSurvivalQueue(CCObject*) { corum::ranked::SurvivalRuntime::get().joinQueue(); }

    void syncLobbyPopulation(){
        auto& runtime=RankedRuntime::get();auto* director=CCDirector::sharedDirector();
        auto const stage=runtime.view().stage;
        bool const visible=isRunning()&&m_page==Page::Live&&!director->getNextScene()&&getParent()==director->getRunningScene()&&
            !m_rankTransitionLayer&&!m_resultScoreLayer&&!m_resultScorePending&&
            (stage==RuntimeStage::Ready||stage==RuntimeStage::Queued||stage==RuntimeStage::JoiningQueue);
        runtime.setLobbyPopulationVisible(visible);
        if(visible&&m_root)if(auto* population=typeinfo_cast<corum::ranked::ui::LobbyPopulationView*>(m_root->getChildByID("ranked-lobby-population"_spr)))population->updatePopulation({runtime.view().lobbyOffline,runtime.view().lobbyOnline,runtime.view().lobbyPopulationKnown});
    }

    void renderMain(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const selected = m_modeTransition.selected();
        addTopBack(true);
        addLobbyHeader(view, selected);
        if (view.stage != RuntimeStage::Ready && view.stage != RuntimeStage::Queued && view.stage != RuntimeStage::JoiningQueue) {
            auto* message = makeLabel(lobbyMessage(view),0.30f,{size.width/2,size.height/2},kGold,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(message,size.width-60,0.30f,0.17f); m_root->addChild(message,5);
            if (view.stage == RuntimeStage::Loading) { auto* spinner = LoadingSpinner::create(42); spinner->setPosition({size.width/2,size.height/2-45}); m_root->addChild(spinner,5); }
            else addButton(tr("retry","RETRY"),{size.width/2,size.height/2-45},menu_selector(CorumRankedLayer::onRetry),false,0.65f);
            return;
        }
        // L1 halves / L2-L3 page: lobby groups sit at the origin at scale 1.
        m_lobbySurvival = makeLobbyGroup({0.0f,0.0f},1.0f,"survival-profile-half"_spr);
        m_lobbySolo = makeLobbyGroup({0.0f,0.0f},1.0f,"solo-ranked-profile-half"_spr);
        m_lobbyPanel = makeLobbyGroup({0.0f,0.0f},1.0f,"ranked-mode-menu-panel"_spr);
        if (!selected || *selected == corum::ranked::RankedMode::Survival) renderModeCard(view,corum::ranked::RankedMode::Survival,m_lobbySurvival);
        if (!selected || *selected == corum::ranked::RankedMode::SoloRanked) renderModeCard(view,corum::ranked::RankedMode::SoloRanked,m_lobbySolo);
        renderLobbyTools(view);
        updateSeasonTimers();
        if (selected) {
            renderModePanel(view,*selected);
            auto* hidden = *selected == corum::ranked::RankedMode::Survival ? m_lobbySolo : m_lobbySurvival;
            hidden->setVisible(false);
            auto* card = *selected == corum::ranked::RankedMode::Survival ? m_lobbySurvival : m_lobbySolo;
            card->setZOrder(5); m_lobbyPanel->setZOrder(4);
            m_queueShown = lobbyQueueActive();
            if (m_queueShown) {
                buildQueueContents(*selected,card);
                corum::ranked::ui::setModeLobbyOpacity(m_lobbyPanel,0); m_lobbyPanel->setVisible(false);
                updateQueuePanel();
            }
        } else { m_lobbyPanel->setVisible(false); }
        placeLobbyFooter();
        syncLobbyInput();refreshLobbyHover();
    }

    // P3 (placement map), from the board: STAGE n with MAP / POOL, the stage
    // strip and the time panel; the map panel (thumbnail, title, difficulty,
    // PLAY AGAIN); the current grade and score; and the attempts table.
    void renderPlacementMap(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& placement = view.placement;
        auto const& match = view.match;
        auto const headerY = size.height - kLobbyHeader / 2.0f;
        auto const pool = !placement.stagePools.empty() ? placement.stagePools.back() : placement.startingPool;
        auto* brand = makeLabel(fmt::format("STAGE {}", placement.currentStageIndex), fontScale(30.0f), {px(28.0f), headerY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
        auto const chipText = fmt::format("MAP {} / {} · POOL {}", placement.currentMapIndex, placement.mapsPerStage, pool);
        auto const chipX = px(28.0f) + brand->getScaledContentSize().width + 12.0f;
        addChip(m_root, chipText, {chipX, headerY}, ccc3(32, 35, 46), corum::ranked::ui::TextMuted, 15.0f);
        std::string stateText = preparationStageText();
        if (match.state == "MAP_PLAYING") stateText = tr("placement-map-live", "MAP LIVE · SCORE + GRADE UPDATE IN REAL TIME");
        if (match.state == "MAP_SETTLING") stateText = tr("final-attempt-progress", "FINAL ATTEMPT IN PROGRESS");
        auto const stateX = chipX + makeLabel(chipText, fontScale(15.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + px(24.0f) + 10.0f;
        // Right: S1..Sn strip and the time panel.
        constexpr float panelW = 74.0f, panelH = 30.0f;
        auto const panelX = size.width - px(28.0f) - panelW / 2.0f;
        auto* timePanel = CCLayerColor::create({23, 25, 34, 255}, panelW, panelH); timePanel->setPosition({panelX - panelW / 2.0f, headerY - panelH / 2.0f}); m_root->addChild(timePanel, 3);
        auto* rule = CCLayerColor::create({corum::ranked::ui::Accent.r, corum::ranked::ui::Accent.g, corum::ranked::ui::Accent.b, 255}, panelW, 1.0f);
        rule->setPosition({panelX - panelW / 2.0f, headerY + panelH / 2.0f - 1.0f}); m_root->addChild(rule, 4);
        m_root->addChild(makeLabel(tr("time-left", "TIME LEFT"), fontScale(13.0f), {panelX, headerY + 8.0f}, corum::ranked::ui::TextMuted, "semibold"), 5);
        if (match.state == "MAP_PREPARE") {
            m_placementPrepareTimerLabel = makeLabel("", fontScale(15.0f), {panelX, headerY - 5.0f}, kGold, "extrabold");
            m_placementPrepareTimerLabel->setID("ranked-placement-prepare-timer"_spr);
            m_root->addChild(m_placementPrepareTimerLabel, 20);
        } else {
            m_root->addChild(makeLabel(match.state == "MAP_SETTLING" ? std::string("LAST") : std::string("LIVE"), fontScale(24.0f), {panelX, headerY - 5.0f},
                match.state == "MAP_SETTLING" ? kGold : corum::ranked::ui::Accent, "black"), 5);
        }
        float stripX = panelX - panelW / 2.0f - 10.0f;
        for (int stage = placement.totalStages; stage >= 1; --stage) {
            auto const done = stage < placement.currentStageIndex || static_cast<int>(placement.stageResults.size()) >= stage;
            auto const current = stage == placement.currentStageIndex;
            auto const text = fmt::format("S{}", stage);
            auto const w = makeLabel(text, fontScale(13.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + 10.0f;
            stripX -= w;
            addChip(m_root, text, {stripX, headerY}, done || current ? corum::ranked::ui::AccentFill : ccc3(32, 35, 46),
                done || current ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
            stripX -= 3.0f;
        }
        auto* state = makeLabel(stateText, fontScale(13.0f), {stateX, headerY}, match.state == "MAP_SETTLING" ? kGold : corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(state, std::max(40.0f, stripX - stateX - 8.0f), fontScale(13.0f), 0.09f); m_root->addChild(state, 5);

        // Left: the map panel (380 px).
        auto const top = size.height - kLobbyHeader - px(4.0f), bottom = px(22.0f);
        auto const mapW = px(380.0f), x0 = px(28.0f);
        auto* mapPanel = CCLayerColor::create({23, 25, 34, 255}, mapW, top - bottom); mapPanel->setPosition({x0, bottom}); m_root->addChild(mapPanel, 1);
        auto const thumbH = px(180.0f);
        auto* thumbBack = CCLayerColor::create({32, 35, 46, 255}, mapW, thumbH); thumbBack->setPosition({x0, top - thumbH}); m_root->addChild(thumbBack, 2);
        addSoloThumbnail(m_root, {x0 + mapW / 2.0f, top - thumbH / 2.0f}, {mapW, thumbH}, match);
        if (match.currentMap) {
            auto const& map = *match.currentMap;
            auto* title = makeLabel(map.title, fontScale(34.0f), {x0 + px(18.0f), top - thumbH - px(16.0f) - 8.0f}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(title, mapW - px(36.0f), fontScale(34.0f), 0.2f); m_root->addChild(title, 5);
            addMapDifficulty(map, {x0 + px(18.0f) + 40.0f, top - thumbH - px(16.0f) - 28.0f}, 80.0f, 16.0f);
            if (!map.creator.empty()) {
                auto* by = makeLabel("by " + map.creator, fontScale(13.0f), {x0 + px(18.0f) + 86.0f, top - thumbH - px(16.0f) - 28.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(by, mapW - px(36.0f) - 86.0f, fontScale(13.0f), 0.09f); m_root->addChild(by, 5);
            }
        }
        if (match.state != "MAP_PREPARE")
            boardButton(tr("play-again", "PLAY AGAIN"), {mapW - px(36.0f), px(42.0f)}, {x0 + mapW / 2.0f, bottom + px(16.0f) + px(21.0f)},
                menu_selector(CorumRankedLayer::onSoloPlayAgain), 16.0f, true, "ranked-placement-play-again"_spr);

        // Right: grade and score panel, then the attempts table (or, while
        // preparing, the download rows and options).
        auto const rx = x0 + mapW + px(20.0f), rw = size.width - px(28.0f) - rx;
        auto const gradeH = 62.0f;
        auto* gradePanel = CCLayerColor::create({23, 25, 34, 255}, rw, gradeH); gradePanel->setPosition({rx, top - gradeH}); m_root->addChild(gradePanel, 1);
        if (auto* grade = corum::ranked::ui::PlacementGradeBadge::create(44.0f)) {
            grade->setGrade(placement.currentMapGrade, false);
            grade->setPosition({rx + px(22.0f) + 22.0f, top - gradeH / 2.0f}); m_root->addChild(grade, 5);
        }
        auto const gx = rx + px(22.0f) + 44.0f + px(24.0f);
        m_root->addChild(makeLabel(tr("current-map-grade", "CURRENT MAP GRADE"), fontScale(14.0f), {gx, top - 14.0f}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 5);
        m_root->addChild(makeLabel(placement.currentMapGrade, fontScale(56.0f), {gx, top - 33.0f}, corum::ranked::ui::Text, "black", {0.0f, 0.5f}), 5);
        m_root->addChild(makeLabel(tr("grade-updates-after-attempt", "UPDATES AFTER EACH ATTEMPT"), fontScale(13.0f), {gx, top - 51.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
        auto const sx = rx + rw - px(22.0f);
        double best = 0.0;
        for (auto const& attempt : placement.currentMapTopAttempts) best = std::max(best, attempt.progressPercent);
        m_root->addChild(makeLabel(tr("score", "SCORE"), fontScale(14.0f), {sx, top - 14.0f}, corum::ranked::ui::TextMuted, "extrabold", {1.0f, 0.5f}), 5);
        m_root->addChild(makeLabel(fmt::format("{:.1f}", placement.provisionalMapScore), fontScale(56.0f), {sx, top - 33.0f}, corum::ranked::ui::Accent, "black", {1.0f, 0.5f}), 5);
        m_root->addChild(makeLabel(fmt::format(fmt::runtime(tr("best-progress-format", "BEST {}%")), static_cast<int>(std::round(best))), fontScale(13.0f), {sx, top - 51.0f},
            corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f}), 5);
        auto const tableTop = top - gradeH - px(14.0f);
        if (match.state == "MAP_PREPARE") {
            addPreparationStatusRows({rx + rw * 0.25f, tableTop - 20.0f}, {rx + rw * 0.75f, tableTop - 20.0f}, rw * 0.42f);
            m_prepareLoadPercentLabel = makeLabel("0%", fontScale(15.0f), {rx + rw / 2.0f, tableTop - 44.0f}, corum::ranked::ui::Accent, "black");
            m_root->addChild(m_prepareLoadPercentLabel, 5);
            addRoundOptions(bottom + 14.0f, rx + rw / 2.0f - 6.0f);
        } else {
            auto const colAttempt = rx + 8.0f, colBar = rx + px(70.0f) + 8.0f, colPct = rx + rw - px(90.0f) - 8.0f, colScore = rx + rw - 8.0f;
            auto const head = [&](std::string const& text, float x, CCPoint anchor) {
                m_root->addChild(makeLabel(text, fontScale(13.0f), {x, tableTop - 6.0f}, corum::ranked::ui::TextMuted, "extrabold", anchor), 5);
            };
            head(tr("attempt", "ATTEMPT"), colAttempt, {0.0f, 0.5f}); head(tr("progress", "PROGRESS"), colBar, {0.0f, 0.5f});
            head("%", colPct, {1.0f, 0.5f}); head(tr("score", "SCORE"), colScore, {1.0f, 0.5f});
            float rowY = tableTop - 6.0f - px(52.0f) / 2.0f - 6.0f;
            std::size_t bestIndex = 0;
            for (std::size_t i = 0; i < placement.currentMapTopAttempts.size(); ++i)
                if (placement.currentMapTopAttempts[i].awardedScore > placement.currentMapTopAttempts[bestIndex].awardedScore) bestIndex = i;
            for (std::size_t i = 0; i < placement.currentMapTopAttempts.size() && rowY > bottom + 10.0f; ++i) {
                auto const& attempt = placement.currentMapTopAttempts[i];
                auto const mine = i == bestIndex;
                auto* row = CCLayerColor::create(mine ? ccc4(32, 38, 42, 255) : ccc4(23, 25, 34, 255), rw, px(52.0f)); row->setPosition({rx, rowY - px(26.0f)}); m_root->addChild(row, 1);
                if (mine) { auto* edge = CCLayerColor::create({corum::ranked::ui::Accent.r, corum::ranked::ui::Accent.g, corum::ranked::ui::Accent.b, 255}, 2.0f, px(52.0f)); edge->setPosition({rx, rowY - px(26.0f)}); m_root->addChild(edge, 2); }
                m_root->addChild(makeLabel(fmt::format("#{}", i + 1), fontScale(15.0f), {colAttempt + 4.0f, rowY}, mine ? corum::ranked::ui::Accent : corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f}), 5);
                addSkewBar(m_root, {colBar, rowY}, colPct - px(90.0f) - colBar, px(6.0f), static_cast<float>(attempt.progressPercent / 100.0),
                    mine ? corum::ranked::ui::Accent : tierColor("SILVER"));
                m_root->addChild(makeLabel(fmt::format("{}%", static_cast<int>(std::round(attempt.progressPercent))), fontScale(15.0f), {colPct, rowY}, corum::ranked::ui::Text, mine ? "black" : "semibold", {1.0f, 0.5f}), 5);
                m_root->addChild(makeLabel(fmt::format("{:.1f}", attempt.awardedScore), fontScale(15.0f), {colScore, rowY}, mine ? corum::ranked::ui::Accent : corum::ranked::ui::TextMuted, "black", {1.0f, 0.5f}), 5);
                rowY -= px(52.0f) + px(6.0f);
            }
            addClearChecks(placement.currentMapClears, {colScore - 4.0f, bottom + 8.0f}, true);
        }
    }

    void renderPlacementMapResult(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& placement = view.placement;
        auto const map1Known = placement.currentMapIndex >= 1;
        auto const map2Known = placement.currentMapIndex >= 2;
        auto const map1Score = placement.currentStageMapScores.size() > 0
            ? placement.currentStageMapScores[0]
            : (placement.currentMapIndex == 1 ? placement.currentMapScore : 0.0);
        auto const map2Score = placement.currentStageMapScores.size() > 1
            ? placement.currentStageMapScores[1]
            : (placement.currentMapIndex == 2 ? placement.currentMapScore : 0.0);
        auto const map1Grade = placement.currentStageMapGrades.size() > 0
            ? placement.currentStageMapGrades[0]
            : (placement.currentMapIndex == 1 ? placement.currentMapGrade : "?");
        auto const map2Grade = map2Known
            ? (placement.currentStageMapGrades.size() > 1 ? placement.currentStageMapGrades[1] : placement.currentMapGrade)
            : "?";
        auto const pool = view.match.currentMap ? view.match.currentMap->pool : 0;
        addScreenHeader(
            fmt::format("MAP {} {}", placement.currentMapIndex, tr("complete", "COMPLETE")),
            pool > 0
                ? fmt::format("STAGE {} · POOL {}", placement.currentStageIndex, pool)
                : fmt::format("STAGE {}", placement.currentStageIndex),
            kGold,
            kAccent
        );

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, std::string const& scoreText, float x, bool final) {
            auto* group = CCNode::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kSteel, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kAccent, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(title, 76.0f, 0.20f, 0.14f);
            group->addChild(title, 3);
            if (grade == "?") {
                group->addChild(makeLabel("?", 0.68f, {44.0f, 53.0f}, corum::ranked::ui::TextMuted, "black"), 4);
            } else {
                auto* badge = corum::ranked::ui::PlacementGradeBadge::create(40.0f);
                if (badge) {
                    badge->setGrade(grade, false);
                    badge->setPosition({44.0f, 54.0f});
                    group->addChild(badge, 4);
                }
            }
            auto* score = makeLabel(scoreText, 0.18f, {44.0f, 16.0f}, corum::ranked::ui::TextMuted, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(score, 76.0f, 0.18f, 0.13f);
            group->addChild(score, 4);
            m_root->addChild(group, 5);
        };

        auto const center = size.width / 2.0f;
        makeGradeCard("MAP 1", map1Grade, map1Known ? fmt::format("{} / 500", formatPlacementScore(map1Score)) : "--", center - 112.0f, false);
        makeGradeCard("MAP 2", map2Grade, map2Known ? fmt::format("{} / 500", formatPlacementScore(map2Score)) : "--", center, false);
        makeGradeCard(tr("stage-grade", "STAGE GRADE"), "?", "--", center + 112.0f, true);
        m_root->addChild(makeLabel("+", 0.48f, {center - 56.0f, size.height / 2.0f + 4.0f}, kGold, "black"), 6);
        m_root->addChild(makeLabel(">", 0.46f, {center + 56.0f, size.height / 2.0f + 4.0f}, kAccent, "black"), 6);

        addButton(
            placement.currentMapIndex == 1 ? tr("next-map", "NEXT MAP") : tr("show-stage-result", "SHOW STAGE RESULT"),
            {center, 27.0f},
            menu_selector(CorumRankedLayer::onContinuePlacement),
            false,
            0.60f
        );
    }

    void renderPlacementStageResult(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        if (view.placement.stageResults.empty()) return;
        auto const result = view.placement.stageResults.back();
        auto const map1Grade = result.mapGrades.size() > 0 ? result.mapGrades[0] : "D";
        auto const map2Grade = result.mapGrades.size() > 1 ? result.mapGrades[1] : "D";
        auto const map1Score = result.mapScores.size() > 0 ? result.mapScores[0] : 0.0;
        auto const map2Score = result.mapScores.size() > 1 ? result.mapScores[1] : 0.0;
        auto const audioKey = fmt::format("{}:{}", view.placement.runId, result.stageIndex);
        // P4 header: STAGE n RESULT with its line, and the S1..Sn strip (grade
        // of every finished stage).
        {
            auto const headerY = size.height - kLobbyHeader / 2.0f;
            auto* brand = makeLabel(fmt::format("STAGE {} {}", result.stageIndex, tr("result", "RESULT")), fontScale(30.0f), {px(28.0f), headerY},
                corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
            auto* line = makeLabel(tr("stage-complete-line", "STAGE COMPLETE. CHECK THE FINAL GRADE."), fontScale(13.0f),
                {px(28.0f) + brand->getScaledContentSize().width + 12.0f, headerY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(line, size.width * 0.38f, fontScale(13.0f), 0.09f); m_root->addChild(line, 6);
            float stripX = size.width - px(28.0f);
            for (int stage = view.placement.totalStages; stage >= 1; --stage) {
                std::string text = fmt::format("S{}", stage);
                bool done = false;
                for (auto const& stageResult : view.placement.stageResults)
                    if (stageResult.stageIndex == stage) { done = true; if (stage != result.stageIndex) text += " · " + stageResult.stageGrade; }
                auto const w = makeLabel(text, fontScale(13.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + 10.0f;
                stripX -= w;
                addChip(m_root, text, {stripX, headerY}, done ? corum::ranked::ui::AccentFill : ccc3(32, 35, 46), done ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
                stripX -= 3.0f;
            }
        }

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, double scoreValue, float x, bool final) {
            auto* group = CCNodeRGBA::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            group->setCascadeColorEnabled(true);
            group->setCascadeOpacityEnabled(true);
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kSteel, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kAccent, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(title, 76.0f, 0.20f, 0.14f);
            group->addChild(title, 3);
            auto* badge = corum::ranked::ui::PlacementGradeBadge::create(40.0f);
            if (badge) {
                badge->setGrade(grade, false);
                badge->setPosition({44.0f, 54.0f});
                group->addChild(badge, 4);
            }
            auto* score = makeLabel(
                fmt::format("{} / 500", formatPlacementScore(scoreValue)),
                0.18f,
                {44.0f, 16.0f},
                final ? kGold : corum::ranked::ui::TextMuted,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(score, 76.0f, 0.18f, 0.13f);
            group->addChild(score, 4);
            m_root->addChild(group, 5);
            return group;
        };

        auto const center = size.width / 2.0f;
        auto const timeline = corum::ranked::placementStageMergeTimeline();
        corum::ranked::TransitionTrace::get().mark("stage-merge-node-build");
        auto* left = makeGradeCard("MAP 1", map1Grade, map1Score, center - 78.0f, false);
        auto* right = makeGradeCard("MAP 2", map2Grade, map2Score, center + 78.0f, false);
        auto* finalGroup = makeGradeCard(
            tr("stage-grade", "STAGE GRADE"),
            result.stageGrade,
            result.stageScore,
            center,
            true
        );
        auto* mergeSymbol = makeLabel(
            "+",
            0.48f,
            {center, size.height / 2.0f + 4.0f},
            kGold,
            "black"
        );
        m_root->addChild(mergeSymbol, 6);

        left->runAction(CCSequence::create(
            CCDelayTime::create(timeline.sourceDelay),
            CCEaseSineInOut::create(CCMoveTo::create(
                timeline.sourceMove,
                {center, size.height / 2.0f + 4.0f}
            )),
            CCFadeOut::create(timeline.sourceFade),
            nullptr
        ));
        right->runAction(CCSequence::create(
            CCDelayTime::create(timeline.sourceDelay),
            CCEaseSineInOut::create(CCMoveTo::create(
                timeline.sourceMove,
                {center, size.height / 2.0f + 4.0f}
            )),
            CCFadeOut::create(timeline.sourceFade),
            nullptr
        ));
        mergeSymbol->runAction(CCSequence::create(
            CCDelayTime::create(timeline.sourceDelay + timeline.sourceMove),
            CCFadeOut::create(timeline.sourceFade),
            nullptr
        ));

        auto* mergeFlash = corum::ranked::ui::rankResourceSprite(
            "fx-rank-shockwave-circle-white.png"
        );
        if (mergeFlash) {
            corum::ranked::ui::fitRankSprite(mergeFlash, {104.0f, 104.0f});
            mergeFlash->setPosition({center, size.height / 2.0f + 4.0f});
            mergeFlash->setColor(kGold);
            mergeFlash->setOpacity(0);
            mergeFlash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            m_root->addChild(mergeFlash, 8);
            mergeFlash->runAction(CCSequence::create(
                CCDelayTime::create(timeline.sourceDelay + timeline.sourceMove),
                CCSpawn::create(
                    CCFadeTo::create(timeline.sourceFade * 0.55f, 205),
                    CCEaseSineOut::create(CCScaleTo::create(timeline.sourceFade, 1.18f)),
                    nullptr
                ),
                CCFadeOut::create(timeline.sourceFade),
                nullptr
            ));
        }

        finalGroup->setVisible(false);
        finalGroup->setOpacity(0);
        finalGroup->setScale(0.84f);
        finalGroup->runAction(CCSequence::create(
            CCDelayTime::create(timeline.finalReveal),
            CallFuncExt::create([this, finalGroup, audioKey, grade = result.stageGrade] {
                finalGroup->setVisible(true);
                finalGroup->setOpacity(255);
                if (m_stageResultAudioPlayedKey != audioKey) {
                    m_stageResultAudioPlayedKey = audioKey;
                    RankedAudioManager::get().setMode(
                        grade == "S" || grade == "A" || grade == "B"
                            ? RankedAudioMode::ResultWin
                            : RankedAudioMode::ResultLose
                    );
                }
            }),
            CCEaseSineOut::create(CCScaleTo::create(timeline.popUp, 1.08f)),
            CCEaseSineInOut::create(CCScaleTo::create(timeline.popSettle, 1.0f)),
            nullptr
        ));

        auto const finalStage = result.stageIndex >= view.placement.totalStages;
        // Footer: the pool rule on the left, NEXT STAGE (and PLAY LATER) on the right.
        {
            auto* rule = makeLabel(tr("stage-pool-rule", "A HIGHER STAGE GRADE STARTS THE NEXT STAGE IN A HARDER POOL"), fontScale(13.0f), {px(28.0f), 25.0f},
                corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(rule, size.width - px(56.0f) - 240.0f, fontScale(13.0f), 0.09f); m_root->addChild(rule, 5);
        }
        auto* movement = makeLabel(
            finalStage
                ? (fmt::format(fmt::runtime(tr("placement-all-complete","ALL {} MAPS COMPLETE")),view.placement.totalStages * view.placement.mapsPerStage))
                : fmt::format("{}  POOL {}  ({:+d})", tr("next", "NEXT"), result.nextPool, result.poolDelta),
            0.30f,
            {center, 49.0f},
            result.poolDelta > 0 ? kGreen : result.poolDelta < 0 ? kRed : corum::ranked::ui::TextMuted,
            "black"
        );
        m_root->addChild(movement, 5);
        auto* button = addButton(
            finalStage ? tr("final-placement", "FINAL PLACEMENT") : tr("next-stage", "NEXT STAGE"),
            {size.width - px(28.0f) - 62.0f, 25.0f},
            menu_selector(CorumRankedLayer::onContinuePlacement),
            false,
            0.54f
        );
        button->setEnabled(false);
        button->runAction(CCSequence::create(
            CCDelayTime::create(timeline.controlsUnlock),
            CallFuncExt::create([button] { button->setEnabled(true); }),
            nullptr
        ));
        if (!finalStage) {
            auto* later = addButton(
                tr("play-later", "PLAY LATER"),
                {size.width - px(28.0f) - 186.0f, 25.0f},
                menu_selector(CorumRankedLayer::onPausePlacement),
                false,
                0.54f
            );
            later->setEnabled(false);
            later->runAction(CCSequence::create(
                CCDelayTime::create(timeline.controlsUnlock),
                CallFuncExt::create([later] { later->setEnabled(true); }),
                nullptr
            ));
        }
    }

    void renderPlacementFinal(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& finalResult = view.placement.finalResult;
        auto const finalTier = upper(finalResult.displayedTier);
        auto const tone = tierColor(finalTier);
        // P5 board, left: the slanted section with the tier band, the 300 px
        // trophy, the PLACED chip, the 72 px tier name and the LP row.
        constexpr float leftW = 260.0f;
        addSlantedFill(m_root, 0.0f, leftW, 0.0f, size.height, px(64.0f), 0.0f, corum::ranked::ui::Surface);
        auto* band = CCLayerColor::create({tone.r, tone.g, tone.b, 51}, px(230.0f), size.height);
        band->setPosition({px(150.0f), 0.0f}); band->setSkewX(8.0f); band->setID("ranked-placement-final-band"_spr); m_root->addChild(band, 2);
        auto const trophy = px(300.0f);
        if (auto* badge = corum::ranked::ui::createAnimatedRankBadge(finalResult.displayedTier, {trophy * 0.82f, trophy * 0.82f},
                corum::ranked::ui::RankBadgeDisplay::Hero)) {
            badge->setPosition({px(80.0f) + trophy / 2.0f, size.height - px(30.0f) - trophy / 2.0f});
            badge->setID("ranked-placement-final-trophy"_spr); m_root->addChild(badge, 4);
        }
        auto const textX = px(32.0f);
        auto* lp = makeLabel(fmt::format("{}", finalResult.visibleRankedScore), fontScale(34.0f), {textX, px(28.0f) + 9.0f},
            corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        m_root->addChild(lp, 6);
        m_root->addChild(makeLabel(tr("stat-lp", "LP"), fontScale(14.0f), {textX + lp->getScaledContentSize().width + 4.0f, px(28.0f) + 6.0f},
            corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
        auto* tier = makeLabel(corum::ranked::rankTierDisplay(finalTier), fontScale(72.0f), {textX, px(28.0f) + 18.0f + 3.0f + 18.0f},
            tone, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(tier); corum::ranked::ui::fitMenuLabelWidth(tier, leftW - 44.0f, fontScale(72.0f), 0.4f);
        tier->setID("ranked-placement-final-tier"_spr); m_root->addChild(tier, 6);
        addChip(m_root, finalResult.skipped ? tr("placement-skipped", "PLACEMENT SKIPPED") : tr("placed", "PLACED"),
            {textX, px(28.0f) + 18.0f + 3.0f + 36.0f + 3.0f + 7.0f}, corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, 15.0f);

        // Right: the map count line, the STAGE / MAPS / POOL / GRADE table and
        // the footer (SHOW STAGE RESULT, CONTINUE).
        auto const& stages = view.placement.stageResults;
        auto const rx0 = leftW + px(16.0f), rx1 = size.width - px(28.0f), top = size.height - px(32.0f);
        auto const totalMaps = view.placement.totalStages * view.placement.mapsPerStage;
        auto* count = makeLabel(finalResult.skipped ? tr("placement-skipped", "PLACEMENT SKIPPED")
            : fmt::format(fmt::runtime(tr("all-maps-complete", "ALL {} MAPS COMPLETE")), totalMaps), fontScale(14.0f), {rx0, top - 5.0f},
            corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(count, rx1 - rx0, fontScale(14.0f), 0.1f); m_root->addChild(count, 6);
        if (!finalResult.skipped && !stages.empty()) {
            auto const colStage = rx0 + px(12.0f), colMaps = rx0 + px(12.0f) + px(80.0f), colGrade = rx1 - px(12.0f);
            auto const colPool = colGrade - px(80.0f) - px(70.0f);
            auto const headY = top - 22.0f;
            auto const head = [&](std::string const& text, float x, CCPoint anchor) {
                m_root->addChild(makeLabel(text, fontScale(13.0f), {x, headY}, corum::ranked::ui::TextMuted, "semibold", anchor), 6);
            };
            head("STAGE", colStage, {0.0f, 0.5f}); head(tr("map", "MAP"), colMaps, {0.0f, 0.5f});
            head("POOL", colPool, {0.0f, 0.5f}); head(tr("stage-grade-short", "GRADE"), colGrade, {1.0f, 0.5f});
            auto* rule = CCLayerColor::create({58, 63, 78, 255}, rx1 - rx0, 1.0f);
            rule->setPosition({rx0, headY - 8.0f}); m_root->addChild(rule, 5);
            auto const rowH = px(48.0f);
            for (std::size_t index = 0; index < stages.size(); ++index) {
                auto const& stage = stages[index];
                auto const y = headY - 12.0f - rowH / 2.0f - static_cast<float>(index) * (rowH + 2.0f);
                auto* stripe = CCLayerColor::create({corum::ranked::ui::SurfaceRaised.r, corum::ranked::ui::SurfaceRaised.g, corum::ranked::ui::SurfaceRaised.b, 255},
                    rx1 - rx0, rowH);
                stripe->setPosition({rx0, y - rowH / 2.0f}); m_root->addChild(stripe, 4);
                m_root->addChild(makeLabel(std::to_string(stage.stageIndex), fontScale(15.0f), {colStage, y}, corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f}), 6);
                std::string maps;
                for (std::size_t map = 0; map < stage.mapGrades.size(); ++map) {
                    if (!maps.empty()) maps += "  ·  ";
                    maps += fmt::format("{} {}", stage.mapGrades[map], map < stage.mapScores.size() ? formatPlacementScore(stage.mapScores[map]) : std::string("-"));
                }
                auto* mapsLabel = makeLabel(maps.empty() ? std::string("-") : maps, fontScale(15.0f), {colMaps, y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(mapsLabel, colPool - colMaps - 8.0f, fontScale(15.0f), 0.1f); m_root->addChild(mapsLabel, 6);
                m_root->addChild(makeLabel(std::to_string(stage.pool), fontScale(13.0f), {colPool, y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
                if (auto* grade = corum::ranked::ui::PlacementGradeBadge::create(px(34.0f))) {
                    grade->setGrade(stage.stageGrade, false);
                    grade->setPosition({colGrade - px(17.0f), y}); m_root->addChild(grade, 6);
                }
            }
        }
        auto const footY = px(28.0f) + px(38.0f);
        if (!view.error.empty()) {
            auto* error = makeLabel(shorten(view.error, 72), fontScale(13.0f), {rx0, footY + 30.0f}, kRed, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(error, rx1 - rx0, fontScale(13.0f), 0.09f);
            m_root->addChild(error, 6);
        }
        auto const ghostW = px(190.0f), gap = px(12.0f);
        boardButton(tr("show-stage-result", "SHOW STAGE RESULT"), {ghostW, px(42.0f)}, {rx0 + ghostW / 2.0f, footY}, menu_selector(CorumRankedLayer::onHistory),
            15.0f, false, "ranked-placement-final-stages"_spr);
        auto const continueW = rx1 - rx0 - ghostW - gap;
        boardButton(tr("continue", "CONTINUE"), {continueW, px(76.0f)}, {rx1 - continueW / 2.0f, footY}, menu_selector(CorumRankedLayer::onResultClose),
            30.0f, true, "ranked-placement-final-continue"_spr);

        auto const transitionKey = "placement:" + view.placement.runId;
        if (m_rankTransitionPlayedKey != transitionKey) {
            if (playResultRankTransition(
                view.match.ratingEventA,
                view.match.profileBeforeA,
                view.match.profileAfterA
            )) {
                m_rankTransitionPlayedKey = transitionKey;
            }
        }
        if (m_rankTransitionLayer && m_menu) m_menu->setTouchEnabled(false);
        enqueueAchievementToasts(
            "placement:" + view.placement.runId,
            finalResult.achievementUnlocks
        );
    }

    void renderPlacement(RuntimeView const& view) {
        auto const& state = view.placement.state;
        if (state == "PLACEMENT_RESULT") {
            renderPlacementFinal(view);
        } else if (state == "STAGE_RESULT") {
            renderPlacementStageResult(view);
        } else if (state == "MAP_RESULT") {
            renderPlacementMapResult(view);
        } else if (state == "CANCELLED") {
            auto const size = CCDirector::sharedDirector()->getWinSize();
            addScreenHeader(tr("placement-cancelled", "PLACEMENT CANCELLED"), tr("server-run", "SERVER-AUTHORITATIVE RUN"), kRed, kRed);
            addButton(tr("close", "CLOSE"), {size.width / 2.0f, size.height / 2.0f - 30.0f}, menu_selector(CorumRankedLayer::onResultClose), true, 0.64f);
        } else {
            renderPlacementMap(view);
        }
    }

    void renderMatch(RuntimeView const& view) {
        if (view.match.placement) {
            renderPlacement(view);
            return;
        }
        auto const& match = view.match;
        if (match.state == "MATCH_RESULT" || match.state == "CANCELLED") {
            // MATCH_RESULT is immutable server authority. alpha.27 could trap the
            // UI forever on SYNCING RESULT when an ACK was lost even though the
            // server had already finalized. Transport cleanup now happens in the
            // runtime; always render the authoritative result here.
            renderResult(view);
            return;
        }
        if (match.state == "BAN_PHASE") {
            renderBan(match);
            return;
        }
        if (match.state == "ROUND_PREPARE" || match.state == "DEATHMATCH_PREPARE" ||
            match.state == "ROUND_ENTERING" || match.state == "DEATHMATCH_ENTERING") {
            renderPrepare(match);
            return;
        }
        // spectatorActive can arrive after the phase itself (state polling and
        // attempt acknowledgements are independent). Prefer it throughout every
        // live Round phase, and include it in presentationKey(), so a late false
        // -> true projection always replaces OPENING LEVEL with the observer UI.
        if (
            match.spectatorActive &&
            (
                match.state == "ROUND_PLAYING" ||
                match.state == "FINAL_ATTEMPT_WINDOW" ||
                match.state == "LAST_ATTEMPT_WINDOW" ||
                match.state == "ROUND_SETTLING" ||
                match.state == "DEATHMATCH_PLAYING"
            )
        ) {
            renderSpectatorWait(match);
            return;
        }
        if (match.state == "ROUND_SETTLING") {
            renderInterRound(match);
            return;
        }
        if (
            match.state == "ROUND_PLAYING" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "DEATHMATCH_PLAYING"
        ) {
            renderPrepare(match);
            return;
        }
        if (match.state == "MATCHED") {
            renderMatchFound(match);
            return;
        }
        renderInterRound(match);
    }

    // Versus intro (once per match, ~2.4 s): both sides sweep in on slanted
    // slabs, icons slam, names/titles slide, VS slams with a flash, the series
    // format appears, then the intro fades into the match-found board.
    static constexpr float kVersusSettle = 2.35f;
    void addVersusIntro(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto* intro = CCLayerColor::create({0, 0, 0, 255}, 0, 0);
        intro->setCascadeOpacityEnabled(true); intro->setID("ranked-versus-intro"_spr);
        m_root->addChild(intro, 40);
        // Backdrop so the board underneath stays hidden until the settle.
        intro->addChild(CCLayerColor::create({10, 11, 14, 255}, size.width, size.height), 0);
        for (int side = 0; side < 2; ++side) {
            auto const own = side == 0;
            auto const accent = own ? kGold : kSideRed;
            auto const fill = own ? ccc4(30, 32, 41, 255) : ccc4(44, 11, 25, 255);
            auto* slab = CCLayerColor::create(fill, size.width * 0.62f, size.height * 1.6f);
            slab->setSkewX(-12.0f);
            auto* edge = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, 3.0f, size.height * 1.6f);
            edge->setPosition({own ? size.width * 0.62f - 3.0f : 0.0f, 0}); slab->addChild(edge, 1);
            auto const restX = own ? size.width * 0.5f - size.width * 0.62f + 12.0f : size.width * 0.5f - 12.0f;
            slab->setPosition({restX + (own ? -size.width : size.width), -size.height * 0.3f});
            slab->runAction(CCSequence::create(CCDelayTime::create(0.12f),
                CCEaseOut::create(CCMoveTo::create(0.42f, {restX, -size.height * 0.3f}), 3.0f), nullptr));
            intro->addChild(slab, 1);
            // Speed lines.
            for (int i = 0; i < 3; ++i) {
                auto* line = CCLayerColor::create({accent.r, accent.g, accent.b, 90}, 90.0f + 40.0f * (i % 2), 1.5f);
                auto const y = size.height * (0.25f + 0.25f * static_cast<float>(i));
                auto const x = own ? 20.0f + 26.0f * i : size.width - 130.0f - 18.0f * i;
                line->setPosition({x + (own ? -size.width : size.width), y});
                line->runAction(CCSequence::create(CCDelayTime::create(0.12f + 0.04f * i),
                    CCEaseOut::create(CCMoveTo::create(0.6f, {x + (own ? 50.0f : -50.0f), y}), 3.0f), nullptr));
                intro->addChild(line, 2);
            }
            auto const& profile = (own == ownA) ? match.playerAProfile : match.playerBProfile;
            auto const& name = (own == ownA) ? match.playerAName : match.playerBName;
            auto const accountId = (own == ownA) ? match.playerAAccountId : match.playerBAccountId;
            auto const cx = own ? size.width * 0.24f : size.width * 0.76f;
            auto const tierAccent = tierColor(upper(profile.displayedTier));
            auto* label = makeLabel(own ? tr("you", "YOU") : tr("opponent", "OPPONENT"), 0.24f, {cx, size.height - 46.0f}, own ? kGold : ccc3(220, 228, 242), "extrabold");
            label->setOpacity(0); label->runAction(CCSequence::create(CCDelayTime::create(0.62f), CCFadeIn::create(0.26f), nullptr));
            intro->addChild(label, 5);
            // Icon slams in from 1.8x.
            auto* plate = makeNeonPanel({78.0f, 78.0f}, CCPointZero, own ? kGold : kSideRed, {15, 16, 21}, 250);
            auto* icon = own ? corum::ranked::ui::PlayerIcon::createLocal() : corum::ranked::ui::PlayerIcon::createRemote(accountId);
            auto* slam = CCNode::create(); slam->setPosition({cx, size.height * 0.60f}); slam->addChild(plate, 1);
            if (icon) { icon->setScale(1.4f); slam->addChild(icon, 2); }
            slam->setScale(1.8f); slam->setVisible(false);
            slam->runAction(CCSequence::create(CCDelayTime::create(0.42f), CCShow::create(),
                CCEaseBackOut::create(CCScaleTo::create(0.30f, 1.0f)), nullptr));
            intro->addChild(slam, 6);
            // Name slides in from outside, title and rating follow.
            auto* nameLabel = makeLabel(shorten(upper(name), 15), 0.58f, {cx, size.height * 0.60f - 58.0f}, corum::ranked::ui::Text, "black");
            corum::ranked::ui::fitMenuLabelWidth(nameLabel, size.width * 0.40f, 0.58f, 0.32f);
            auto const namePos = nameLabel->getPosition();
            nameLabel->setPosition(namePos + CCPoint{own ? -size.width * 0.5f : size.width * 0.5f, 0}); nameLabel->setOpacity(0);
            nameLabel->runAction(CCSequence::create(CCDelayTime::create(0.62f),
                CCSpawn::create(CCEaseOut::create(CCMoveTo::create(0.36f, namePos), 3.0f), CCFadeIn::create(0.24f), nullptr), nullptr));
            intro->addChild(nameLabel, 6);
            auto lineY = size.height * 0.60f - 80.0f;
            if (profile.equippedTitle.active) {
                auto* title = makeLabel(achievementTitleText(profile.equippedTitle), 0.26f, {cx, lineY}, achievementTitleColor(profile.equippedTitle), "semibold");
                corum::ranked::ui::fitMenuLabelWidth(title, size.width * 0.38f, 0.26f, 0.16f);
                corum::ranked::ui::styleAchievementText(title, profile.equippedTitle.level.value_or(corum::ranked::achievementRarityTier(profile.equippedTitle.rarity)));
                title->setOpacity(0); title->runAction(CCSequence::create(CCDelayTime::create(0.76f), CCFadeIn::create(0.3f), nullptr));
                intro->addChild(title, 6);
                lineY -= 22.0f;
            }
            auto const placement = profile.placementGames < profile.placementGamesRequired;
            auto const rating = placement ? tr("placement", "PLACEMENT")
                : fmt::format("{}  {} LP", corum::ranked::rankTierDisplay(upper(profile.displayedTier)), profile.visibleRankedScore.value_or(0));
            auto* ratingPlate = makeTextPlate(rating, {122.0f, 20.0f}, {cx, lineY}, tierAccent, 0.22f, ccc3(240, 244, 250), "black");
            ratingPlate->setVisible(false);
            ratingPlate->runAction(CCSequence::create(CCDelayTime::create(0.88f), CCShow::create(), nullptr));
            intro->addChild(ratingPlate, 6);
        }
        // VS slam + flash + shake.
        auto* versus = makeLabel("VS", 1.25f, {size.width / 2.0f, size.height * 0.60f}, kGold, "display");
        versus->setRotation(-6.0f); versus->setScale(4.0f); versus->setVisible(false);
        versus->runAction(CCSequence::create(CCDelayTime::create(0.90f), CCShow::create(),
            CCEaseIn::create(CCScaleTo::create(0.22f, 1.25f), 3.0f), nullptr));
        intro->addChild(versus, 8);
        auto* flash = CCLayerColor::create({255, 255, 255, 0}, size.width, size.height);
        flash->runAction(CCSequence::create(CCDelayTime::create(1.12f), CCFadeTo::create(0.03f, 190), CCFadeTo::create(0.36f, 0), nullptr));
        intro->addChild(flash, 9);
        intro->runAction(CCSequence::create(CCDelayTime::create(1.12f),
            CCMoveBy::create(0.04f, {-5.0f, 2.0f}), CCMoveBy::create(0.04f, {9.0f, -4.0f}), CCMoveBy::create(0.04f, {-7.0f, 3.0f}),
            CCMoveBy::create(0.04f, {3.0f, -1.0f}), nullptr));
        // Format and ban rule under the VS.
        auto const format = match.singleGame ? "SOLO RANKED  ·  " + tr("single-game", "SINGLE") : std::string("SOLO RANKED  ·  BO3");
        auto* chip = makeTextPlate(format, {150.0f, 20.0f}, {size.width / 2.0f, size.height * 0.60f - 50.0f}, kSteel, 0.23f, kGold, "black");
        auto* banLine = makeLabel(match.mapBan ? fmt::format("MAP BAN  ·  {}", match.singleGame ? 4 : 5) : tr("no-ban-random-maps", "NO BAN  ·  MAPS DRAWN AT RANDOM"),
            0.20f, {size.width / 2.0f, size.height * 0.60f - 70.0f}, match.mapBan ? ccc3(164, 181, 205) : kGreen, "extrabold");
        chip->setVisible(false); banLine->setOpacity(0);
        chip->runAction(CCSequence::create(CCDelayTime::create(1.35f), CCShow::create(), nullptr));
        banLine->runAction(CCSequence::create(CCDelayTime::create(1.45f), CCFadeIn::create(0.26f), nullptr));
        intro->addChild(chip, 7); intro->addChild(banLine, 7);
        // Settle: fade the whole intro (chrome explicitly) and remove it.
        intro->runAction(CCSequence::create(CCDelayTime::create(kVersusSettle),
            CallFuncExt::create([intro] { corum::ranked::ui::fadeModeLobbySurfaces(intro, 0.32f, false); }),
            corum::ranked::ui::ModeLobbyFade::create(0.32f, 0, corum::ranked::ui::ModeLobbyEase::SineIn),
            CCRemoveSelf::create(), nullptr));
    }

    // M1 (match found), ported from the board: lime and red skewed fields
    // behind you and the opponent, the big leaning VS with the mode chip in
    // between, and MATCH FOUND at the top; the countdown to the ban or round 1
    // sits under the centre line.
    void renderMatchFound(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto const animate = m_matchFoundMotionKey != match.matchId && phaseSeconds() < 3.5;
        m_matchFoundMotionKey = match.matchId;
        auto const lead = animate ? kVersusSettle + 0.2f : 0.0f;
        // Fields: 700 x 720 px at skewX(-12deg) with a 6 px inner rule.
        auto const field = [&](bool left) {
            auto const color = left ? ccc3(27, 36, 16) : ccc3(42, 18, 24);
            auto const rule = left ? corum::ranked::ui::Accent : kRed;
            auto const width = px(700.0f), x = left ? -px(80.0f) : size.width + px(80.0f) - width;
            auto* fill = CCLayerColor::create({color.r, color.g, color.b, 255}, width, size.height + px(80.0f));
            fill->setPosition({x, -px(40.0f)}); fill->setSkewX(12.0f); m_root->addChild(fill, 0);
            auto* edge = CCLayerColor::create({rule.r, rule.g, rule.b, 255}, px(6.0f), size.height + px(80.0f));
            edge->setPosition({left ? x + width - px(6.0f) : x, -px(40.0f)}); edge->setSkewX(12.0f); m_root->addChild(edge, 1);
        };
        field(true); field(false);
        auto const dash = [&](float x, float y, float w, ccColor3B c, GLubyte a) {
            auto* line = CCLayerColor::create({c.r, c.g, c.b, a}, w, px(3.0f)); line->setPosition({x, y}); m_root->addChild(line, 1);
        };
        dash(px(40.0f), size.height - px(150.0f), px(160.0f), corum::ranked::ui::Accent, 128);
        dash(px(80.0f), size.height - px(420.0f), px(110.0f), corum::ranked::ui::Accent, 90);
        dash(size.width - px(40.0f) - px(140.0f), size.height - px(190.0f), px(140.0f), kRed, 128);
        dash(size.width - px(90.0f) - px(100.0f), size.height - px(460.0f), px(100.0f), kRed, 90);
        // Top: MATCH FOUND chip.
        addChip(m_root, tr("match-found", "MATCH FOUND"), {size.width / 2.0f - 34.0f, size.height - px(32.0f)}, corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, 18.0f);
        // Sides: you (left) and the opponent (right).
        auto const sideW = (size.width - px(200.0f)) / 2.0f;
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        for (int side = 0; side < 2; ++side) {
            auto const own = side == 0;
            auto const sideA = own == ownA;
            auto* group = makeMotionGroup();
            m_root = group; m_menu = typeinfo_cast<CCMenu*>(group->getChildByID("ranked-motion-menu"_spr));
            auto const cx = own ? sideW / 2.0f : size.width - sideW / 2.0f;
            auto const cy = size.height / 2.0f;
            m_root->addChild(makeLabel(own ? tr("you", "YOU") : tr("opponent", "OPPONENT"), fontScale(14.0f), {cx, cy + 57.0f},
                own ? corum::ranked::ui::Accent : kRed, "extrabold"), 5);
            CCNode* icon = own ? static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createLocal())
                : static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createRemote(sideA ? match.playerAAccountId : match.playerBAccountId));
            if (icon) { icon->setScale(1.25f); icon->setPosition({cx, cy + 30.0f}); m_root->addChild(icon, 5); }
            auto* name = makeLabel(upper(sideA ? match.playerAName : match.playerBName), fontScale(44.0f), {cx, cy - 2.0f}, corum::ranked::ui::Text, "black");
            corum::ranked::ui::leanLabel(name); corum::ranked::ui::fitMenuLabelWidth(name, sideW - 20.0f, fontScale(44.0f), 0.3f); m_root->addChild(name, 5);
            auto const& profile = sideA ? match.playerAProfile : match.playerBProfile;
            if (profile.equippedTitle.active) {
                auto* title = makeLabel(achievementTitleText(profile.equippedTitle), fontScale(16.0f), {cx, cy - 22.0f}, achievementTitleColor(profile.equippedTitle), "extrabold");
                corum::ranked::ui::fitMenuLabelWidth(title, sideW - 30.0f, fontScale(16.0f), 0.12f); m_root->addChild(title, 5);
            }
            auto const tier = upper(profile.displayedTier);
            auto const chipText = profile.visibleRankedScore ? fmt::format("{}  {} LP", corum::ranked::rankTierDisplay(tier), *profile.visibleRankedScore)
                : corum::ranked::rankTierDisplay(tier);
            auto* chipLabel = makeLabel(chipText, fontScale(15.0f), {cx, cy - 38.0f}, tierColor(tier), "black");
            auto const chipW = chipLabel->getScaledContentSize().width + px(24.0f);
            auto* chip = CCLayerColor::create({32, 35, 46, 255}, chipW, px(26.0f)); chip->setPosition({cx - chipW / 2.0f, cy - 38.0f - px(13.0f)}); chip->setSkewX(8.0f);
            m_root->addChild(chip, 4); m_root->addChild(chipLabel, 5);
            m_root = savedRoot; m_menu = savedMenu;
            if (animate) { group->setPositionX(own ? -60.0f : 60.0f); revealGroup(group, lead + 0.05f, {own ? 60.0f : -60.0f, 0}, 0.45f); }
        }
        // Centre: VS (120 px, rotated and leaning), the mode chip and the ban line.
        auto* versus = makeLabel("VS", fontScale(120.0f), {size.width / 2.0f, size.height / 2.0f + 22.0f}, corum::ranked::ui::Accent, "black");
        versus->setRotation(-6.0f); corum::ranked::ui::leanLabel(versus);
        auto* shadow = makeLabel("VS", fontScale(120.0f), {size.width / 2.0f + 3.0f, size.height / 2.0f + 19.0f}, corum::ranked::ui::Ink, "black");
        shadow->setRotation(-6.0f); corum::ranked::ui::leanLabel(shadow); m_root->addChild(shadow, 4);
        m_root->addChild(versus, 5);
        if (animate) {
            auto const finalScale = versus->getScale();
            for (auto* mark : {versus, shadow}) {
                mark->setScale(0.0f);
                mark->runAction(CCSequence::create(CCDelayTime::create(lead + 0.35f), CCEaseBackOut::create(CCScaleTo::create(0.32f, finalScale)), nullptr));
            }
        }
        auto* centerGroup = makeMotionGroup();
        auto* outerRoot = m_root; m_root = centerGroup;
        auto const chipLabelText = std::string("SOLO RANKED · BO3");
        auto* modeLabel = makeLabel(chipLabelText, fontScale(15.0f), {}, corum::ranked::ui::AccentInk, "black");
        auto const modeW = modeLabel->getScaledContentSize().width + px(24.0f);
        addChip(m_root, chipLabelText, {size.width / 2.0f - modeW / 2.0f, size.height / 2.0f - 22.0f}, corum::ranked::ui::AccentFill, corum::ranked::ui::AccentInk, 15.0f);
        auto* banLine = makeLabel(match.mapBan ? tr("private-ban-phase", "PRIVATE BAN PHASE") : tr("no-ban-random-maps", "NO BAN  ·  MAPS DRAWN AT RANDOM"),
            fontScale(13.0f), {size.width / 2.0f, size.height / 2.0f - 38.0f}, corum::ranked::ui::Text, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(banLine, px(200.0f), fontScale(13.0f), 0.1f); m_root->addChild(banLine, 5);
        auto const left = std::max(0, 5 - static_cast<int>(std::floor(phaseSeconds())));
        m_root->addChild(makeLabel(match.mapBan ? tr("map-ban-in", "MAP BAN IN") : tr("round-one-in", "ROUND 1 IN"), fontScale(13.0f),
            {size.width / 2.0f, size.height / 2.0f - 62.0f}, corum::ranked::ui::TextMuted, "semibold"), 5);
        auto* countdown = makeLabel(fmt::format("{}", left), fontScale(34.0f), {size.width / 2.0f, size.height / 2.0f - 78.0f}, left <= 2 ? kSideRed : kGold, "black");
        m_matchFoundCountdownLabel = countdown; m_root->addChild(countdown, 5);
        m_root = outerRoot;
        if (animate) {
            centerGroup->setPositionY(-10.0f); revealGroup(centerGroup, lead + 0.55f, {0, 10.0f}, 0.35f); addVersusIntro(match);
            // Map/audio preparation progress changes the key during MATCHED;
            // rebuilding would cut the one-shot entrance short.
            holdForMotion(lead + 1.0f);
        }
    }

    // M2 (map ban), ported from the board: a header with BAN MAP, both
    // players' ban state and the timer; the instruction line; five map cards
    // (thumbnail slot with the difficulty, title, creator, rating and length,
    // BAN at the bottom). A ban lands instantly; the card fades and sinks.
    void renderBan(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        auto const headerY = size.height - kLobbyHeader / 2.0f;
        auto* brand = makeLabel(tr("ban-map", "BAN MAP"), fontScale(30.0f), {px(28.0f) + px(48.0f) + px(16.0f), headerY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(brand); brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
        auto const count = std::min<std::size_t>(5, match.candidateMaps.size());
        m_root->addChild(makeLabel(fmt::format("BAN MAP · {} {}", count, tr("ban-one-of", "MAPS, BAN 1")), fontScale(13.0f),
            {brand->getPositionX() + brand->getScaledContentSize().width + 10.0f, headerY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 6);
        // Right: own state · vs · opponent state · timer panel.
        auto const ownA = match.side == "A";
        auto const ownBanned = !m_selectedBan.empty();
        float right = size.width - px(28.0f);
        {
            constexpr float panelW = 46.0f, panelH = 28.0f;
            auto* panel = CCLayerColor::create({23, 25, 34, 255}, panelW, panelH); panel->setPosition({right - panelW, headerY - panelH / 2.0f}); m_root->addChild(panel, 3);
            auto* rule = CCLayerColor::create({corum::ranked::ui::Accent.r, corum::ranked::ui::Accent.g, corum::ranked::ui::Accent.b, 255}, panelW, 1.0f);
            rule->setPosition({right - panelW, headerY + panelH / 2.0f - 1.0f}); m_root->addChild(rule, 4);
            m_root->addChild(makeLabel(tr("time-left", "TIME LEFT"), fontScale(13.0f), {right - panelW / 2.0f, headerY + 7.0f}, corum::ranked::ui::TextMuted, "semibold"), 5);
            auto const remaining = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
            auto* timer = makeLabel(fmt::format("{}", remaining), fontScale(24.0f), {right - panelW / 2.0f, headerY - 5.0f}, remaining <= 3 ? kSideRed : corum::ranked::ui::Text, "black");
            m_banTimerLabel = timer; m_root->addChild(timer, 6);
            right -= panelW + 12.0f;
        }
        auto const playerState = [&](bool own) {
            auto const sideA = own == ownA;
            auto const done = own && ownBanned;
            auto* chipText = makeLabel(done ? tr("ban-done", "BANNED") : tr("ban-choosing", "CHOOSING"), fontScale(13.0f), {}, done ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, "black");
            auto const chipW = chipText->getScaledContentSize().width + 10.0f;
            addChip(m_root, done ? tr("ban-done", "BANNED") : tr("ban-choosing", "CHOOSING"), {right - chipW, headerY}, done ? corum::ranked::ui::AccentFill : ccc3(32, 35, 46),
                done ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
            right -= chipW + 5.0f;
            auto* name = makeLabel(sideA ? match.playerAName : match.playerBName, fontScale(15.0f), {right, headerY}, corum::ranked::ui::Text, "extrabold", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(name, 70.0f, fontScale(15.0f), 0.12f); m_root->addChild(name, 5);
            right -= name->getScaledContentSize().width + 5.0f;
            CCNode* icon = own ? static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createLocal())
                : static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createRemote(sideA ? match.playerAAccountId : match.playerBAccountId));
            if (icon) { icon->setScale(0.42f); icon->setPosition({right - 7.0f, headerY}); m_root->addChild(icon, 5); }
            right -= 19.0f;
        };
        playerState(false);
        m_root->addChild(makeLabel("vs", fontScale(15.0f), {right - 6.0f, headerY}, ccc3(111, 116, 134), "extrabold"), 5);
        right -= 16.0f;
        playerState(true);
        // Instruction line (+ NO BAN on the right).
        auto const lineY = size.height - kLobbyHeader - 9.0f;
        auto* lead = makeLabel(tr("ban-instruction", "BAN ONE MAP."), fontScale(18.0f), {px(28.0f), lineY}, corum::ranked::ui::Text, "extrabold", {0.0f, 0.5f});
        m_root->addChild(lead, 5);
        auto* rest = makeLabel(tr("private-ban-notice", "YOUR BAN IS PRIVATE  |  REVEALED WHEN THE PHASE ENDS"), fontScale(15.0f),
            {lead->getPositionX() + lead->getScaledContentSize().width + 6.0f, lineY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(rest, size.width - px(28.0f) - 90.0f - rest->getPositionX(), fontScale(15.0f), 0.1f); m_root->addChild(rest, 5);
        if (m_selectedBan == kNoBanChoice) addChip(m_root, tr("no-ban", "NO BAN"), {size.width - px(28.0f) - 60.0f, lineY}, kGold, corum::ranked::ui::AccentInk);
        else if (m_pendingBan == kNoBanChoice) addChip(m_root, tr("banning", "BANNING..."), {size.width - px(28.0f) - 60.0f, lineY}, kGold, corum::ranked::ui::AccentInk);
        else if (m_selectedBan.empty() && m_pendingBan.empty())
            boardButton(tr("no-ban", "NO BAN"), {px(150.0f), px(34.0f)}, {size.width - px(28.0f) - px(75.0f), lineY}, menu_selector(CorumRankedLayer::onNoBan), 14.0f, false, "ranked-ban-none"_spr);

        // Cards: five columns, 12 px gaps, between the line and an 18 px margin.
        auto const gap = px(12.0f), x0 = px(28.0f), width = size.width - 2.0f * x0;
        auto const cardW = (width - gap * 4.0f) / 5.0f;
        auto const cardTop = lineY - 9.0f - px(16.0f), cardBottom = px(18.0f), cardH = cardTop - cardBottom;
        auto const startX = size.width / 2.0f - (cardW + gap) * static_cast<float>(count > 0 ? count - 1 : 0) / 2.0f;
        auto const animate = m_banMotionKey != match.matchId;
        m_banMotionKey = match.matchId;
        if (animate) holdForMotion(0.30f + 0.06f * static_cast<float>(count) + 0.4f);
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        for (std::size_t i = 0; i < count; ++i) {
            auto* group = makeMotionGroup();
            m_root = group; m_menu = typeinfo_cast<CCMenu*>(group->getChildByID("ranked-motion-menu"_spr));
            if (animate) { group->setPositionY(-14.0f); revealGroup(group, 0.10f + 0.06f * static_cast<float>(i), {0, 14.0f}, 0.34f); }
            auto const& candidate = match.candidateMaps[i];
            auto const x = startX + (cardW + gap) * static_cast<float>(i), left = x - cardW / 2.0f;
            auto const selected = m_selectedBan == candidate.canonicalLevelId;
            auto const pending = m_pendingBan == candidate.canonicalLevelId;
            auto* panel = CCLayerColor::create({23, 25, 34, 255}, cardW, cardH); panel->setPosition({left, cardBottom}); m_root->addChild(panel, 1);
            // Thumbnail slot (120 px) with the difficulty.
            auto const thumbH = px(120.0f);
            auto* thumb = CCLayerColor::create({32, 35, 46, 255}, cardW, thumbH); thumb->setPosition({left, cardTop - thumbH}); m_root->addChild(thumb, 2);
            addMapDifficulty(candidate, {x, cardTop - thumbH / 2.0f}, cardW - 16.0f, 22.0f);
            if (selected) addChip(m_root, tr("ban", "BAN"), {left + 5.0f, cardTop - 9.0f}, kRed, corum::ranked::ui::AccentInk, 15.0f);
            // Title (18 px), creator, rating and length.
            float y = cardTop - thumbH - 6.0f - 6.0f;
            auto* title = makeLabel(candidate.title, fontScale(18.0f), {left + 6.0f, y}, selected ? corum::ranked::ui::TextMuted : corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(title, cardW - 12.0f, fontScale(18.0f), 0.12f); m_root->addChild(title, 5);
            if (selected) {
                auto* strike = CCLayerColor::create({169, 174, 189, 255}, title->getScaledContentSize().width, 1.0f);
                strike->setPosition({left + 6.0f, y}); m_root->addChild(strike, 6);
            }
            y -= 12.0f;
            if (!candidate.creator.empty()) {
                auto* by = makeLabel("by " + candidate.creator, fontScale(13.0f), {left + 6.0f, y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(by, cardW - 12.0f, fontScale(13.0f), 0.1f); m_root->addChild(by, 5);
            }
            y -= 12.0f;
            auto const lengthMetadata = corum::ranked::ui::displayedMapMetadata(candidate.levelId, candidate.metadata);
            auto* length = makeLabel(corum::ranked::mapLengthText(lengthMetadata), fontScale(13.0f), {left + 6.0f, y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(length, cardW - 12.0f, fontScale(13.0f), 0.1f); m_root->addChild(length, 5);
            // Bottom: BAN (outlined red), BANNING..., or BANNED.
            auto const actionY = cardBottom + px(12.0f) + px(17.0f);
            if (selected) {
                m_root->addChild(makeLabel(tr("banned", "BANNED"), fontScale(13.0f), {x, actionY}, kRed, "extrabold"), 5);
                auto* veil = CCLayerColor::create({13, 14, 18, 0}, cardW, cardH);
                veil->setPosition({left, cardBottom}); m_root->addChild(veil, 9);
                auto const dimKey = match.matchId + ":" + candidate.canonicalLevelId;
                if (m_banDimKey != dimKey) {
                    m_banDimKey = dimKey;
                    veil->runAction(CCFadeTo::create(0.28f, 120));
                    group->runAction(CCEaseSineOut::create(CCMoveBy::create(0.32f, {0, -8.0f})));
                } else { veil->setOpacity(120); group->setPositionY(group->getPositionY() - 8.0f); }
            } else if (pending) {
                m_root->addChild(makeLabel(tr("banning", "BANNING..."), fontScale(13.0f), {x, actionY}, kGold, "extrabold"), 5);
            } else if (m_selectedBan.empty() && m_pendingBan.empty()) {
                auto* face = corum::ranked::ui::slab({cardW - 12.0f, px(34.0f)}, {}, kRed, true, corum::ranked::ui::Ink);
                auto* caption = makeLabel(tr("ban", "BAN"), fontScale(14.0f), {(cardW - 12.0f) / 2.0f, px(17.0f)}, ccc3(255, 217, 223), "black");
                face->addChild(caption, 5);
                auto* button = CCMenuItemSpriteExtra::create(face, this, menu_selector(CorumRankedLayer::onBan));
                button->setPosition({x, actionY}); button->setTag(static_cast<int>(i)); m_menu->addChild(button);
            } else {
                auto* face = corum::ranked::ui::slab({cardW - 12.0f, px(34.0f)}, {x, actionY}, ccc3(32, 35, 46));
                m_root->addChild(face, 4);
                m_root->addChild(makeLabel(tr("ban", "BAN"), fontScale(14.0f), {x, actionY}, ccc3(126, 132, 150), "black"), 5);
            }
            m_root = savedRoot; m_menu = savedMenu;
        }
    }

    // Hidden group appears after `delay`, moving by `offset` while its text,
    // icons and CCLayerColor chrome fade in together (chrome needs its own fade).
    static void revealGroup(CCLayerColor* group, float delay, CCPoint offset, float seconds) {
        if (!group) return;
        group->setVisible(false);
        group->runAction(CCSequence::create(CCDelayTime::create(delay),
            CallFuncExt::create([group, seconds] {
                corum::ranked::ui::setModeLobbyOpacity(group, 0); group->setVisible(true);
                corum::ranked::ui::fadeModeLobbySurfaces(group, seconds * 0.8f, true);
            }),
            CCSpawn::create(CCEaseOut::create(CCMoveBy::create(seconds, offset), 3.0f),
                corum::ranked::ui::ModeLobbyFade::create(seconds * 0.8f, 255,corum::ranked::ui::ModeLobbyEase::SineOut), nullptr), nullptr));
    }

    // A group container at the origin: children keep screen coordinates.
    CCLayerColor* makeMotionGroup(CCPoint position = CCPointZero) {
        auto* group = CCLayerColor::create({0, 0, 0, 255}, 0, 0);
        group->setCascadeOpacityEnabled(true); group->setPosition(position);
        m_root->addChild(group, 3);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); menu->setID("ranked-motion-menu"_spr); group->addChild(menu, 20);
        return group;
    }

    struct SoloLayout { float scale = 1, centerY = 0, sideOffset = 0; CCSize win; };
    SoloLayout soloLayout() const {
        SoloLayout layout; layout.win = CCDirector::sharedDirector()->getWinSize();
        layout.scale = std::clamp(std::min((layout.win.width - 16.0f) / 553.0f, (layout.win.height - 110.0f) / 210.0f), 0.6f, 1.0f);
        // Cards sit centred between the header (bottom ~H-74) and the status bar
        // (top ~34), so scaled-down boards (iPad) drop instead of hugging the top.
        layout.centerY = (layout.win.height - 74.0f + 34.0f) / 2.0f;
        layout.sideOffset = 201.5f * layout.scale;
        return layout;
    }

    void renderPrepare(MatchView const& match) {
        auto const layout = soloLayout();
        auto const& size = layout.win;
        auto const ownA = soloOwnA(match);
        auto const deathmatch = match.deathmatchSequence > 0;
        auto const prepareState = match.state == "ROUND_PREPARE" || match.state == "DEATHMATCH_PREPARE";
        auto const introKey = fmt::format("{}:{}:{}", match.matchId, match.roundNumber, match.deathmatchSequence);
        // The overview + revolving reveal plays once per round, only from the
        // start of PREPARE (a later rebuild or re-entry shows the board directly).
        auto const intro = prepareState && match.currentMap && m_soloIntroPlayed != introKey && phaseSeconds() < 1.5;
        if (prepareState) m_soloIntroPlayed = introKey;
        auto const ownWins = ownA ? match.roundWinsA : match.roundWinsB;
        auto const oppWins = ownA ? match.roundWinsB : match.roundWinsA;
        addSoloHeader(match, intro ? std::string("ROUND OVERVIEW") : soloRoundTitle(match), ownWins, oppWins);
        addSoloClock(match);

        auto& runtime = RankedRuntime::get();
        auto const clearSyncPending = !deathmatch && runtime.localDisplayClears() >= 3 && runtime.hasLocalAttemptInFlight();
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        std::array<CCLayerColor*, 3> columns{};
        // M3: while preparing, the map panel sits left and a PLAYERS / RULES
        // column right (the board's prepare page). Live rounds keep the
        // three-column board (own card, map, opponent card).
        auto const prepareLayout = prepareState || match.state == "ROUND_ENTERING" || match.state == "DEATHMATCH_ENTERING";
        if (prepareLayout) {
            // Board M3: the map panel fills the left; PLAYERS / RULES (300 px) on the right.
            auto const areaTop = size.height - kLobbyHeader - px(8.0f), areaBottom = px(46.0f);
            auto const infoW = px(300.0f), gapW = px(20.0f);
            auto const mapScale = std::min((size.width - px(56.0f) - infoW - gapW) / 225.0f, (areaTop - areaBottom) / 210.0f);
            auto const infoScale = std::min(infoW / 200.0f, (areaTop - areaBottom) / 210.0f);
            auto const midY = (areaTop + areaBottom) / 2.0f;
            auto const xs = std::array<float, 3>{px(28.0f) + 112.5f * mapScale, size.width - px(28.0f) - 100.0f * infoScale, size.width / 2.0f};
            auto const scales = std::array<float, 3>{mapScale, infoScale, 1.0f};
            for (int i = 0; i < 3; ++i) {
                columns[i] = makeSoloColumn({xs[i], midY}, scales[i]);
                m_root = columns[i]; m_menu = typeinfo_cast<CCMenu*>(columns[i]->getChildByID("ranked-solo-column-menu"_spr));
                if (i == 0) addSoloMapColumn(match, clearSyncPending);
                else if (i == 1) addPrepareInfoColumn(match, ownA, deathmatch);
                m_root = savedRoot; m_menu = savedMenu;
            }
            ccColor3B accent = kGold;
            auto const footer = soloFooterText(match, accent);
            auto* footerPlate = makeTextPlate(footer, {225.0f * layout.scale, 22.0f}, {size.width / 2.0f, 18.0f}, accent, 0.27f,
                ccc3(245, 248, 255), "black", &m_prepareFooterLabel);
            m_soloFooterPlate = footerPlate;
            m_root->addChild(footerPlate, 3);
            if (intro) startSoloIntro(match, columns, layout);
            return;
        }
        // ---- M4 (live round between attempts), from the board: a score panel
        // (both players with clear chips and big scores, the -200..+200 score
        // difference bar between them) and a map panel (thumbnail, map, round
        // list, SPECTATE / PLAY AGAIN).
        {
            auto const deathmatchLive = match.deathmatchSequence > 0;
            auto const top = size.height - kLobbyHeader - px(4.0f), bottom = px(22.0f);
            auto const mapW = px(300.0f), gap = px(20.0f);
            auto const left = px(28.0f), right = size.width - px(28.0f);
            auto const scoreRight = right - mapW - gap;
            auto* scorePanel = CCLayerColor::create({23, 25, 34, 255}, scoreRight - left, top - bottom);
            scorePanel->setPosition({left, bottom}); m_root->addChild(scorePanel, 1);
            auto const ownScore = ownA ? match.scoreA : match.scoreB, oppScore = ownA ? match.scoreB : match.scoreA;
            auto const ownClears = ownA ? match.clearsA : match.clearsB, oppClears = ownA ? match.clearsB : match.clearsA;
            auto const midY = (top + bottom) / 2.0f;
            auto const playerRow = [&](bool own, float y) {
                auto const sideA = own == ownA;
                CCNode* icon = own ? static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createLocal())
                    : static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createRemote(sideA ? match.playerAAccountId : match.playerBAccountId));
                if (icon) { icon->setScale(0.62f); icon->setPosition({left + px(24.0f) + 11.0f, y}); m_root->addChild(icon, 5); }
                auto const textX = left + px(24.0f) + 22.0f + px(14.0f);
                auto* name = makeLabel(sideA ? match.playerAName : match.playerBName, fontScale(24.0f), {textX, y + 6.0f}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(name, 110.0f, fontScale(24.0f), 0.15f); m_root->addChild(name, 5);
                if (deathmatchLive) {
                    auto const used = sideA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
                    m_root->addChild(makeLabel(fmt::format("ATTEMPT {}/3", std::min(3, used)), fontScale(13.0f), {textX, y - 8.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
                } else {
                    auto const clears = own ? std::max(ownClears, RankedRuntime::get().localDisplayClears()) : oppClears;
                    for (int i = 0; i < 3; ++i) {
                        auto const done = i < clears;
                        addChip(m_root, done ? tr("cleared-short", "CLEAR") : std::string("-"), {textX + static_cast<float>(i) * 34.0f, y - 8.0f},
                            done ? (own ? corum::ranked::ui::AccentFill : kRed) : ccc3(32, 35, 46), done ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted);
                    }
                    m_root->addChild(makeLabel(fmt::format("{} / 3", clears), fontScale(13.0f), {textX + 104.0f, y - 8.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
                }
                auto* score = makeLabel(formatScore(own ? ownScore : oppScore, deathmatchLive), fontScale(56.0f), {scoreRight - px(24.0f) - 12.0f, y},
                    own ? corum::ranked::ui::Accent : corum::ranked::ui::Text, "black", {1.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(score, 110.0f, fontScale(56.0f), 0.3f); m_root->addChild(score, 5);
                m_root->addChild(makeLabel(deathmatchLive ? "%" : tr("points-short", "PTS"), fontScale(24.0f), {scoreRight - px(24.0f), y - 4.0f},
                    own ? corum::ranked::ui::Text : corum::ranked::ui::TextMuted, "black", {1.0f, 0.5f}), 5);
            };
            playerRow(true, midY + 52.0f);
            playerRow(false, midY - 52.0f);
            // Score difference bar (-200 .. +200) between the rows.
            auto const barX = left + px(24.0f), barW = scoreRight - left - px(48.0f);
            auto const diff = std::clamp(ownScore - oppScore, -200.0, 200.0);
            m_root->addChild(makeLabel("-200", fontScale(13.0f), {barX, midY + 18.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
            m_root->addChild(makeLabel("+200", fontScale(13.0f), {barX + barW, midY + 18.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f}), 5);
            m_root->addChild(makeLabel(fmt::format("{} {}{}", tr("score-diff", "SCORE DIFF"), diff >= 0 ? "+" : "", formatScore(diff, false)), fontScale(16.0f),
                {barX + barW / 2.0f, midY + 18.0f}, diff >= 0 ? corum::ranked::ui::Accent : kRed, "black"), 5);
            auto* track = CCLayerColor::create({32, 35, 46, 255}, barW, px(20.0f)); track->setPosition({barX, midY - px(10.0f)}); track->setSkewX(20.0f); m_root->addChild(track, 4);
            auto const fillW = static_cast<float>(std::abs(diff) / 200.0) * barW / 2.0f;
            if (fillW > 0.5f) {
                auto const fillColor = diff >= 0 ? corum::ranked::ui::Accent : kRed;
                auto* fill = CCLayerColor::create({fillColor.r, fillColor.g, fillColor.b, 255}, fillW, px(20.0f));
                fill->setPosition({diff >= 0 ? barX + barW / 2.0f : barX + barW / 2.0f - fillW, midY - px(10.0f)}); fill->setSkewX(20.0f); m_root->addChild(fill, 5);
            }
            auto* tick = CCLayerColor::create({244, 245, 247, 255}, 1.0f, px(32.0f)); tick->setPosition({barX + barW / 2.0f - 0.5f, midY - px(16.0f)}); m_root->addChild(tick, 6);
            m_root->addChild(makeLabel(tr("opponent-ahead", "OPPONENT AHEAD"), fontScale(13.0f), {barX, midY - 17.0f}, kRed, "semibold", {0.0f, 0.5f}), 5);
            m_root->addChild(makeLabel(tr("you-ahead", "YOU AHEAD"), fontScale(13.0f), {barX + barW, midY - 17.0f}, corum::ranked::ui::Accent, "semibold", {1.0f, 0.5f}), 5);
            // Footer note (status line kept live through m_prepareFooterLabel).
            ccColor3B accent = kGold;
            auto* note = makeLabel(soloFooterText(match, accent), fontScale(13.0f), {(left + scoreRight) / 2.0f, bottom + 10.0f}, corum::ranked::ui::TextMuted, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(note, scoreRight - left - 20.0f, fontScale(13.0f), 0.1f);
            m_prepareFooterLabel = note; m_root->addChild(note, 5);

            // Map panel: thumbnail, title, rating line, round list, buttons.
            auto const mapX = right - mapW;
            auto* mapPanel = CCLayerColor::create({23, 25, 34, 255}, mapW, top - bottom); mapPanel->setPosition({mapX, bottom}); m_root->addChild(mapPanel, 1);
            auto const thumbH = px(140.0f);
            auto* thumbBack = CCLayerColor::create({32, 35, 46, 255}, mapW, thumbH); thumbBack->setPosition({mapX, top - thumbH}); m_root->addChild(thumbBack, 2);
            addSoloThumbnail(m_root, {mapX + mapW / 2.0f, top - thumbH / 2.0f}, {mapW, thumbH}, match);
            float y = top - thumbH - px(14.0f) - 6.0f;
            if (match.currentMap) {
                auto const& map = *match.currentMap;
                auto* title = makeLabel(map.title, fontScale(24.0f), {mapX + px(16.0f), y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(title, mapW - px(32.0f), fontScale(24.0f), 0.15f); m_root->addChild(title, 5);
                y -= 13.0f;
                auto const lengthMetadata = corum::ranked::ui::displayedMapMetadata(map.levelId, map.metadata);
                auto* info = makeLabel(fmt::format("{}{}", corum::ranked::mapLengthText(lengthMetadata), map.creator.empty() ? std::string() : " · by " + map.creator),
                    fontScale(13.0f), {mapX + px(16.0f), y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
                corum::ranked::ui::fitMenuLabelWidth(info, mapW - px(32.0f), fontScale(13.0f), 0.1f); m_root->addChild(info, 5);
            }
            y -= 16.0f;
            auto const rounds = match.singleGame ? 1 : 3;
            for (int number = 1; number <= rounds; ++number) {
                std::string outcome; double ownRound = 0, oppRound = 0; bool done = false;
                for (auto const& round : match.rounds) if (round.roundNumber == number) {
                    done = true; outcome = round.result; ownRound = ownA ? round.scoreA : round.scoreB; oppRound = ownA ? round.scoreB : round.scoreA;
                }
                m_root->addChild(makeLabel(fmt::format("ROUND {}", number), fontScale(13.0f), {mapX + px(16.0f), y}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
                auto const live = !done && number == match.roundNumber && !deathmatchLive;
                auto const won = done && outcome != "DRAW" && (outcome == "A") == ownA;
                auto const chipText = done ? fmt::format("{} · {} : {}", outcome == "DRAW" ? tr("draw-short", "D") : won ? tr("win-short", "W") : tr("lose-short", "L"),
                    formatScore(ownRound, false), formatScore(oppRound, false)) : live ? tr("round-live", "PLAYING") : tr("round-waiting", "WAITING");
                auto* chipLabel = makeLabel(chipText, fontScale(13.0f), {}, corum::ranked::ui::AccentInk, "black");
                auto const chipW = chipLabel->getScaledContentSize().width + 10.0f;
                addChip(m_root, chipText, {mapX + mapW - px(16.0f) - chipW, y}, done ? (won ? corum::ranked::ui::AccentFill : outcome == "DRAW" ? ccc3(43, 47, 60) : kRed) : ccc3(32, 35, 46),
                    done ? corum::ranked::ui::AccentInk : live ? corum::ranked::ui::Text : corum::ranked::ui::TextMuted);
                y -= 15.0f;
            }
            // Buttons: SPECTATE (ghost) and PLAY AGAIN (filled), at the bottom.
            auto const buttonsY = bottom + px(14.0f) + px(17.0f);
            auto const ghostW = (mapW - px(32.0f) - px(8.0f)) / 2.6f, playW = mapW - px(32.0f) - px(8.0f) - ghostW;
            auto const canSpectate = match.spectatorLiveEnabled && RankedRuntime::get().canEnterCurrentLevelAsSpectator();
            m_soloSpectateButton = boardButton(tr("spectate-live", "SPECTATE"), {ghostW, px(34.0f)}, {mapX + px(16.0f) + ghostW / 2.0f, buttonsY},
                menu_selector(CorumRankedLayer::onSpectateLive), 14.0f, false, "ranked-solo-spectate"_spr, canSpectate);
            boardButton(tr("play-again", "PLAY AGAIN"), {playW, px(42.0f)}, {mapX + mapW - px(16.0f) - playW / 2.0f, buttonsY},
                menu_selector(CorumRankedLayer::onSoloPlayAgain), 16.0f, true, "ranked-solo-play-again"_spr);
        }
    }

    // M4: re-enter the current round's level between attempts.
    void onSoloPlayAgain(CCObject*) {
        if (m_enteringLevel) return;
        maybeEnterLevel();
    }


    // M3 right column: PLAYERS (ready state of both) and RULES panels.
    void addPrepareInfoColumn(MatchView const& match, bool ownA, bool deathmatch) {
        auto const panel = [&](float top, float height, std::string const& caption) {
            auto* fill = CCLayerColor::create({23, 25, 34, 255}, 200.0f, height); fill->setPosition({-100.0f, top - height}); m_root->addChild(fill, 1);
            auto* rule = CCLayerColor::create({corum::ranked::ui::Accent.r, corum::ranked::ui::Accent.g, corum::ranked::ui::Accent.b, 255}, 200.0f, 1.0f);
            rule->setPosition({-100.0f, top - 1.0f}); m_root->addChild(rule, 2);
            auto* label = makeLabel(caption, 0.20f, {-90.0f, top - 11.0f}, kAccent, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(label, 120.0f, 0.20f, 0.14f); m_root->addChild(label, 5);
        };
        panel(105.0f, 96.0f, tr("prepare-players", "PLAYERS"));
        for (int row = 0; row < 2; ++row) {
            auto const own = row == 0;
            auto const sideA = own == ownA;
            auto const y = 105.0f - 38.0f - 28.0f * static_cast<float>(row);
            auto const ready = sideA ? match.readyA : match.readyB;
            auto const accountId = sideA ? match.playerAAccountId : match.playerBAccountId;
            CCNode* icon = own ? static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createLocal()) : static_cast<CCNode*>(corum::ranked::ui::PlayerIcon::createRemote(accountId));
            if (icon) { icon->setScale(0.42f); icon->setPosition({-80.0f, y}); m_root->addChild(icon, 5); }
            auto* name = makeLabel(sideA ? match.playerAName : match.playerBName, 0.26f, {-66.0f, y}, own ? kAccent : corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(name, 96.0f, 0.26f, 0.16f); m_root->addChild(name, 5);
            auto* chip = corum::ranked::ui::slab({52.0f, 13.0f}, {66.0f, y}, ready ? corum::ranked::ui::AccentFill : corum::ranked::ui::SurfaceSoft);
            m_root->addChild(chip, 5);
            auto* state = makeLabel(ready ? tr("prepare-ready", "READY") : tr("prepare-preparing", "PREPARING"), 0.17f, {66.0f, y},
                ready ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, "black");
            corum::ranked::ui::fitMenuLabelWidth(state, 44.0f, 0.17f, 0.11f); m_root->addChild(state, 6);
        }
        panel(1.0f, 106.0f, tr("prepare-rules", "RULES"));
        std::array<std::string, 2> rules = deathmatch
            ? std::array<std::string, 2>{tr("rule-deathmatch-progress", "DEATH MATCH: BEST PROGRESS WINS"), tr("rule-deathmatch-attempts", "3 ATTEMPTS")}
            : std::array<std::string, 2>{tr("rule-round-score", "SCORE MATCH · 3 CLEARS WIN THE ROUND"), tr("rule-round-draw", "A DRAW GOES TO A DEATH MATCH (BEST PROGRESS)")};
        for (std::size_t index = 0; index < rules.size(); ++index) {
            auto* bullet = CCLayerColor::create({kAccent.r, kAccent.g, kAccent.b, 255}, 3.0f, 3.0f);
            auto const y = 1.0f - 34.0f - 24.0f * static_cast<float>(index);
            bullet->setPosition({-90.0f, y - 1.5f}); m_root->addChild(bullet, 5);
            auto* line = makeLabel(rules[index], 0.19f, {-82.0f, y}, corum::ranked::ui::Text, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(line, 170.0f, 0.19f, 0.11f); m_root->addChild(line, 5);
        }
    }

    void startSoloIntro(MatchView const& match, std::array<CCLayerColor*, 3> const& columns, SoloLayout const& layout) {
        using corum::ranked::ui::SoloIntroReveal;
        int revealSlot = 2; bool shift = false;
        auto cards = soloIntroCards(match, revealSlot, shift);
        auto const& map = *match.currentMap;
        SoloIntroReveal reveal;
        reveal.label = soloRoundTitle(match);
        reveal.chip = match.deathmatchSequence > 0 ? "3 ATTEMPTS" : match.roundNumber >= 3 ? "DECIDER" : "NEXT";
        reveal.accent = match.deathmatchSequence > 0 ? kSideRed : kAccent;
        reveal.mapTitle = map.title; reveal.creator = map.creator.empty() ? "-" : map.creator;
        auto const lengthMetadata = corum::ranked::ui::displayedMapMetadata(map.levelId, map.metadata);
        reveal.detail = fmt::format("{}  ·  {}", upper(map.difficulty), corum::ranked::mapLengthText(lengthMetadata));
        reveal.detailColor = difficultyColor(map.difficulty);
        reveal.levelText = fmt::format("GD #{}", map.levelId);
        auto* thumbHolder = CCNode::create();
        if (addSoloThumbnail(thumbHolder, {0, 0}, corum::ranked::ui::kSoloIntroThumb, match)) reveal.thumbnail = thumbHolder;
        // Columns stay hidden (and untouchable) until the board rises.
        // Columns are fully hidden (their CCLayerColor chrome ignores cascaded
        // opacity) until the board rises after the reveal.
        for (auto* column : columns) {
            column->setVisible(false);
            column->setPositionY(column->getPositionY() - 13.0f);
            setLobbyNodeInput(column, false);
        }
        if (m_soloFooterPlate) m_soloFooterPlate->setVisible(false);
        m_soloIntroRunning = true;
        auto const deathmatchEntry = match.deathmatchSequence > 0;
        auto const round = match.roundNumber;
        auto weak = WeakRef(this);
        auto* intro = corum::ranked::ui::SoloRoundIntro::create(cards, reveal, shift, revealSlot, static_cast<float>(phaseSeconds()),
            [weak, deathmatchEntry, round](int phase) {
                auto self = weak.lock(); if (!self || !self->m_prepareFooterLabel) return;
                std::string text = deathmatchEntry ? std::string("DEATH MATCH") : fmt::format("ROUND {}  ·  SELECTING MAP...", round);
                if (phase == 2) text = "REVEALING MAP...";
                if (phase == 3) text = "MAP DECIDED";
                if (self->m_soloFooterPlate) self->m_soloFooterPlate->setVisible(true);
                self->m_prepareFooterLabel->setString(text.c_str());
            },
            [weak, columns, title = soloRoundTitle(match)] {
                auto self = weak.lock(); if (!self) return;
                if (self->m_soloTabLabel) self->m_soloTabLabel->setString(title.c_str());
                for (std::size_t i = 0; i < columns.size(); ++i)
                    revealGroup(columns[i], corum::ranked::kSoloIntroRiseStagger * static_cast<float>(i), {0, 13.0f}, corum::ranked::kSoloIntroRiseSeconds);
            },
            [weak] {
                // Rebuild once without a fade so deferred readiness changes land
                // on the now-static board (the intro node is no longer updating).
                queueInMainThread([weak] {
                    auto self = weak.lock(); if (!self || !self->getParent()) return;
                    self->m_soloIntroRunning = false;
                    if (self->m_page != Page::Live || self->m_uiFadingOut) return;
                    self->m_renderedUiKey = self->presentationKey(); self->m_pendingUiKey.clear();
                    self->render();
                });
            });
        if (!intro) { m_soloIntroRunning = false; return; }
        intro->setPosition({layout.win.width / 2.0f, layout.centerY});
        intro->setScale(std::min(layout.scale, (layout.win.width - 16.0f) / 480.0f));
        intro->setID("ranked-solo-round-intro"_spr);
        m_root->addChild(intro, 8);
    }

    // ---- Solo board (round overview, revolving reveal, VS board) ------------

    bool soloOwnA(MatchView const& match) const { return match.side == "A"; }

    std::string soloRoundTitle(MatchView const& match) const {
        return match.deathmatchSequence > 0 ? fmt::format("DEATH MATCH {}", match.deathmatchSequence) : fmt::format("ROUND {}", match.roundNumber);
    }

    // Header: mode chip (top-left), series scoreboard with BO3 pips (top-centre)
    // and the death-match / match-point banner under it. Own side is on the left.
    // M3-M5 header (.hdr, 72 px), from the boards: the leaning ROUND title, a
    // dim chip with the live series score (own : opponent · BO3), the state
    // line, and the time panel on the right (addSoloClock). The series digits
    // stay separate labels so the round-win roll can animate them.
    void addSoloHeader(MatchView const& match, std::string const& tab, int ownWins, int opponentWins, bool modeChip = true) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = soloOwnA(match);
        auto const y = size.height - kLobbyHeader / 2.0f;
        auto* title = makeLabel(tab, fontScale(30.0f), {px(28.0f), y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(title); corum::ranked::ui::fitMenuLabelWidth(title, 150.0f, fontScale(30.0f), 0.25f);
        m_soloTabLabel = title; m_root->addChild(title, 6);
        // Series chip: [own] : [opponent] · BO3 (single game: · 1 GAME).
        auto x = px(28.0f) + std::min(150.0f, title->getScaledContentSize().width) + 12.0f;
        auto const chipH = px(26.0f);
        auto* chip = CCLayerColor::create({32, 35, 46, 255}, 10.0f, chipH); chip->setPosition({x, y - chipH / 2.0f}); chip->setSkewX(8.0f); m_root->addChild(chip, 3);
        float cursor = x + 7.0f;
        m_soloPips.clear();
        m_soloOwnScore = makeLabel(std::to_string(ownWins), fontScale(15.0f), {cursor, y}, kGold, "black", {0.0f, 0.5f}); m_root->addChild(m_soloOwnScore, 5);
        cursor += m_soloOwnScore->getScaledContentSize().width + 3.0f;
        auto* colon = makeLabel(":", fontScale(15.0f), {cursor, y}, corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f}); m_root->addChild(colon, 5);
        cursor += colon->getScaledContentSize().width + 3.0f;
        m_soloOppScore = makeLabel(std::to_string(opponentWins), fontScale(15.0f), {cursor, y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f}); m_root->addChild(m_soloOppScore, 5); m_soloDigitScale = fontScale(15.0f);
        cursor += m_soloOppScore->getScaledContentSize().width + 4.0f;
        auto* format = makeLabel(match.singleGame ? std::string("· ") + tr("single-game", "SINGLE") : std::string("· BO3"), fontScale(15.0f), {cursor, y},
            corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f});
        m_root->addChild(format, 5);
        cursor += format->getScaledContentSize().width + 7.0f;
        chip->setContentSize({cursor - x, chipH});
        // State line: death match / tiebreaker banner, else the two names.
        std::string line;
        ccColor3B lineColor = corum::ranked::ui::TextMuted;
        if (match.deathmatchSequence > 0) { line = "DEATH MATCH  ·  3 ATTEMPTS"; lineColor = kGold; }
        else if (!match.banner.empty() && match.banner != "NONE") { line = displayBanner(match.banner); lineColor = match.banner == "TIEBREAKER" ? kGold : kSideRed; }
        else line = fmt::format("{}  vs  {}", ownA ? match.playerAName : match.playerBName, ownA ? match.playerBName : match.playerAName);
        auto* state = makeLabel(line, fontScale(13.0f), {cursor + 8.0f, y}, lineColor, "semibold", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(state, std::max(40.0f, size.width - px(28.0f) - 70.0f - cursor - 8.0f), fontScale(13.0f), 0.1f);
        m_root->addChild(state, 5);
        (void)modeChip;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        // Debug Bot only: settle the live Round now from committed scores.
        if (modeChip && RankedRuntime::get().canDebugEndRound()) {
            auto* end = addButton(tr("survival-end-round", "END ROUND"), {px(28.0f) + 30.0f, size.height - kLobbyHeader - 14.0f},
                menu_selector(CorumRankedLayer::onDebugEndRound), false, 0.34f);
            end->setID("ranked-debug-end-round"_spr);
        }
#endif
    }

    // Top-right clock: countdown in PREPARE, own death-match attempt, or LIVE.
    std::pair<std::string, std::string> soloClockText(MatchView const& match, ccColor3B& color) const {
        auto const prepare = match.state == "ROUND_PREPARE" || match.state == "DEATHMATCH_PREPARE";
        if (prepare) {
            auto const seconds = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
            color = seconds <= 3 ? kSideRed : corum::ranked::ui::Text;
            return {tr("starts-in", "STARTS IN"), fmt::format("0:{:02d}", seconds)};
        }
        if (match.state == "DEATHMATCH_PLAYING" || match.deathmatchSequence > 0) {
            auto used = soloOwnA(match) ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
            used = std::max(used, RankedRuntime::get().localDeathmatchVisualAttemptsUsed());
            color = kGold;
            return {"ATTEMPT", fmt::format("{}/3", std::min(3, used + (used < 3 ? 1 : 0)))};
        }
        color = corum::ranked::ui::Accent;
        return {"ROUND", "LIVE"};
    }

    // The time panel (.panel.rule): a lime top rule over caption and value.
    void addSoloClock(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        ccColor3B color = corum::ranked::ui::Text;
        auto const [caption, value] = soloClockText(match, color);
        constexpr float panelW = 54.0f, panelH = 30.0f;
        auto const cx = size.width - px(28.0f) - panelW / 2.0f, cy = size.height - kLobbyHeader / 2.0f;
        m_soloClockPanel = makeNeonPanel({panelW, panelH}, {cx, cy}, corum::ranked::ui::Accent, {23, 25, 34}, 255);
        m_root->addChild(m_soloClockPanel, 2);
        m_soloClockCaption = makeLabel(caption, fontScale(13.0f), {cx, cy + 8.0f}, corum::ranked::ui::TextMuted, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(m_soloClockCaption, panelW - 6.0f, fontScale(13.0f), 0.1f); m_root->addChild(m_soloClockCaption, 5);
        m_soloClockLabel = makeLabel(value, fontScale(34.0f), {cx, cy - 5.0f}, color, "black");
        corum::ranked::ui::fitMenuLabelWidth(m_soloClockLabel, panelW - 8.0f, fontScale(34.0f), 0.3f); m_root->addChild(m_soloClockLabel, 5);
    }

    void updateSoloClock(MatchView const& match) {
        if (!m_soloClockLabel || !m_soloClockCaption) return;
        ccColor3B color = corum::ranked::ui::Text;
        auto const [caption, value] = soloClockText(match, color);
        if (caption != m_soloClockCaption->getString()) m_soloClockCaption->setString(caption.c_str());
        if (value != m_soloClockLabel->getString()) { m_soloClockLabel->setString(value.c_str()); corum::ranked::ui::fitMenuLabelWidth(m_soloClockLabel, 46.0f, fontScale(34.0f), 0.3f); }
        m_soloClockLabel->setColor(color);
    }

    // Bottom status bar message (the countdown itself lives in the clock).
    std::string soloFooterText(MatchView const& match, ccColor3B& accent) const {
        auto const ownA = soloOwnA(match);
        auto& runtime = RankedRuntime::get();
        auto const clearSyncPending = match.deathmatchSequence <= 0 && runtime.localDisplayClears() >= 3 && runtime.hasLocalAttemptInFlight();
        if (clearSyncPending) { accent = kAccent; return tr("syncing-clear", "SYNCING CLEAR..."); }
        if (match.state == "DEATHMATCH_PLAYING") {
            auto ownUsed = ownA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
            auto const opponentUsed = ownA ? match.deathmatchAttemptsUsedB : match.deathmatchAttemptsUsedA;
            ownUsed = std::max(ownUsed, runtime.localDeathmatchVisualAttemptsUsed());
            accent = ownUsed >= 3 ? kSteel : kGold;
            return ownUsed >= 3 ? fmt::format("{}   {}/3", tr("waiting-opponent", "WAITING FOR OPPONENT"), opponentUsed)
                : corum::ranked::i18n::attemptOfThree(std::min(3, ownUsed + 1));
        }
        auto const ownReady = ownA ? match.readyA : match.readyB;
        auto const opponentReady = ownA ? match.readyB : match.readyA;
        accent = kGold;
        if (phaseSeconds() < 10.0) {
            if (corum::ranked::RankedRoundPreparation::get().ready()) { accent = kGreen; return tr("ready-to-start", "READY TO START"); }
            return tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
        }
        if (!ownReady) return corum::ranked::RankedRoundPreparation::get().ready() ? tr("sending-ready", "SENDING READY...") : tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
        if (!opponentReady) return corum::ranked::i18n::waitingForPlayer(upper(shorten(match.opponentName, 13)));
        accent = kGreen;
        return tr("starting", "STARTING...");
    }

    // Per-player round state shown at the bottom of each VS card.
    void addSoloStrip(MatchView const& match, bool own, CCPoint center, float width) {
        auto const ownA = soloOwnA(match);
        auto const sideA = own == ownA;
        if (match.deathmatchSequence > 0) {
            auto const used = sideA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
            auto const shown = own ? std::max(used, RankedRuntime::get().localDeathmatchVisualAttemptsUsed()) : used;
            m_root->addChild(makeNeonPanel({width, 21.0f}, center, own ? kGold : corum::ranked::ui::TextMuted, kDeepPanel2, 236), 3);
            auto* caption = makeLabel("ATTEMPTS", 0.19f, {center.x - width / 2.0f + 8.0f, center.y}, corum::ranked::ui::TextMuted, "semibold");
            caption->setAnchorPoint({0, 0.5f}); m_root->addChild(caption, 5);
            for (int i = 0; i < 3; ++i) {
                auto const filled = i < shown;
                auto const color = own ? kGold : corum::ranked::ui::Text;
                auto* pip = CCLayerColor::create(filled ? ccc4(color.r, color.g, color.b, 255) : ccc4(164, 181, 205, 70), 7.0f, 7.0f);
                pip->setPosition({center.x + width / 2.0f - 34.0f + i * 10.0f, center.y - 3.5f}); m_root->addChild(pip, 5);
            }
            return;
        }
        auto const prepare = match.state == "ROUND_PREPARE" || match.state == "ROUND_ENTERING";
        std::string text; ccColor3B color = kAccent;
        if (prepare) {
            auto const ready = sideA ? match.readyA : match.readyB;
            if (own && !ready && !corum::ranked::RankedRoundPreparation::get().ready()) text = tr("downloading", "DOWNLOADING...");
            else if (ready || (own && corum::ranked::RankedRoundPreparation::get().ready())) { text = tr("ready", "READY"); color = kGreen; }
            else text = tr("preparing", "PREPARING...");
        } else {
            auto const score = sideA ? match.scoreA : match.scoreB;
            auto const clears = sideA ? match.clearsA : match.clearsB;
            text = fmt::format("{}  {}  ·  {} {}", tr("score", "SCORE"), formatScore(score, false), tr("cleared", "CLEARED"), clears);
            color = own ? kGold : corum::ranked::ui::Text;
        }
        auto* plate = makeTextPlate(text, {width, 21.0f}, center, color, 0.24f, color, "black");
        m_root->addChild(plate, 3);
    }

    CCLayerColor* makeSoloColumn(CCPoint position, float scale) {
        auto* column = CCLayerColor::create({0, 0, 0, 255}, 0, 0);
        column->setCascadeOpacityEnabled(true); column->setPosition(position); column->setScale(scale);
        m_root->addChild(column, 3);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); menu->setID("ranked-solo-column-menu"_spr); column->addChild(menu, 20);
        return column;
    }

    CCSprite* addSoloThumbnail(CCNode* parent, CCPoint center, CCSize size, MatchView const& match) {
        if (!match.currentMap || match.currentMap->levelId <= 0 || !corum::ranked::SurvivalThumbnailProvider::supported()) return nullptr;
        auto const context = corum::ranked::SurvivalSelectionContext{match.matchId,
            fmt::format("solo:{}:{}", match.roundNumber, match.deathmatchSequence), 0, static_cast<std::uint64_t>(match.currentMap->levelId), 0};
        if (!m_soloThumbContext.sameReveal(context) || m_soloThumbContext.levelId != context.levelId) {
            m_soloThumbContext = context; m_soloThumbContext.sceneGeneration = ++m_historyThumbnailGeneration; m_soloThumbTexture = nullptr;
        }
        auto* sprite = CCSprite::create(); sprite->setPosition(center); parent->addChild(sprite, 6);
        m_soloThumbs.push_back({sprite, size});
        return sprite;
    }

    void updateSoloThumbnails() {
        if (m_soloThumbs.empty() || !m_soloThumbContext.levelId) return;
        auto& provider = corum::ranked::SurvivalThumbnailProvider::history(); provider.activate(m_soloThumbContext);
        auto texture = provider.selected(); if (!texture) return;
        auto const size = texture->getContentSize(); if (size.width <= 0 || size.height <= 0) return;
        for (auto const& [sprite, box] : m_soloThumbs) {
            if (sprite->getTexture() == texture.data()) continue;
            sprite->setTexture(texture.data()); sprite->setTextureRect({0, 0, size.width, size.height});
            sprite->setScale(std::min(box.width / size.width, box.height / size.height));
        }
        m_soloThumbTexture = texture;
    }

    // Centre column: map identity, difficulty band (+thumbnail), readiness rows,
    // load bar and the round options. Local coordinates (column centre = 0,0).
    void addSoloMapColumn(MatchView const& match, bool clearSyncPending) {
        // Board .panel: a flat dark fill with a lime top rule.
        auto* panelFill = CCLayerColor::create({23, 25, 34, 255}, 225.0f, 210.0f); panelFill->setPosition({-112.5f, -105.0f}); m_root->addChild(panelFill, 1);
        auto* panelRule = CCLayerColor::create({corum::ranked::ui::Accent.r, corum::ranked::ui::Accent.g, corum::ranked::ui::Accent.b, 255}, 225.0f, 1.0f);
        panelRule->setPosition({-112.5f, 104.0f}); m_root->addChild(panelRule, 2);
        auto* caption = makeLabel(fmt::format("{}  ·  MAP", soloRoundTitle(match)), 0.21f, {-100.0f, 92.0f}, kAccent, "semibold");
        caption->setAnchorPoint({0, 0.5f}); corum::ranked::ui::fitMenuLabelWidth(caption, 130.0f, 0.21f, 0.15f); m_root->addChild(caption, 5);
        if (match.currentMap) {
            auto const& map = *match.currentMap;
            auto* id = makeLabel(fmt::format("GD #{}", map.levelId), 0.19f, {100.0f, 92.0f}, corum::ranked::ui::TextMuted, "semibold");
            id->setAnchorPoint({1, 0.5f}); m_root->addChild(id, 5);
            auto* title = makeLabel(map.title, 0.47f, {-100.0f, 75.0f}, corum::ranked::ui::Text, "black");
            title->setAnchorPoint({0, 0.5f}); corum::ranked::ui::fitMenuLabelWidth(title, 200.0f, 0.47f, 0.24f); m_root->addChild(title, 5);
            if (!map.creator.empty()) {
                auto* creator = makeLabel("by " + map.creator, 0.22f, {-100.0f, 58.0f}, corum::ranked::ui::TextMuted, "semibold");
                creator->setAnchorPoint({0, 0.5f}); corum::ranked::ui::fitMenuLabelWidth(creator, 200.0f, 0.22f, 0.15f); m_root->addChild(creator, 5);
            }
            auto const diffColor = difficultyColor(map.difficulty);
            { auto* band = CCLayerColor::create({32, 35, 46, 255}, 205.0f, 42.0f); band->setPosition({-102.5f, 6.0f}); m_root->addChild(band, 2);
              auto* edge = CCLayerColor::create({diffColor.r, diffColor.g, diffColor.b, 255}, 3.0f, 42.0f); edge->setPosition({-102.5f, 6.0f}); m_root->addChild(edge, 3); }
            auto const thumb = corum::ranked::SurvivalThumbnailProvider::supported() && map.levelId > 0;
            auto const badgeX = thumb ? -34.0f : 0.0f, badgeWidth = thumb ? 120.0f : 150.0f;
            addMapDifficulty(map, {badgeX, 34.0f}, badgeWidth, 16);
            auto const lengthMetadata = corum::ranked::ui::displayedMapMetadata(map.levelId, map.metadata);
            auto* length = makeLabel(corum::ranked::mapLengthText(lengthMetadata), 0.24f, {badgeX, 18.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(length, badgeWidth, 0.24f, 0.16f); m_root->addChild(length, 6);
            if (thumb) {
                m_root->addChild(makeNeonPanel({62.0f, 36.0f}, {66.0f, 27.0f}, kSteel, {15, 16, 21}, 248), 3);
                addSoloThumbnail(m_root, {66.0f, 27.0f}, {60.0f, 34.0f}, match);
            }
        }
        addPreparationStatusRows({-52.0f, -8.0f}, {52.0f, -8.0f}, 100.0f, clearSyncPending);
        // Live round: own minus opponent score on a -200..+200 bar.
        auto const preparing = match.state == "ROUND_PREPARE" || match.state == "ROUND_ENTERING";
        if (!preparing && match.deathmatchSequence <= 0 && match.state.starts_with("ROUND")) {
            auto const ownA = soloOwnA(match);
            auto const diff = (ownA ? match.scoreA - match.scoreB : match.scoreB - match.scoreA);
            constexpr float half = 84.0f;
            auto const reach = static_cast<float>(std::clamp(diff, -200.0, 200.0) / 200.0) * half;
            auto* caption = makeLabel(fmt::format("{}  {}{}", tr("score-diff", "SCORE DIFF"), diff >= 0 ? "+" : "", formatScore(diff, false)), 0.17f,
                {0.0f, -59.0f}, diff >= 0 ? kAccent : kRed, "black");
            m_root->addChild(caption, 6);
            auto* track = CCLayerColor::create({32, 35, 46, 255}, half * 2.0f, 5.0f); track->setPosition({-half, -68.0f}); m_root->addChild(track, 5);
            auto* fill = CCLayerColor::create(diff >= 0 ? ccc4(kAccent.r, kAccent.g, kAccent.b, 255) : ccc4(kRed.r, kRed.g, kRed.b, 255), std::abs(reach), 5.0f);
            fill->setPosition({reach >= 0 ? 0.0f : reach, -68.0f}); m_root->addChild(fill, 6);
            auto* tick = CCLayerColor::create({244, 245, 247, 255}, 1.0f, 9.0f); tick->setPosition({-0.5f, -70.0f}); m_root->addChild(tick, 7);
        }
        constexpr float barWidth = 168.0f;
        auto* back = CCLayerColor::create({16, 17, 22, 245}); back->setContentSize({barWidth, 7.0f});
        back->setPosition({-100.0f, -36.0f}); m_root->addChild(back, 4);
        m_prepareLoadFill = CCLayerColor::create({kAccent.r, kAccent.g, kAccent.b, 245});
        m_prepareLoadFill->setContentSize({barWidth * m_prepareLoadVisual, 7.0f}); m_prepareLoadFill->setPosition(back->getPosition());
        m_prepareLoadWidth = barWidth; m_root->addChild(m_prepareLoadFill, 5);
        m_prepareLoadPercentLabel = makeLabel(fmt::format("{}%", static_cast<int>(std::round(m_prepareLoadVisual * 100.0f))), 0.23f,
            {100.0f, -32.5f}, ccc3(235, 242, 255), "black");
        m_prepareLoadPercentLabel->setAnchorPoint({1, 0.5f}); m_root->addChild(m_prepareLoadPercentLabel, 6);
        m_prepareLoadStageLabel = makeLabel(preparationStageText(), 0.18f, {-100.0f, -50.0f}, m_levelLookupError.empty() ? kAccent : kGold, "semibold");
        m_prepareLoadStageLabel->setAnchorPoint({0, 0.5f}); corum::ranked::ui::fitMenuLabelWidth(m_prepareLoadStageLabel, 200.0f, 0.18f, 0.12f);
        m_root->addChild(m_prepareLoadStageLabel, 6);
        // Three equal option buttons across the column.
        auto* level = findPlayableMap();
        auto option = [&](std::string const& caption, float x, SEL_MenuHandler selector, bool on) {
            auto* face = corum::ranked::ui::surface({64.0f, 22.0f}, {}, on ? kAccent : kSteel, corum::ranked::ui::SurfaceRaised, 255, true);
            auto* label = makeLabel(caption, 0.2f, {32.0f, 11.0f}, corum::ranked::ui::Text, "extrabold");
            corum::ranked::ui::fitMenuLabelWidth(label, 56.0f, 0.2f, 0.12f); face->addChild(label, 5);
            auto* item = CCMenuItemSpriteExtra::create(face, this, selector); item->setPosition({x, -84.0f}); m_menu->addChild(item);
            return item;
        };
        auto* ldm = option(fmt::format("{}: {}", tr("low-detail", "LDM"), level && level->m_lowDetailModeToggled ? "ON" : "OFF"), -70.0f,
            menu_selector(CorumRankedLayer::onLowDetail), level && level->m_lowDetailModeToggled);
        auto* shake = option(fmt::format("{}: {}", tr("disable-shake", "DISABLE SHAKE"), level && level->m_disableShakeToggled ? "ON" : "OFF"), 0.0f,
            menu_selector(CorumRankedLayer::onDisableShake), level && level->m_disableShakeToggled);
        ldm->setID("ranked-round-ldm"_spr); shake->setID("ranked-round-disable-shake"_spr);
        // Maps without a Low Detail Mode have nothing to toggle.
        auto const ldmAvailable = level && level->m_lowDetailMode;
        ldm->setEnabled(ldmAvailable); shake->setEnabled(level != nullptr);
        if (!ldmAvailable) corum::ranked::ui::grayButtonVisual(ldm->getNormalImage());
        m_jukeboxButton = option("Jukebox", 70.0f, menu_selector(CorumRankedLayer::onJukebox), false);
        m_jukeboxButton->setID("ranked-jukebox-button"_spr);
        auto const jukeboxLoaded = corum::ranked::ui::rankedJukeboxLoaded();
        if (!jukeboxLoaded) corum::ranked::ui::grayButtonVisual(m_jukeboxButton->getNormalImage());
        m_jukeboxButton->setEnabled(!jukeboxLoaded || findPlayableMap() != nullptr);
    }

    std::vector<corum::ranked::ui::SoloIntroCard> soloIntroCards(MatchView const& match, int& revealSlot, bool& shift) const {
        using corum::ranked::ui::SoloIntroCard;
        auto const ownA = soloOwnA(match);
        auto const outcome = [&](std::string const& result, SoloIntroCard& card) {
            if (result.empty()) return;
            auto const draw = result == "DRAW";
            auto const win = !draw && (result == "A") == ownA;
            card.result = draw ? "DRAW" : win ? "WIN" : "LOSS";
            card.accent = card.resultColor = draw ? corum::ranked::ui::TextMuted : win ? kGold : kSideRed;
        };
        std::vector<SoloIntroCard> past;
        for (auto const& round : match.rounds) {
            if (match.deathmatchSequence <= 0 && round.roundNumber >= match.roundNumber) continue;
            SoloIntroCard card; card.label = fmt::format("ROUND {}", round.roundNumber); card.map = round.mapTitle;
            card.detail = upper(round.difficulty); card.detailColor = difficultyColor(round.difficulty);
            card.you = formatScore(ownA ? round.scoreA : round.scoreB, false); card.opp = formatScore(ownA ? round.scoreB : round.scoreA, false);
            outcome(round.result, card); past.push_back(card);
        }
        for (auto const& dm : match.deathmatches) {
            if (dm.sequence >= match.deathmatchSequence) continue;
            SoloIntroCard card; card.label = fmt::format("DEATH MATCH {}", dm.sequence); card.map = dm.mapTitle;
            card.detail = upper(dm.difficulty); card.detailColor = difficultyColor(dm.difficulty);
            card.you = formatScore(ownA ? dm.scoreA : dm.scoreB, true); card.opp = formatScore(ownA ? dm.scoreB : dm.scoreA, true);
            outcome(dm.winnerSide, card); past.push_back(card);
        }
        shift = match.deathmatchSequence > 0 && past.size() >= 3;
        if (shift) { revealSlot = 2; return std::vector<SoloIntroCard>(past.end() - 3, past.end()); }
        // Single game: the only round is revealed centre stage; a draw's death
        // match keeps the decided cards on its left.
        if (match.singleGame) {
            revealSlot = past.size() >= 2 ? 2 : 1;
            return past;
        }
        // Normal BO3: the revealed round sits in its own slot; later rounds are locked.
        revealSlot = std::clamp(match.roundNumber - 1, 0, 2);
        std::vector<SoloIntroCard> cards;
        for (int slot = 0; slot < 3; ++slot) {
            if (slot == revealSlot) continue;
            if (slot < revealSlot && static_cast<std::size_t>(slot) < past.size()) { cards.push_back(past[static_cast<std::size_t>(slot)]); continue; }
            SoloIntroCard locked; locked.label = fmt::format("ROUND {}", slot + 1); locked.locked = true; cards.push_back(locked);
        }
        return cards;
    }

    bool soloHoldActive() const { return m_soloIntroRunning || SteadyClock::now() < m_soloHoldUntil; }

    void onSpectateLive(CCObject*) {
        auto const& match = RankedRuntime::get().view().match;
        if (m_enteringLevel || !RankedRuntime::get().canEnterCurrentLevelAsSpectator()) return;
        m_spectateRequestKey = fmt::format("{}:{}:{}", match.matchId, match.roundNumber, match.deathmatchSequence);
        maybeEnterLevel();
    }

    void renderSpectatorWait(MatchView const& match) {
        auto const layout = soloLayout();
        auto const& size = layout.win;
        auto const ownA = soloOwnA(match);
        auto const ownScore = ownA ? match.scoreA : match.scoreB;
        auto const opponentScore = ownA ? match.scoreB : match.scoreA;
        auto const opponentAttemptActive = match.spectatorCurrentProgress.has_value();
        auto const progress = match.spectatorCurrentProgress.value_or(0);
        auto const opponent = match.spectatorOpponentName.empty() ? match.opponentName : match.spectatorOpponentName;
        addSoloHeader(match, soloRoundTitle(match), ownA ? match.roundWinsA : match.roundWinsB, ownA ? match.roundWinsB : match.roundWinsA);
        // Clock: the last-attempt deadline when it applies, otherwise LIVE.
        m_root->addChild(makeNeonPanel({67.0f, 47.0f}, {size.width - 43.5f, size.height - 35.0f}, kGold, {22, 24, 31}, 248), 2);
        auto const lastAttempt = match.state == "LAST_ATTEMPT_WINDOW";
        m_root->addChild(makeLabel(lastAttempt ? "LAST ATTEMPT" : "OPPONENT", 0.17f, {size.width - 43.5f, size.height - 23.0f}, corum::ranked::ui::TextMuted, "semibold"), 5);
        auto const seconds = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
        m_root->addChild(makeLabel(lastAttempt ? fmt::format("{}:{:02d}", seconds / 60, seconds % 60) : std::string("LIVE"), 0.5f,
            {size.width - 43.5f, size.height - 42.0f}, kGold, "black"), 5);

        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        auto const xs = std::array<float, 3>{size.width / 2.0f - layout.sideOffset, size.width / 2.0f, size.width / 2.0f + layout.sideOffset};
        for (int i = 0; i < 3; ++i) {
            auto* column = makeSoloColumn({xs[i], layout.centerY}, layout.scale);
            m_root = column; m_menu = typeinfo_cast<CCMenu*>(column->getChildByID("ranked-solo-column-menu"_spr));
            if (i == 1) {
                m_root->addChild(makeNeonPanel({225.0f, 210.0f}, {0, 0}, kSteel, kDeepPanel2, 236), 1);
                auto* title = makeLabel(tr("attempt-finished", "YOUR ATTEMPT IS FINISHED"), 0.40f, {0, 86.0f}, kGold, "black");
                corum::ranked::ui::fitMenuLabelWidth(title, 205.0f, 0.40f, 0.24f); m_root->addChild(title, 5);
                m_spectatorStateLabel = makeLabel(opponentAttemptActive ? tr("live-progress", "LIVE PROGRESS") : tr("waiting-start", "WAITING TO START"),
                    0.23f, {0, 66.0f}, kAccent, "semibold");
                m_root->addChild(m_spectatorStateLabel, 5);
                m_spectatorProgressLabel = makeLabel(opponentAttemptActive ? fmt::format("{}%", progress) : std::string("--"), 1.45f, {0, 22.0f}, kGold, "black");
                corum::ranked::ui::fitMenuLabelWidth(m_spectatorProgressLabel, 200.0f, 1.45f, 0.7f); m_root->addChild(m_spectatorProgressLabel, 5);
                constexpr float barWidth = 190.0f;
                auto* back = CCLayerColor::create({16, 17, 22, 245}); back->setContentSize({barWidth, 7.0f});
                back->setPosition({-95.0f, -25.0f}); m_root->addChild(back, 4);
                m_spectatorBarFill = CCLayerColor::create({kGold.r, kGold.g, kGold.b, 245});
                m_spectatorBarFill->setContentSize({barWidth * std::clamp(progress / 100.0f, 0.0f, 1.0f), 7.0f});
                m_spectatorBarFill->setPosition(back->getPosition()); m_spectatorBarWidth = barWidth; m_root->addChild(m_spectatorBarFill, 5);
                auto* scorePlate = makeTextPlate(scoreComparison(ownScore, opponentScore, match.deathmatchSequence > 0), {205.0f, 24.0f}, {0, -56.0f},
                    kSteel, 0.22f, ccc3(218, 228, 244), "semibold", &m_spectatorScoreLabel);
                m_root->addChild(scorePlate, 3);
                // Global live spectating ON: offer it instead of entering automatically.
                if (match.spectatorLiveEnabled) {
                    auto* spectate = addButton(tr("spectate-live", "SPECTATE"), {0, -86.0f}, menu_selector(CorumRankedLayer::onSpectateLive), false, 0.42f);
                    spectate->setID("ranked-solo-spectate"_spr);
                    spectate->setEnabled(RankedRuntime::get().canEnterCurrentLevelAsSpectator());
                    m_soloSpectateButton = spectate;
                }
            } else {
                auto const own = i == 0;
                auto const sideA = own == ownA;
                addPlayerCard({0, 13.0f}, sideA ? match.playerAName : match.playerBName, sideA ? match.playerAProfile : match.playerBProfile,
                    own, sideA ? match.playerAAccountId : match.playerBAccountId, nullptr, false, 26.0f);
                auto const text = own ? std::string("ATTEMPTS FINISHED") : opponentAttemptActive ? std::string("LIVE ATTEMPT") : tr("waiting-start", "WAITING TO START");
                auto const color = own ? corum::ranked::ui::TextMuted : opponentAttemptActive ? kSideRed : kAccent;
                m_root->addChild(makeTextPlate(text, {132.0f, 21.0f}, {0, -92.0f}, color, 0.23f, color, "black"), 3);
            }
            m_root = savedRoot; m_menu = savedMenu;
        }
        std::string hintText = tr("round-resolve-hint", "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS");
        if (!opponentAttemptActive && lastAttempt)
            hintText = fmt::format(fmt::runtime(tr("last-attempt-seconds", "OPPONENT HAS {}s TO START THE LAST ATTEMPT")), seconds);
        auto* hint = makeTextPlate(hintText, {std::min(size.width - 40.0f, 300.0f), 22.0f}, {size.width / 2.0f, 18.0f}, kSteel, 0.22f,
            ccc3(195, 205, 224), "semibold", &m_spectatorHintLabel);
        m_root->addChild(hint, 3);
    }

    // Round result: outcome title, both records and the series score rolling
    // up for the round winner (played once per round).
    void renderInterRound(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = soloOwnA(match);
        auto const deathmatch = match.state == "DEATHMATCH_RESULT" || match.deathmatchSequence > 0;
        std::string result; double ownScore = 0, oppScore = 0; int ownClears = 0, oppClears = 0; std::string mapTitle;
        if (deathmatch) {
            for (auto const& dm : match.deathmatches) if (dm.sequence == match.deathmatchSequence) {
                result = dm.winnerSide; ownScore = ownA ? dm.scoreA : dm.scoreB; oppScore = ownA ? dm.scoreB : dm.scoreA; mapTitle = dm.mapTitle;
            }
        } else {
            for (auto const& round : match.rounds) if (round.roundNumber == match.roundNumber) {
                result = round.result; ownScore = ownA ? round.scoreA : round.scoreB; oppScore = ownA ? round.scoreB : round.scoreA;
                ownClears = ownA ? round.clearsA : round.clearsB; oppClears = ownA ? round.clearsB : round.clearsA; mapTitle = round.mapTitle;
            }
        }
        auto const known = !result.empty();
        auto const draw = result == "DRAW";
        auto const ownWin = known && !draw && (result == "A") == ownA;
        auto const ownWins = ownA ? match.roundWinsA : match.roundWinsB;
        auto const oppWins = ownA ? match.roundWinsB : match.roundWinsA;
        auto const animKey = fmt::format("{}:{}:{}:{}", match.matchId, match.roundNumber, match.deathmatchSequence, result);
        auto const animate = known && !draw && !deathmatch && m_soloScoreAnimated != animKey && (ownWin ? ownWins : oppWins) > 0;
        if (known) m_soloScoreAnimated = animKey;
        // M5 (round result), from the board: a lime band with the round line
        // and the 104 px result, both players around the big series score,
        // the round chips, and the next-step line with a thin bar.
        auto const shownOwn = ownWins - (animate && ownWin ? 1 : 0), shownOpp = oppWins - (animate && !ownWin ? 1 : 0);
        m_soloPips.clear();
        auto const bandTop = size.height - px(96.0f), bandH = px(170.0f);
        auto const bandColor = !known || draw ? ccc3(43, 47, 60) : ownWin ? corum::ranked::ui::AccentFill : kSideRed;
        auto const bandInk = ownWin ? corum::ranked::ui::AccentInk : corum::ranked::ui::Text;
        auto* band = CCLayerColor::create({bandColor.r, bandColor.g, bandColor.b, 255}, size.width + 40.0f, bandH);
        band->setPosition({-20.0f, bandTop - bandH}); band->setSkewY(3.0f); m_root->addChild(band, 1);
        auto const roundLine = fmt::format("{} · {}", soloRoundTitle(match), !known ? tr("settling", "SETTLING...") : draw ? tr("round-draw", "ROUND DRAW")
            : ownWin ? tr("round-won-mine", "MY ROUND") : tr("round-lost-mine", "ROUND LOST"));
        m_root->addChild(makeLabel(roundLine, fontScale(16.0f), {size.width / 2.0f, bandTop - px(22.0f) - 8.0f}, bandInk, "black"), 6);
        auto const titleText = !known ? std::string(deathmatch ? "DEATH MATCH" : "ROUND RESULT") : draw ? std::string("ROUND DRAW") : ownWin ? std::string("ROUND WIN") : std::string("ROUND LOST");
        auto* title = makeLabel(titleText, fontScale(104.0f), {size.width / 2.0f, bandTop - px(22.0f) - 44.0f}, bandInk, "black");
        corum::ranked::ui::fitMenuLabelWidth(title, size.width - 60.0f, fontScale(104.0f), 0.5f);
        m_root->addChild(title, 6);
        // Players around the series score panel.
        auto const rowY = bandTop - bandH - px(46.0f) - 10.0f;
        constexpr float panelW = 96.0f, panelH = 44.0f;
        auto* scorePanel = CCLayerColor::create({23, 25, 34, 255}, panelW, panelH); scorePanel->setPosition({size.width / 2.0f - panelW / 2.0f, rowY - panelH / 2.0f}); m_root->addChild(scorePanel, 2);
        m_soloOwnScore = makeLabel(std::to_string(shownOwn), fontScale(56.0f), {size.width / 2.0f - 22.0f, rowY}, corum::ranked::ui::Accent, "black"); m_root->addChild(m_soloOwnScore, 5);
        m_root->addChild(makeLabel(":", fontScale(24.0f), {size.width / 2.0f, rowY}, corum::ranked::ui::TextMuted, "black"), 5);
        m_soloOppScore = makeLabel(std::to_string(shownOpp), fontScale(56.0f), {size.width / 2.0f + 22.0f, rowY}, corum::ranked::ui::Text, "black"); m_root->addChild(m_soloOppScore, 5);
        m_soloDigitScale = fontScale(56.0f);
        for (int side = 0; side < 2; ++side) {
            auto const own = side == 0;
            auto const x = own ? size.width / 2.0f - panelW / 2.0f - px(28.0f) : size.width / 2.0f + panelW / 2.0f + px(28.0f);
            auto const anchor = own ? CCPoint{1.0f, 0.5f} : CCPoint{0.0f, 0.5f};
            auto* name = makeLabel(own == ownA ? match.playerAName : match.playerBName, fontScale(24.0f), {x, rowY + 17.0f}, corum::ranked::ui::Text, "black", anchor);
            corum::ranked::ui::fitMenuLabelWidth(name, 120.0f, fontScale(24.0f), 0.15f); m_root->addChild(name, 5);
            auto* score = makeLabel(fmt::format("{}{}", formatScore(own ? ownScore : oppScore, deathmatch), deathmatch ? "" : tr("points-short", "PTS")), fontScale(34.0f),
                {x, rowY}, own ? corum::ranked::ui::Accent : corum::ranked::ui::TextMuted, "black", anchor);
            corum::ranked::ui::fitMenuLabelWidth(score, 120.0f, fontScale(34.0f), 0.2f); m_root->addChild(score, 5);
            if (!deathmatch) m_root->addChild(makeLabel(fmt::format("{} {} / 3", tr("cleared", "CLEARED"), own ? ownClears : oppClears), fontScale(13.0f), {x, rowY - 16.0f},
                corum::ranked::ui::TextMuted, "semibold", anchor), 5);
        }
        // Round chips: R1..R3 (and DM) with W / D / L.
        {
            std::vector<std::pair<std::string, std::string>> chips;
            for (int number = 1; number <= (match.singleGame ? 1 : 3); ++number) {
                std::string outcome;
                for (auto const& round : match.rounds) if (round.roundNumber == number) outcome = round.result;
                chips.push_back({fmt::format("R{}", number), outcome});
            }
            for (auto const& dm : match.deathmatches) chips.push_back({"DM", dm.winnerSide});
            float total = 0.0f;
            std::vector<std::string> texts;
            for (auto const& [label, outcome] : chips) {
                auto const played = !outcome.empty();
                auto const won = played && outcome != "DRAW" && (outcome == "A") == ownA;
                texts.push_back(!played ? label : fmt::format("{} {}", label, outcome == "DRAW" ? tr("draw-short", "D") : won ? tr("win-short", "W") : tr("lose-short", "L")));
                total += makeLabel(texts.back(), fontScale(15.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + px(24.0f) + px(10.0f);
            }
            float chipX = size.width / 2.0f - total / 2.0f;
            auto const chipY = rowY - panelH / 2.0f - px(22.0f) - 8.0f;
            for (std::size_t index = 0; index < chips.size(); ++index) {
                auto const& outcome = chips[index].second;
                auto const played = !outcome.empty();
                auto const won = played && outcome != "DRAW" && (outcome == "A") == ownA;
                addChip(m_root, texts[index], {chipX, chipY}, !played ? ccc3(32, 35, 46) : outcome == "DRAW" ? ccc3(43, 47, 60) : won ? corum::ranked::ui::AccentFill : kRed,
                    won || (played && outcome != "DRAW") ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, 15.0f);
                chipX += makeLabel(texts[index], fontScale(15.0f), {}, corum::ranked::ui::Text, "black")->getScaledContentSize().width + px(24.0f) + px(10.0f);
            }
            // Next step and a thin bar (420 px).
            auto const lineY = chipY - px(22.0f) - 12.0f;
            m_root->addChild(makeLabel(tr("next-map-preparing", "NEXT MAP PREPARING..."), fontScale(18.0f), {size.width / 2.0f, lineY}, corum::ranked::ui::Text, "extrabold"), 5);
            addSkewBar(m_root, {size.width / 2.0f - px(210.0f), lineY - 12.0f}, px(420.0f), px(6.0f), 0.7f, corum::ranked::ui::Accent);
        }
        if (!animate) return;
        // Title pops in, then the winner's series digit rolls up.
        title->setScale(title->getScale() * 1.35f); title->setOpacity(0);
        auto const titleScale = title->getScale() / 1.35f;
        title->runAction(CCSequence::create(CCDelayTime::create(0.25f), CCSpawn::create(CCFadeIn::create(0.26f),
            CCEaseBackOut::create(CCScaleTo::create(0.42f, titleScale)), nullptr), nullptr));
        auto* digit = ownWin ? m_soloOwnScore : m_soloOppScore;
        auto const wins = ownWin ? ownWins : oppWins;
        if (digit) {
            auto const base = digit->getPosition();
            auto* incoming = makeLabel(std::to_string(wins), m_soloDigitScale, {base.x, base.y - 12.0f}, ownWin ? corum::ranked::ui::Accent : corum::ranked::ui::Text, "black");
            incoming->setOpacity(0); m_root->addChild(incoming, 6);
            digit->runAction(CCSequence::create(CCDelayTime::create(0.75f), CCSpawn::create(CCMoveBy::create(0.22f, {0, 12.0f}), CCFadeOut::create(0.2f), nullptr), nullptr));
            incoming->runAction(CCSequence::create(CCDelayTime::create(0.78f), CCSpawn::create(CCFadeIn::create(0.18f),
                CCEaseBackOut::create(CCMoveTo::create(0.52f, base)), CCSequence::create(CCScaleTo::create(0.2f, m_soloDigitScale * 1.35f),
                CCEaseBackOut::create(CCScaleTo::create(0.3f, m_soloDigitScale)), nullptr), nullptr), nullptr));
        }
        m_soloHoldUntil = SteadyClock::now() + std::chrono::milliseconds(1700);
    }


    void addClearChecks(int clears, CCPoint base, bool rightAligned = false, CCNode* parent = nullptr) {
        auto const count = std::clamp(clears, 0, 3);
        for (int i = 0; i < count; ++i) {
            auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            check->setScale(0.26f);
            check->setColor(kGreen);
            check->setPosition({base.x + (rightAligned ? -1.0f : 1.0f) * i * 13.0f, base.y});
            (parent ? parent : m_root)->addChild(check, 5);
        }
    }

    void animateRankedScoreChange(
        float centerX,
        float finalY,
        std::optional<int> delta,
        CCLabelBMFont* finalScore,
        RankedProfileView const& before
    ) {
        if (!delta || !finalScore || !before.visibleRankedScore) return;

        // Show the authoritative pre-match LP first. The delta rises from below,
        // overlaps it, and the post-match value fades in at the same position.
        auto* previousScore = makeLabel(
            fmt::format("{} - {} LP", corum::ranked::rankTierDisplay(upper(before.displayedTier)), *before.visibleRankedScore),
            0.27f,
            {centerX, finalY},
            ccc3(240, 244, 250),
            "goldFont.fnt"
        );
        corum::ranked::ui::fitMenuLabelWidth(previousScore, 118.0f, 0.27f, 0.16f);
        m_root->addChild(previousScore, 9);
        previousScore->runAction(CCSequence::create(
            CCDelayTime::create(0.56f),
            CCFadeOut::create(0.22f),
            nullptr
        ));
        finalScore->setOpacity(0);
        finalScore->runAction(CCSequence::create(
            CCDelayTime::create(0.72f),
            CCFadeIn::create(0.28f),
            nullptr
        ));

        auto* deltaLabel = makeLabel(
            fmt::format("{:+d} LP", *delta),
            0.28f,
            {centerX, 58.0f},
            *delta >= 0 ? kGreen : kRed,
            "goldFont.fnt"
        );
        deltaLabel->setOpacity(0);
        m_root->addChild(deltaLabel, 9);
        deltaLabel->runAction(CCSequence::create(
            CCFadeIn::create(0.16f),
            CCEaseSineOut::create(CCMoveTo::create(0.56f, {centerX, finalY})),
            CCFadeOut::create(0.24f),
            nullptr
        ));
    }

    void animatePromotionTrigger(
        float centerX,
        float finalY,
        corum::ranked::RatingEventView const& event,
        RankedProfileView const& before,
        std::optional<int> delta
    ) {
        if (!event.promotionStarted || !before.visibleRankedScore) return;

        // The promotion marks are the final state, but keep them covered while
        // the previous LP and its gain perform the same overlap transition as a
        // normal rating update. Fading the cover reveals `-----` naturally.
        auto* marksCover = CCLayerColor::create({22, 24, 31, 255});
        marksCover->setContentSize({124.0f, 17.0f});
        marksCover->setPosition({centerX - 62.0f, finalY - 8.5f});
        m_root->addChild(marksCover, 9);
        marksCover->runAction(CCSequence::create(
            CCDelayTime::create(0.56f),
            CCFadeOut::create(0.28f),
            nullptr
        ));

        auto* prior = makeLabel(
            fmt::format("{} - {} LP", corum::ranked::rankTierDisplay(upper(before.displayedTier)), *before.visibleRankedScore),
            0.27f,
            {centerX, finalY},
            ccc3(240, 244, 250),
            "goldFont.fnt"
        );
        corum::ranked::ui::fitMenuLabelWidth(prior, 118.0f, 0.27f, 0.16f);
        m_root->addChild(prior, 10);
        prior->runAction(CCSequence::create(
            CCDelayTime::create(0.56f),
            CCFadeOut::create(0.22f),
            nullptr
        ));

        if (delta) {
            auto* deltaLabel = makeLabel(
                fmt::format("{:+d} LP", *delta),
                0.28f,
                {centerX, 58.0f},
                *delta >= 0 ? kGreen : kRed,
                "goldFont.fnt"
            );
            deltaLabel->setOpacity(0);
            m_root->addChild(deltaLabel, 11);
            deltaLabel->runAction(CCSequence::create(
                CCFadeIn::create(0.16f),
                CCEaseSineOut::create(CCMoveTo::create(0.56f, {centerX, finalY})),
                CCFadeOut::create(0.24f),
                nullptr
            ));
        }
    }

    bool playResultRankTransition(
        corum::ranked::RatingEventView const& event,
        RankedProfileView const& before,
        RankedProfileView const& after,
        std::function<void()> afterReveal = {}
    ) {
        auto const transition = authoritativeTransitionType(event, before, after);
        if (!transition || m_rankTransitionLayer) return false;

        auto const weak = WeakRef<CorumRankedLayer>(this);
        auto* transitionLayer = RankTransitionLayer::create(
            before.displayedTier,
            after.displayedTier,
            *transition,
            ratingEventText(event).first,
            tr("tap-to-skip", "TAP TO SKIP"),
            tr("continue", "CONTINUE"),
            [weak, afterReveal = std::move(afterReveal)] {
                if (auto layer = weak.lock()) {
                    layer->m_rankTransitionLayer = nullptr;
                    if (layer->isRunning()) {
                        if (afterReveal) afterReveal();
                        else layer->restoreResultInput();
                    }
                }
            }, *transition != RankTransitionType::Placement
        );
        if (!transitionLayer) return false;

        m_rankTransitionLayer = transitionLayer;
        if (m_menu) {
            corum::ranked::ui::cancelMenuTouch(m_menu);
            m_menu->setTouchEnabled(false);
        }
        setKeypadEnabled(false);
        addChild(transitionLayer, 1000);
        return true;
    }

    // A direct render() records its key as rendered so the next refresh does
    // not fade-rebuild the same screen (which restarted/cut one-shot motions).
    void adoptCurrentPresentation() {
        m_renderedUiKey = presentationKey(); m_pendingUiKey.clear(); m_stableKeyCandidate.clear();
        if (m_uiFadingOut && m_fadeRoot) { m_fadeRoot->stopAllActions(); m_fadeRoot->setOpacity(255); }
        m_uiFadingOut = false;
    }

    void restoreResultInput() {
        auto const weak = WeakRef<CorumRankedLayer>(this);
        queueInMainThread([weak] {
            if (auto layer = weak.lock(); layer && layer->isRunning() && !layer->m_rankTransitionLayer &&
                !layer->m_resultScoreLayer && !layer->m_resultScorePending) {
                layer->setKeypadEnabled(true);
                if (layer->m_menu) {
                    corum::ranked::ui::cancelMenuTouch(layer->m_menu); layer->m_menu->setTouchEnabled(true);
                }
                layer->updateDynamicUi(); layer->playNextAchievementToast();
            }
        });
    }

    std::optional<corum::ranked::ScoreAnimationInput> resultScoreInput(corum::ranked::MatchView const& match) const {
        auto const ownA = match.side == "A";
        auto const& before = ownA ? match.profileBeforeA : match.profileBeforeB;
        auto const& after = ownA ? match.profileAfterA : match.profileAfterB;
        if (match.placement || match.state != "MATCH_RESULT" || !match.scorePresentation.available ||
            before.displayedTier == "UNRANKED" || after.displayedTier == "UNRANKED" ||
            !before.visibleRankedScore || !after.visibleRankedScore ||
            before.placementGames < before.placementGamesRequired) return std::nullopt;
        auto const& event = ownA ? match.ratingEventA : match.ratingEventB;
        auto const& player = ownA ? match.scorePresentation.playerA : match.scorePresentation.playerB;
        corum::ranked::ScoreAnimationInput input;
        input.matchId = match.matchId; input.beforeTier = before.displayedTier; input.afterTier = after.displayedTier;
        input.before = *before.visibleRankedScore; input.after = *after.visibleRankedScore;
        input.won = match.winnerSide == match.side; input.promotionStarted = event.promotionStarted;
        input.promotionInProgress = before.promotion.active; input.promoted = event.promotionSucceeded;
        input.promotionFailed = event.promotionFailed; input.demoted = event.demoted;
        input.demotionWarning = after.demotionWarning; input.previousMarks = static_cast<int>(before.promotion.results.size());
        input.bands = match.scorePresentation.bands; input.standingBefore = player.before;
        input.standingAfter = player.after; input.promotion = player.promotion; input.goldTimeline = player.goldTimeline;
        if(!player.before.displayTier.empty())input.beforeTier=player.before.displayTier;
        if(!player.after.displayTier.empty())input.afterTier=player.after.displayTier;
        input.promoted=input.promoted || corum::ranked::rankTierOrder(input.afterTier)>corum::ranked::rankTierOrder(input.beforeTier);
        input.demoted=input.demoted || corum::ranked::rankTierOrder(input.afterTier)<corum::ranked::rankTierOrder(input.beforeTier);
        return input;
    }

    void queueResultScore(corum::ranked::ScoreAnimationInput input) {
        m_resultScorePending = true; setKeypadEnabled(false);
        if (m_menu) { corum::ranked::ui::cancelMenuTouch(m_menu); m_menu->setTouchEnabled(false); }
        auto const weak = WeakRef<CorumRankedLayer>(this);
        // Add the next keypad delegate outside the current Esc dispatch.
        queueInMainThread([weak,input = std::move(input)]() mutable {
            auto layer = weak.lock(); if (!layer) return;
            auto const& match = RankedRuntime::get().view().match;
            if (!layer->isRunning() || match.matchId != input.matchId || match.state != "MATCH_RESULT") {
                layer->m_resultScorePending = false; layer->restoreResultInput(); return;
            }
            auto const opacity = layer->m_fadeRoot->getOpacity(); layer->m_fadeRoot->setOpacity(255);
            auto* score = corum::ranked::ui::ResultScoreLayer::create(std::move(input),layer.data(),[weak] {
                if (auto owner = weak.lock()) {
                    owner->m_resultScoreLayer = nullptr;
                    // Rebuild once, already recorded as the rendered key, so the next
                    // refresh does not fade-rebuild again and cut the row/stamp motion.
                    if (owner->isRunning()) {
                        owner->adoptCurrentPresentation();
                        owner->render(); owner->restoreResultInput();
                    }
                }
            });
            layer->m_fadeRoot->setOpacity(opacity);
            layer->m_resultScorePending = false;
            if (!score) { layer->restoreResultInput(); return; }
            layer->m_resultScoreLayer = score; layer->addChild(score,1000); layer->updateDynamicUi();
        });
    }

    void beginResultScore(corum::ranked::MatchView const& match, corum::ranked::ScoreAnimationInput input) {
        if (!m_resultPresentationGate.begin(match.matchId)) return;
        auto const ownA = match.side == "A";
        auto const weak = WeakRef<CorumRankedLayer>(this);
        auto start = [weak,input] { if (auto layer = weak.lock()) layer->queueResultScore(input); };
        auto const started = playResultRankTransition(ownA ? match.ratingEventA : match.ratingEventB,
            ownA ? match.profileBeforeA : match.profileBeforeB,
            ownA ? match.profileAfterA : match.profileAfterB,start);
        if (started) m_rankTransitionPlayedKey = match.matchId;
        else start();
    }

    void playNextAchievementToast() {
        // Result rank reveals own the foreground first. Keep notices queued until
        // the settled Match/Placement Result UI is actually visible.
        if (m_achievementToastPlaying || !m_achievementToastRoot || m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending) return;
        auto const key = m_achievementToastQueue.pop();
        if (!key) return;
        auto const found = m_achievementToasts.find(*key);
        if (found == m_achievementToasts.end()) {
            playNextAchievementToast();
            return;
        }
        auto const notice = found->second;
        m_achievementToasts.erase(found);
        m_achievementToastPlaying = true;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* group = CCNode::create();
        auto const toastX = std::min(size.width / 2.0f, 12.0f + 165.0f);
        group->setPosition({-180.0f, 78.0f});
        group->setID("ranked-achievement-result-toast"_spr);
        m_achievementToastRoot->addChild(group, 1);

        auto const noticeTier=notice.level.value_or(corum::ranked::achievementRarityTier(notice.rarity));
        auto const accent=corum::ranked::ui::achievementColor(noticeTier);
        auto* panel = makeNeonPanel({330.0f, 67.0f}, CCPointZero, accent, kDeepPanel, 255);
        group->addChild(panel, 0);
        auto const heading = notice.type == "ONE_SHOT"
            ? tr("achievement-unlocked", "ACHIEVEMENT UNLOCKED")
            : notice.maxed
                ? tr("achievement-maxed", "ACHIEVEMENT MAXED")
                : tr("achievement-upgraded", "ACHIEVEMENT UPGRADED");
        auto* headingLabel = makeLabel(heading, 0.22f, {0.0f, 19.0f}, accent, "semibold");
        group->addChild(headingLabel, 2);
        auto const baseName = corum::ranked::i18n::text("achievement."+notice.achievementId+".name",notice.nameEn);
        auto const displayName = notice.displayLabel.empty()
            ? baseName
            : fmt::format("{} - {}", baseName, notice.displayLabel);
        auto* name = makeLabel(displayName, 0.34f, {0.0f, 0.0f}, corum::ranked::ui::Text, "black");
        corum::ranked::ui::fitMenuLabelWidth(name, 290.0f, 0.34f, 0.22f);
        group->addChild(name, 2);
        corum::ranked::ui::styleAchievementText(name,noticeTier);
        auto* reward = makeLabel(
            fmt::format("+{} AP {}", notice.rewardAp, tr("available", "AVAILABLE")),
            0.20f,
            {0.0f, -20.0f},
            kGold,
            "black"
        );
        group->addChild(reward, 2);
        group->runAction(CCSequence::create(
            CCDelayTime::create(0.35f),
            CCEaseBackOut::create(CCMoveTo::create(0.34f, {toastX, 78.0f})),
            CCDelayTime::create(2.15f),
            CCEaseSineIn::create(CCMoveTo::create(0.26f, {-180.0f, 78.0f})),
            CallFuncExt::create([this, group] {
                group->removeFromParentAndCleanup(true);
                m_achievementToastPlaying = false;
                playNextAchievementToast();
            }),
            nullptr
        ));
    }

    void enqueueAchievementToasts(
        std::string const& context,
        std::vector<corum::ranked::AchievementUnlockNoticeView> const& notices
    ) {
        for (auto const& notice : notices) {
            auto const key = fmt::format(
                "{}:{}:{}",
                context,
                notice.achievementId,
                notice.level.value_or(0)
            );
            if (!m_achievementToastQueue.enqueue(key)) continue;
            m_achievementToasts.emplace(key, notice);
        }
        playNextAchievementToast();
    }

    void renderResult(RuntimeView const& view) {
        auto const& match = view.match;
        auto const scoreInput = resultScoreInput(match);
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* close = corum::ranked::ui::surface({28, 28}, {}, kSteel, kDeepPanel2, 250, false);
        auto* closeMark = corum::ranked::ui::cross(15.0f, 2.5f, corum::ranked::ui::Text);
        closeMark->setPosition({14.0f, 14.0f}); close->addChild(closeMark, 5);
        auto* closeButton = CCMenuItemSpriteExtra::create(close, this, menu_selector(CorumRankedLayer::onResultClose));
        closeButton->setPosition({24.0f, size.height - 24.0f});
        m_menu->addChild(closeButton);

        if (match.state == "CANCELLED") {
            auto* title = makeLabel(tr("match-canceled", "MATCH CANCELED"), 0.70f, {size.width / 2.0f, size.height / 2.0f + 58.0f}, kRed, "goldFont.fnt");
            m_root->addChild(title, 3);
            auto* reason = makeLabel(
                match.cancellationReason.empty()
                    ? tr("invalid-no-rating", "MATCH INVALID - NO RATING CHANGES")
                    : match.cancellationReason,
                0.28f,
                {size.width / 2.0f, size.height / 2.0f + 12.0f},
                ccc3(220, 220, 225)
            );
            corum::ranked::ui::fitMenuLabelWidth(reason, size.width - 100.0f, 0.28f, 0.18f);
            m_root->addChild(reason, 3);
            addButton(tr("queue-again", "QUEUE AGAIN"), {size.width / 2.0f, size.height / 2.0f - 55.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.66f);
            return;
        }

        auto const ownA = match.side == "A";
        // M6 board: a slanted verdict band across the top (VICTORY / DEFEAT
        // with the series score plate), the round table on the left with the
        // achievement notices below it, the LP panel on the right, and REMATCH /
        // HISTORY / LOBBY along the bottom.
        auto const leftPerfect = match.perfectWinSide == (ownA ? "A" : "B");
        auto const ownWon = match.winnerSide == (ownA ? "A" : "B");
        auto const draw = match.winnerSide.empty();
        auto const bandColor = ownWon ? corum::ranked::ui::AccentFill : draw ? corum::ranked::ui::SurfaceSoft : kSideRed;
        auto const bandInk = ownWon ? corum::ranked::ui::AccentInk : corum::ranked::ui::Text;
        {
            constexpr float solid = 54.0f, slant = 20.0f, step = 2.0f;
            auto* band = CCLayerColor::create({bandColor.r, bandColor.g, bandColor.b, 255}, size.width, solid);
            band->setPosition({0.0f, size.height - solid}); band->setID("ranked-result-verdict-band"_spr); m_root->addChild(band, 1);
            for (float y = 0.0f; y < slant; y += step) {
                auto const t = 1.0f - (y + step / 2.0f) / slant;
                auto* strip = CCLayerColor::create({bandColor.r, bandColor.g, bandColor.b, 255}, size.width * t, step);
                strip->setPosition({size.width * (1.0f - t), size.height - solid - slant + y}); m_root->addChild(strip, 1);
            }
        }
        auto* eyebrow = makeLabel(fmt::format("SOLO RANKED · {}", tr("match-complete", "MATCH COMPLETE")), 0.20f, {48.0f, size.height - 13.0f}, bandInk, "black", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(eyebrow, 200.0f, 0.20f, 0.13f); m_root->addChild(eyebrow, 4);
        auto* verdict = makeLabel(leftPerfect ? std::string("PERFECT WIN") : ownWon ? std::string("VICTORY") : draw ? std::string("DRAW") : std::string("DEFEAT"),
            0.95f, {48.0f, size.height - 37.0f}, bandInk, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(verdict); corum::ranked::ui::fitMenuLabelWidth(verdict, 190.0f, 0.95f, 0.5f);
        verdict->setID("ranked-result-verdict"_spr); m_root->addChild(verdict, 4);
        auto* verdictSub = makeLabel(ownWon ? tr("win", "WIN") : draw ? tr("draw", "DRAW") : tr("lose", "LOSE"), 0.34f,
            {48.0f + verdict->getScaledContentSize().width + 12.0f, size.height - 41.0f}, bandInk, "black", {0.0f, 0.5f});
        m_root->addChild(verdictSub, 4);
        // Series plate: own name/tier, the series score, opponent name/tier.
        {
            constexpr float plateW = 228.0f, plateH = 38.0f;
            auto const plateX = size.width - 14.0f - plateW / 2.0f, plateY = size.height - 30.0f;
            auto* plate = CCLayerColor::create({corum::ranked::ui::Ink.r, corum::ranked::ui::Ink.g, corum::ranked::ui::Ink.b, 255}, plateW, plateH);
            plate->setPosition({plateX - plateW / 2.0f, plateY - plateH / 2.0f}); m_root->addChild(plate, 3);
            auto const side = [&](bool own) {
                auto const sideA = own == ownA;
                auto const& profile = sideA ? match.profileAfterA : match.profileAfterB;
                auto const x = own ? plateX - 34.0f : plateX + 34.0f;
                auto const anchor = own ? CCPoint{1.0f, 0.5f} : CCPoint{0.0f, 0.5f};
                auto* name = makeLabel(sideA ? match.playerAName : match.playerBName, 0.26f, {x, plateY + 6.0f}, corum::ranked::ui::Text, "black", anchor);
                corum::ranked::ui::fitMenuLabelWidth(name, 72.0f, 0.26f, 0.15f); m_root->addChild(name, 4);
                auto* tier = makeLabel(corum::ranked::rankTierDisplay(upper(profile.displayedTier)), 0.18f, {x, plateY - 8.0f}, tierColor(upper(profile.displayedTier)), "black", anchor);
                corum::ranked::ui::fitMenuLabelWidth(tier, 72.0f, 0.18f, 0.12f); m_root->addChild(tier, 4);
            };
            side(true); side(false);
            auto* score = makeLabel(fmt::format("{} : {}", ownA ? match.roundWinsA : match.roundWinsB, ownA ? match.roundWinsB : match.roundWinsA), 0.62f,
                {plateX, plateY}, ownWon ? corum::ranked::ui::Accent : corum::ranked::ui::Text, "black");
            corum::ranked::ui::fitMenuLabelWidth(score, 56.0f, 0.62f, 0.4f); m_root->addChild(score, 4);
        }

        // Left: the round table. Up to 3.5 rows show; longer series scroll.
        constexpr float panelW = 176.0f, rowH = 26.0f;
        auto const tableLeft = 16.0f, tableRight = size.width - 16.0f - panelW - 12.0f, tableW = tableRight - tableLeft;
        auto const headY = size.height - 92.0f;
        auto const colMap = tableLeft + 30.0f, colOwn = tableRight - 98.0f, colOpp = tableRight - 46.0f, colResult = tableRight - 14.0f;
        auto const headCaption = [&](std::string const& text, float x, CCPoint anchor) {
            m_root->addChild(makeLabel(text, 0.16f, {x, headY}, corum::ranked::ui::TextMuted, "semibold", anchor), 4);
        };
        headCaption(tr("round-short", "ROUND"), tableLeft + 4.0f, {0.0f, 0.5f}); headCaption(tr("map", "MAP"), colMap, {0.0f, 0.5f});
        headCaption(tr("you", "YOU"), colOwn, {1.0f, 0.5f}); headCaption(tr("opponent", "OPPONENT"), colOpp, {1.0f, 0.5f});
        headCaption(tr("result", "RESULT"), colResult, {0.5f, 0.5f});
        auto const rows = std::max<std::size_t>(1, match.rounds.size() + match.deathmatches.size());
        auto const visibleRows = std::min(3.5f, static_cast<float>(rows));
        auto const viewTop = headY - 9.0f, viewH = rowH * visibleRows;
        auto const resultKey = match.matchId.empty() ? fmt::format("{}:{}", match.playerAName, match.stateVersion) : match.matchId;
        auto const presentationAhead = scoreInput && !m_resultPresentationGate.played(match.matchId);
        auto const overlayActive = m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending || presentationAhead;
        auto const animateRows = !overlayActive && m_resultMotionKey != resultKey;
        if (!overlayActive) m_resultMotionKey = resultKey;
        auto const holdRows = overlayActive && m_resultMotionKey != resultKey;
        auto* rowsRoot = m_root;
        auto xShift = 0.0f;
        float y = viewTop - rowH / 2.0f;
        if (rows > 3) {
            auto const contentH = rowH * static_cast<float>(rows);
            auto* scroll = ScrollLayer::create({0.0f, 0.0f, tableW, viewH});
            scroll->setPosition({tableLeft, viewTop - viewH});
            scroll->setID("ranked-result-rounds-scroll"_spr);
            scroll->m_contentLayer->setContentSize({tableW, contentH});
            m_root->addChild(scroll, 4);
            scroll->scrollToTop();
            m_root = scroll->m_contentLayer;
            xShift = tableLeft;
            y = contentH - rowH / 2.0f;
        }
        int rowIndex = 0;
        auto addRow = [&](std::string const& label, std::string const& map, double ownScore, double oppScore, int ownClears, int oppClears,
            std::string const& winner, bool deathmatch) {
            auto* rowGroup = makeMotionGroup();
            auto* outer = m_root; m_root = rowGroup;
            auto const rowDraw = winner == "DRAW" || winner.empty();
            auto const won = !rowDraw && winner == (ownA ? "A" : "B");
            auto* stripe = CCLayerColor::create({corum::ranked::ui::SurfaceRaised.r, corum::ranked::ui::SurfaceRaised.g, corum::ranked::ui::SurfaceRaised.b, 255}, tableW, rowH - 3.0f);
            stripe->setPosition({tableLeft - xShift, y - (rowH - 3.0f) / 2.0f}); m_root->addChild(stripe, 1);
            m_root->addChild(makeLabel(label, 0.24f, {tableLeft + 4.0f - xShift, y}, deathmatch ? kGold : corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f}), 5);
            auto* mapLabel = makeLabel(map, 0.22f, {colMap - xShift, y}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(mapLabel, colOwn - colMap - 44.0f, 0.22f, 0.13f); m_root->addChild(mapLabel, 5);
            auto const detail = [&](int clears) { return deathmatch ? tr("best-progress-short", "BEST") : fmt::format("{} {}", tr("cleared", "CLEARED"), clears); };
            auto* own = makeLabel(formatScore(ownScore, deathmatch), 0.25f, {colOwn - xShift, y + 4.0f}, corum::ranked::ui::Text, "black", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(own, 48.0f, 0.25f, 0.15f); m_root->addChild(own, 5);
            auto* ownSub = makeLabel(detail(ownClears), 0.13f, {colOwn - xShift, y - 7.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(ownSub, 48.0f, 0.13f, 0.09f); m_root->addChild(ownSub, 5);
            auto* opp = makeLabel(formatScore(oppScore, deathmatch), 0.23f, {colOpp - xShift, y + 4.0f}, corum::ranked::ui::TextMuted, "black", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(opp, 46.0f, 0.23f, 0.14f); m_root->addChild(opp, 5);
            auto* oppSub = makeLabel(detail(oppClears), 0.13f, {colOpp - xShift, y - 7.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(oppSub, 46.0f, 0.13f, 0.09f); m_root->addChild(oppSub, 5);
            auto const chipColor = rowDraw ? corum::ranked::ui::SurfaceSoft : won ? corum::ranked::ui::AccentFill : kSideRed;
            auto* chip = corum::ranked::ui::slab({22.0f, 13.0f}, {colResult - xShift, y}, chipColor); m_root->addChild(chip, 5);
            m_root->addChild(makeLabel(rowDraw ? tr("draw-short", "D") : won ? tr("win-short", "W") : tr("lose-short", "L"), 0.17f, {colResult - xShift, y},
                won ? corum::ranked::ui::AccentInk : corum::ranked::ui::Text, "black"), 6);
            m_root = outer;
            if (animateRows) { rowGroup->setPositionX(-30.0f); revealGroup(rowGroup, 0.35f + 0.20f * static_cast<float>(rowIndex), {30.0f, 0}, 0.22f); }
            if (holdRows) rowGroup->setVisible(false);
            ++rowIndex;
            y -= rowH;
        };
        for (auto const& round : match.rounds)
            addRow(fmt::format("R{}", round.roundNumber), round.mapTitle, ownA ? round.scoreA : round.scoreB, ownA ? round.scoreB : round.scoreA,
                ownA ? round.clearsA : round.clearsB, ownA ? round.clearsB : round.clearsA, round.result, false);
        for (auto const& deathmatch : match.deathmatches)
            addRow("DM", deathmatch.mapTitle, ownA ? deathmatch.scoreA : deathmatch.scoreB, ownA ? deathmatch.scoreB : deathmatch.scoreA, 0, 0, deathmatch.winnerSide, true);
        m_root = rowsRoot;
        if (animateRows) holdForMotion(0.35f + 0.20f * static_cast<float>(rowIndex) + 0.3f);

        // Right: the player's LP panel (tier after, change, LP before -> after, event).
        auto const& before = ownA ? match.profileBeforeA : match.profileBeforeB;
        auto const& after = ownA ? match.profileAfterA : match.profileAfterB;
        auto const delta = ownA ? match.rankedScoreDeltaA : match.rankedScoreDeltaB;
        auto const panelX = size.width - 16.0f - panelW / 2.0f, panelTop = size.height - 84.0f, panelBottom = 46.0f;
        auto const panelY = (panelTop + panelBottom) / 2.0f;
        auto const afterTier = upper(after.displayedTier);
        m_root->addChild(makeNeonPanel({panelW, panelTop - panelBottom}, {panelX, panelY}, tierColor(afterTier), kDeepPanel, 246), 1);
        if (auto* badge = corum::ranked::ui::createAnimatedRankBadge(after.displayedTier, {58.0f, 58.0f}, corum::ranked::ui::RankBadgeDisplay::Large)) {
            badge->setPosition({panelX - panelW / 2.0f + 40.0f, panelTop - 40.0f}); m_root->addChild(badge, 4);
        }
        auto const textX = panelX - panelW / 2.0f + 76.0f;
        auto* tierLabel = makeLabel(corum::ranked::rankTierDisplay(afterTier), 0.24f, {textX, panelTop - 20.0f}, tierColor(afterTier), "black", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(tierLabel, panelW - 84.0f, 0.24f, 0.14f); m_root->addChild(tierLabel, 4);
        auto* deltaLabel = makeLabel(delta ? fmt::format("{:+} LP", *delta) : std::string("- LP"), 0.56f, {textX, panelTop - 40.0f},
            !delta || *delta == 0 ? corum::ranked::ui::Text : *delta > 0 ? corum::ranked::ui::Accent : kRed, "black", {0.0f, 0.5f});
        corum::ranked::ui::leanLabel(deltaLabel); corum::ranked::ui::fitMenuLabelWidth(deltaLabel, panelW - 84.0f, 0.56f, 0.3f); m_root->addChild(deltaLabel, 4);
        if (std::abs(after.resultStreak) >= 2) {
            m_root->addChild(makeLabel(corum::ranked::i18n::streak(std::abs(after.resultStreak), after.resultStreak > 0), 0.18f, {textX, panelTop - 58.0f},
                after.resultStreak > 0 ? kGold : kRed, "semibold", {0.0f, 0.5f}), 4);
        }
        auto* lpLine = makeLabel(fmt::format("{} - {} LP", corum::ranked::rankTierDisplay(afterTier), after.visibleRankedScore.value_or(0)), 0.24f,
            {panelX, panelBottom + 44.0f}, corum::ranked::ui::Text, "black");
        corum::ranked::ui::fitMenuLabelWidth(lpLine, panelW - 16.0f, 0.24f, 0.14f); lpLine->setID("ranked-result-lp-line"_spr); m_root->addChild(lpLine, 4);
        auto const event = ratingEventText(ownA ? match.ratingEventA : match.ratingEventB);
        if (!event.first.empty()) {
            auto* eventLabel = makeLabel(event.first, 0.22f, {panelX, panelBottom + 22.0f}, event.second, "black");
            corum::ranked::ui::fitMenuLabelWidth(eventLabel, panelW - 16.0f, 0.22f, 0.14f); m_root->addChild(eventLabel, 4);
        } else if (after.promotion.active) {
            auto* series = makeLabel(corum::ranked::i18n::promotionMatch(after.promotion.winsRequired), 0.20f, {panelX, panelBottom + 22.0f}, kGold, "black");
            corum::ranked::ui::fitMenuLabelWidth(series, panelW - 16.0f, 0.20f, 0.13f); m_root->addChild(series, 4);
        }

        if (!scoreInput) {
            animateRankedScoreChange(panelX, panelBottom + 44.0f, delta, lpLine, before);
            animatePromotionTrigger(panelX, panelBottom + 44.0f, ownA ? match.ratingEventA : match.ratingEventB, before, delta);
        }

        // Footer: REMATCH (primary), HISTORY, LOBBY.
        addButton(tr("queue-again", "QUEUE AGAIN"), {size.width / 2.0f - 70.0f, 22.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.56f);
        addButton(tr("history", "HISTORY"), {size.width / 2.0f + 62.0f, 22.0f}, menu_selector(CorumRankedLayer::onHistory), false, 0.46f, true);
        addButton(tr("to-lobby", "LOBBY"), {size.width / 2.0f + 148.0f, 22.0f}, menu_selector(CorumRankedLayer::onResultClose), false, 0.46f, true);

        // A result can be rendered again after visiting History or after a
        // presentation refresh. Play a tier transition only once per match;
        // subsequent renders immediately show the authoritative settled UI.
        auto const transitionKey = match.matchId.empty()
            ? fmt::format("{}:{}:{}", match.playerAName, match.playerBName, match.stateVersion)
            : match.matchId;
        if (scoreInput) beginResultScore(match,*scoreInput);
        else if (m_rankTransitionPlayedKey != transitionKey) {
            auto const transitionStarted = ownA
                ? playResultRankTransition(
                    match.ratingEventA,
                    match.profileBeforeA,
                    match.profileAfterA
                )
                : playResultRankTransition(
                    match.ratingEventB,
                    match.profileBeforeB,
                    match.profileAfterB
            );
            if (transitionStarted) m_rankTransitionPlayedKey = transitionKey;
        }
        if ((m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending) && m_menu) m_menu->setTouchEnabled(false);
        enqueueAchievementToasts(
            "match:" + transitionKey,
            match.achievementUnlocks
        );
    }

    ccColor3B achievementCardAccent(corum::ranked::AchievementCardView const& card) const {
        return corum::ranked::ui::achievementColor(corum::ranked::achievementCardTier(card,RankedRuntime::get().view().achievements.equippedTitle));
    }

    void renderAchievementTitleSelector(RuntimeView const& view) {
        if (m_titleSelectorAchievement.empty()) return;
        auto const found = std::find_if(
            view.achievements.cards.begin(),
            view.achievements.cards.end(),
            [this](auto const& card) { return card.id == m_titleSelectorAchievement; }
        );
        if (found == view.achievements.cards.end()) {
            m_titleSelectorAchievement.clear();
            return;
        }
        auto const& card = *found;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* dim = CCLayerColor::create({0, 0, 0, 188});
        dim->setContentSize(size);
        dim->setPosition(CCPointZero);
        m_root->addChild(dim, 70);
        auto* panel = makeNeonPanel(
            {390.0f, 178.0f},
            {size.width / 2.0f, size.height / 2.0f},
            achievementCardAccent(card),
            kDeepPanel,
            255
        );
        m_root->addChild(panel, 72);
        auto const name = fmt::format("[{}] {}",
            corum::ranked::achievementScopeName(card.scope,corum::ranked::i18n::isKorean()),
            corum::ranked::i18n::text("achievement."+card.id+".name",card.nameEn));
        auto* heading = makeLabel(
            name,
            0.50f,
            {size.width / 2.0f, size.height / 2.0f + 62.0f},
            achievementCardAccent(card),
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(heading, 320.0f, 0.50f, 0.32f);
        m_root->addChild(heading, 74);
        corum::ranked::ui::styleAchievementText(heading,corum::ranked::achievementCardTier(card,view.achievements.equippedTitle),card.unlocked || card.currentLevel>0);
        auto* hint = makeLabel(
            tr("select-title-version", "SELECT ACHIEVEMENT TITLE VERSION"),
            0.30f,
            {size.width / 2.0f, size.height / 2.0f + 38.0f},
            corum::ranked::ui::TextMuted,
            "semibold"
        );
        m_root->addChild(hint, 74);

        for (int level = 1; level <= 5; ++level) {
            auto const available = corum::ranked::achievementTitleLevelSelectable(
                level,
                card.currentLevel
            );
            auto const current = view.achievements.equippedTitle.active &&
                view.achievements.equippedTitle.achievementId == card.id &&
                view.achievements.equippedTitle.level == level;
            auto const text = current
                ? fmt::format("{}  {}", corum::ranked::achievementStageLabel(level), tr("current", "CURRENT"))
                : available
                    ? corum::ranked::achievementStageLabel(level)
                    : fmt::format("{}  {}", corum::ranked::achievementStageLabel(level), tr("locked", "LOCKED"));
            auto* button = addButton(
                text,
                {size.width / 2.0f - 144.0f + static_cast<float>(level - 1) * 72.0f,
                 size.height / 2.0f},
                menu_selector(CorumRankedLayer::onAchievementTitleLevel),
                false,
                0.52f
            );
            button->setTag(level);
            button->setEnabled(available && !current && !view.achievements.loading);
            if (!available) corum::ranked::ui::grayButtonVisual(button);
            // Stage controls are static; only achievement names animate.
            corum::ranked::ui::styleAchievementTree(button,available?level:0);
        }
        auto* clear = addButton(
            tr("clear-title", "CLEAR TITLE"),
            {size.width / 2.0f - 70.0f, size.height / 2.0f - 52.0f},
            menu_selector(CorumRankedLayer::onAchievementTitleClear),
            true,
            0.56f
        );
        clear->setEnabled(view.achievements.equippedTitle.active && !view.achievements.loading);
        addButton(
            tr("close", "CLOSE"),
            {size.width / 2.0f + 70.0f, size.height / 2.0f - 52.0f},
            menu_selector(CorumRankedLayer::onAchievementTitleSelectorClose),
            false,
            0.56f
        );
    }

    void renderAchievements(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        // m_menu is a sibling above m_root, so base-page CCMenu items would render
        // above the selector dim/panel even with a larger child z-order on m_root.
        // While the selector is open, render only its controls into m_menu.
        auto const titleSelectorOpen = !m_titleSelectorAchievement.empty();
        if (!titleSelectorOpen) addTopBack(false);
        auto const& equippedTitle = view.achievements.equippedTitle;
        // H3 header: back, the leaning brand with its caption; BADGES and
        // CLAIM ALL on the right.
        auto const headY = size.height - kLobbyHeader / 2.0f;
        {   auto* brand = makeLabel(tr("achievements", "ACHIEVEMENTS"), fontScale(30.0f), {px(28.0f) + 24.0f + 8.0f, headY}, corum::ranked::ui::Text, "black", {0.0f, 0.5f});
            corum::ranked::ui::leanLabel(brand); corum::ranked::ui::fitMenuLabelWidth(brand, size.width * 0.26f, fontScale(30.0f), 0.2f);
            brand->setID("ranked-screen-title"_spr); m_root->addChild(brand, 6);
            auto* caption = makeLabel(tr("achievement-subtitle", "PROGRESS · AP · TITLES"), fontScale(13.0f),
                {brand->getPositionX() + brand->getScaledContentSize().width + 10.0f, headY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(caption, size.width * 0.18f, fontScale(13.0f), 0.09f); m_root->addChild(caption, 6);
        }
        if(!titleSelectorOpen){
            boardButton(tr("badge-menu","BADGES"),{70.0f,px(42.0f)},{size.width-px(28.0f)-35.0f,headY},menu_selector(CorumRankedLayer::onBadgeCollection),15.0f,false,"ranked-achievement-badges"_spr);
        }
        // Equipped title (.panel.rule): the label, the own name with the
        // title, the hint and CLEAR TITLE.
        if(!titleSelectorOpen){
            auto const panelY = size.height - 84.0f, panelW = size.width - 2.0f * px(28.0f), left = px(28.0f);
            auto* panel = makeNeonPanel({panelW, 22.0f}, {size.width / 2.0f, panelY}, kAccent, kDeepPanel, 255); m_root->addChild(panel, 3);
            m_root->addChild(makeLabel(tr("equipped-title", "EQUIPPED TITLE"), fontScale(14.0f), {left + 8.0f, panelY}, corum::ranked::ui::TextMuted, "extrabold", {0.0f, 0.5f}), 5);
            auto const titleText = equippedTitle.active ? achievementTitleText(equippedTitle) : tr("no-title", "NO TITLE");
            auto* titleLabel = makeLabel(titleText, fontScale(18.0f), {left + 92.0f, panelY}, equippedTitle.active ? achievementTitleColor(equippedTitle) : corum::ranked::ui::TextMuted, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(titleLabel, panelW * 0.34f, fontScale(18.0f), 0.12f); m_root->addChild(titleLabel, 5);
            auto* hint = makeLabel(tr("title-hint", "SHOWN NEXT TO YOUR NAME · PRESS TITLE ON AN ACHIEVEMENT"), fontScale(13.0f),
                {left + 92.0f + titleLabel->getScaledContentSize().width + 10.0f, panelY}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(hint, std::max(30.0f, panelW - 92.0f - titleLabel->getScaledContentSize().width - 10.0f - 80.0f), fontScale(13.0f), 0.08f); m_root->addChild(hint, 5);
            boardButton(tr("clear-title", "CLEAR TITLE"), {64.0f, px(30.0f)}, {left + panelW - 6.0f - 32.0f, panelY}, menu_selector(CorumRankedLayer::onAchievementTitleClear), 13.0f, false,
                "ranked-achievement-clear-title"_spr, equippedTitle.active && !view.achievements.loading);
        }
        // Status row: double-framed AP and claimable plates, then CLAIM ALL.
        // Achievement grade emblem plate (private, only this screen shows it):
        // grade colour and name, the AP total and five RED..GOLD steps, the
        // next one filled by progress. Tapping it opens the grade ladder.
        auto const totalAp = view.achievements.totalAp;
        auto const grade = corum::ranked::achievementApGrade(totalAp);
        auto const gradeTone = corum::ranked::ui::achievementGradeTone(grade.tier);
        auto const gradePanel = corum::ranked::ui::achievementGradePanel(grade.tier);
        auto const plateSize = CCSize{size.width*.19f, 27.0f};
        auto* ap = corum::ranked::ui::achievementPlate("", plateSize, gradeTone, gradePanel, kGold, 0.38f);
        auto* emblem = corum::ranked::ui::achievementGradeEmblem(21.0f, gradeTone, gradePanel);
        emblem->setPosition({13.5f, 13.5f}); ap->addChild(emblem, 6);
        auto const textLeft = 26.0f, textRight = plateSize.width - 6.0f;
        auto* gradeName = makeLabel(grade.tier.empty() ? tr("achievement-grade-none", "NO GRADE") : std::string(grade.tier),
            0.24f, {textLeft, 18.0f}, gradeTone, "black", {0.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(gradeName, (textRight - textLeft) * 0.52f, 0.24f, 0.13f); ap->addChild(gradeName, 6);
        auto* apText = makeLabel(fmt::format("{} AP", totalAp), 0.22f, {textRight, 18.0f}, kGold, "black", {1.0f, 0.5f});
        corum::ranked::ui::fitMenuLabelWidth(apText, (textRight - textLeft) * 0.46f, 0.22f, 0.12f); ap->addChild(apText, 6);
        auto const stepGap = 1.2f, stepWidth = (textRight - textLeft - 4 * stepGap) / 5.0f;
        for (std::size_t step = 0; step < corum::ranked::kAchievementApGrades.size(); ++step) {
            auto const [stepName, stepAp] = corum::ranked::kAchievementApGrades[step];
            auto const x = textLeft + static_cast<float>(step) * (stepWidth + stepGap);
            auto* back = CCLayerColor::create({42, 36, 64, 255}, stepWidth, 3.0f); back->setPosition({x, 6.0f}); ap->addChild(back, 6);
            auto const fill = totalAp >= stepAp ? 1.0f : grade.nextTier == stepName
                ? corum::ranked::achievementGradeProgress(totalAp, grade.floorAp, grade.nextAp) : 0.0f;
            if (fill <= 0) continue;
            auto const stepTone = corum::ranked::ui::tierColor(std::string(stepName));
            auto* bar = CCLayerColor::create({stepTone.r, stepTone.g, stepTone.b, 255}, stepWidth * fill, 3.0f);
            bar->setPosition({x, 6.0f}); ap->addChild(bar, 7);
        }
        if (!titleSelectorOpen) {
            auto* gradeButton = CCMenuItemSpriteExtra::create(ap, this, menu_selector(CorumRankedLayer::onAchievementGrade));
            gradeButton->setID("ranked-achievement-grade"_spr);
            gradeButton->setPosition({size.width - px(28.0f) - plateSize.width / 2.0f - size.width * .23f - 6.0f, size.height - kLobbyHeader - 13.0f});
            m_menu->addChild(gradeButton);
        } else {
            ap->setID("ranked-achievement-grade"_spr);
            ap->setPosition({size.width - px(28.0f) - plateSize.width / 2.0f - size.width * .23f - 6.0f, size.height - kLobbyHeader - 13.0f});
            m_root->addChild(ap, 5);
        }
        auto const claimable = view.achievements.unclaimedRewardCount > 0;
        auto* pending = corum::ranked::ui::achievementPlate(
            fmt::format("{} {}", view.achievements.unclaimedRewardCount, tr("claimable", "CLAIMABLE")),
            {size.width*.23f, 27.0f}, claimable ? ccc3(255, 111, 138) : kAccent, claimable ? ccc3(32, 10, 18) : kDeepPanel2,
            claimable ? ccc3(255, 214, 222) : corum::ranked::ui::Text, 0.34f);
        pending->setPosition({size.width - px(28.0f) - size.width * .23f / 2.0f, size.height - kLobbyHeader - 13.0f});
        m_root->addChild(pending, 5);
        if(!titleSelectorOpen){
            auto const pendingRewards=corum::ranked::achievementPending(view.achievements.cards,"ALL");
            auto const tier=view.achievements.loading?0:pendingRewards.tier;
            auto const width=std::min(150.f,size.width*.26f);
            auto* visual=makeNeonPanel({width,27},{},corum::ranked::ui::achievementColor(tier),kDeepPanel,255);
            auto* fill=CCLayerColor::create({255,255,255,255},width-3,24);fill->setPosition({1.5f,1.5f});visual->addChild(fill,2);
            corum::ranked::ui::styleAchievementFill(fill,tier);
            corum::ranked::ui::addAchievementInnerFrame(visual,{width,27},tier?ccc3(122,74,16):kAccent,3.0f,150);
            auto* caption=makeLabel(fmt::format("{} (+{} AP)",tr("claim-all","CLAIM ALL"),pendingRewards.amount),.29f,{width/2,13.5f},tier?ccc3(30, 32, 41):corum::ranked::ui::Text,"black");
            corum::ranked::ui::fitFullMenuLabel(caption,width-12,19,.29f);visual->addChild(caption,5);
            auto* button=CCMenuItemSpriteExtra::create(visual,this,menu_selector(CorumRankedLayer::onAchievementClaimAll));
            button->setID("ranked-achievement-claim-all"_spr);button->setPosition({size.width-px(28.0f)-70.0f-6.0f-width/2,size.height-kLobbyHeader/2.0f});
            button->setEnabled(!view.achievements.loading && pendingRewards.rewards>0);m_menu->addChild(button);
        }

        if (view.achievements.loading && !view.achievements.loaded) {
            auto* spinner = LoadingSpinner::create(55.0f);
            spinner->setPosition({size.width / 2.0f, size.height / 2.0f - 8.0f});
            m_root->addChild(spinner, 4);
            return;
        }
        if (!view.achievements.error.empty()) {
            auto* error = makeLabel(
                shorten(view.achievements.error, 72),
                0.28f,
                {size.width / 2.0f, 48.0f},
                kRed,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(error, size.width - 90.0f, 0.23f, 0.16f);
            m_root->addChild(error, 8);
        }

        if (!titleSelectorOpen) {
            // Scope tabs with pointed ends; the selected one is filled.
            for (int tab = 0; tab < 3; ++tab) {
                auto const scope = tab == 0 ? "SOLO_RANKED" : tab == 1 ? "SURVIVAL" : "SHARED";
                auto const name = corum::ranked::achievementScopeName(scope,corum::ranked::i18n::isKorean());
                // H3 line tabs on the left of the second row.
                historyTab(name, {px(28.0f) + 32.0f + static_cast<float>(tab) * 66.0f, size.height - kLobbyHeader - 13.0f}, 62.0f,
                    menu_selector(CorumRankedLayer::onAchievementScope), tab, m_achievementScope == scope, std::nullopt);
            }
        }
        // H3 grid (board): three columns by two rows between the title panel
        // and the footer; six cards a page.
        constexpr std::size_t perPage = 6;
        auto const order = corum::ranked::achievementDisplayOrderForScope(view.achievements.cards,m_achievementScope);
        auto const pageCount = std::max<std::size_t>(
            1,
            (order.size() + perPage - 1) / perPage
        );
        if (order.empty() && view.achievements.loaded) {
            auto* empty = makeLabel(tr("no-achievements","NO ACHIEVEMENTS IN THIS SECTION"),0.28f,{size.width/2,size.height/2-25},kAccent,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(empty,size.width-65,0.28f,0.18f); m_root->addChild(empty,5);
        }
        m_achievementPage = std::min(m_achievementPage, pageCount - 1);
        auto const start = m_achievementPage * perPage;
        auto const gridLeft = px(28.0f), gridRight = size.width - px(28.0f), gridGap = 5.0f;
        auto const gridTop = size.height - 84.0f - 11.0f - 6.0f, gridBottom = 30.0f;
        auto const cardWidth = (gridRight - gridLeft - 2.0f * gridGap) / 3.0f;
        auto const cardHeight = (gridTop - gridBottom - gridGap) / 2.0f;
        for (std::size_t slot = 0; slot < perPage && start + slot < order.size(); ++slot) {
            auto const index = order[start + slot];
            auto const& card = view.achievements.cards[index];
            auto const column = slot % 3;
            auto const row = slot / 3;
            auto const x0 = gridLeft + static_cast<float>(column) * (cardWidth + gridGap);
            auto const y0 = gridTop - static_cast<float>(row + 1) * cardHeight - static_cast<float>(row) * gridGap;
            auto const top = y0 + cardHeight, right = x0 + cardWidth;
            auto const cardTier = corum::ranked::achievementCardTier(card,view.achievements.equippedTitle);
            auto const achieved = card.type == "PROGRESSIVE" ? card.currentLevel > 0 : card.unlocked;
            auto const accent = cardTier > 0 ? corum::ranked::ui::achievementColor(cardTier) : corum::ranked::ui::Text;
            // .panel with the tier colour as its inset top rule.
            auto* fill = CCLayerColor::create({corum::ranked::ui::Surface.r, corum::ranked::ui::Surface.g, corum::ranked::ui::Surface.b, 255}, cardWidth, cardHeight);
            fill->setPosition({x0, y0}); fill->setID("ranked-achievement-card"_spr); m_root->addChild(fill, 1);
            if (cardTier > 0 || achieved) {
                auto* rule = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, cardWidth, 1.0f);
                rule->setPosition({x0, top - 1.0f}); m_root->addChild(rule, 2);
            }
            // Title row: the 18 px name in the tier colour and the stage chip.
            auto const chipText = card.type == "PROGRESSIVE"
                ? (card.maxed ? tr("achievement-max-stage", "MAX STAGE") : fmt::format("LV {}", std::clamp(card.currentLevel, 0, 5)))
                : card.unlocked ? upper(card.rarity) : tr("achievement-not-yet", "NOT YET");
            auto* chipProbe = makeLabel(chipText, fontScale(12.0f), {}, corum::ranked::ui::Text, "black");
            auto const chipWidth = chipProbe->getScaledContentSize().width + px(16.0f);
            addChip(m_root, chipText, {right - 7.0f - chipWidth, top - 11.0f}, card.maxed ? kGold : ccc3(32, 35, 46),
                card.maxed ? corum::ranked::ui::AccentInk : corum::ranked::ui::TextMuted, 12.0f);
            auto const baseName = corum::ranked::achievementCardName(card,corum::ranked::i18n::isKorean());
            auto const title = card.type == "PROGRESSIVE" && card.currentLevel > 0
                ? fmt::format("{} - {}", baseName, card.displayLabel)
                : baseName;
            auto* titleLabel = makeLabel(title, fontScale(18.0f), {x0 + 8.0f, top - 11.0f}, accent, "black", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(titleLabel, std::max(40.0f, cardWidth - 22.0f - chipWidth), fontScale(18.0f), 0.12f);
            corum::ranked::ui::styleAchievementText(titleLabel,cardTier,achieved);
            m_root->addChild(titleLabel, 4);
            // Description (.cap), up to two lines under the title.
            auto const description = corum::ranked::achievementCardDescription(card,corum::ranked::i18n::isKorean());
            auto* descriptionLabel = makeLabel(description, fontScale(13.0f), {x0 + 8.0f, top - 21.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 1.0f});
            corum::ranked::ui::fitWrappedMenuLabel(descriptionLabel, description, cardWidth - 16.0f, 18.0f, fontScale(13.0f));
            descriptionLabel->setAlignment(kCCTextAlignmentLeft); descriptionLabel->setAnchorPoint({0.0f, 1.0f});
            m_root->addChild(descriptionLabel, 4);
            // Holders, over the bar on the right.
            auto* holdersLabel = makeLabel(
                card.stageHolders
                    ? fmt::format(fmt::runtime(tr("achievement-stage-holders", "{} players at this stage (top {}%)")), *card.stageHolders, fmt::format("{:.1f}", card.stageTopPercent))
                    : fmt::format(fmt::runtime(tr("achievement-holders", "{} players ({}%)")), card.holders, fmt::format("{:.1f}", card.holderPercent)),
                fontScale(12.0f), {right - 8.0f, y0 + 31.0f}, corum::ranked::ui::TextMuted, "semibold", {1.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(holdersLabel, cardWidth - 16.0f, fontScale(12.0f), 0.08f);
            m_root->addChild(holdersLabel, 4);
            // .bar.thin in the tier colour.
            double progress = card.type == "PROGRESSIVE"
                ? std::clamp(card.gauge, 0.0, 1.0)
                : card.unlocked ? 1.0 : 0.0;
            auto const barWidth = cardWidth - 16.0f;
            auto* barBack = CCLayerColor::create({32, 35, 46, 255}, barWidth, 3.0f);
            barBack->setPosition({x0 + 8.0f, y0 + 22.0f}); barBack->setSkewX(20.0f); m_root->addChild(barBack, 3);
            auto* barFill = CCLayerColor::create({accent.r, accent.g, accent.b, 245});
            barFill->setContentSize({static_cast<float>(barWidth * progress), 3.0f});
            barFill->setPosition(barBack->getPosition()); barFill->setSkewX(20.0f);
            m_root->addChild(barFill, 4);
            corum::ranked::ui::styleAchievementFill(barFill,cardTier,true,card.type=="PROGRESSIVE"?card.currentLevel>=4:card.unlocked);
            // Bottom row: progress / state on the left, CLAIM and TITLE on the right.
            auto const progressText = card.type == "PROGRESSIVE"
                ? card.maxed
                    ? fmt::format("{}", card.metricValue)
                    : fmt::format("{} / {}", card.metricValue, card.nextThreshold.value_or(0))
                : card.unlocked
                    ? fmt::format("{} · {} AP", tr("completed", "COMPLETED"), card.rewardAp)
                    : fmt::format("0 / 1 · {} AP", card.rewardAp);
            auto* progressLabel = makeLabel(progressText, fontScale(13.0f), {x0 + 8.0f, y0 + 10.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f});
            corum::ranked::ui::fitMenuLabelWidth(progressLabel, cardWidth * 0.42f, fontScale(13.0f), 0.08f);
            m_root->addChild(progressLabel, 4);

            if (!titleSelectorOpen) {
                auto const claimState = corum::ranked::achievementClaimState(
                    card.unlocked,
                    card.currentLevel,
                    card.claimableRewards
                );
                auto const canClaim = claimState == corum::ranked::AchievementClaimState::Unclaimed;
                auto const buttonW = 46.0f, buttonH = 13.0f;
                auto const titleUnlocked = achieved;
                auto const equipped = view.achievements.equippedTitle.active &&
                    view.achievements.equippedTitle.achievementId == card.id;
                auto* titleFace = corum::ranked::ui::achievementButtonFace(
                    equipped ? (card.type=="PROGRESSIVE"?tr("current", "CURRENT"):tr("unequip-title", "UNEQUIP")) : tr("set-title", "TITLE"),
                    {buttonW, buttonH},
                    !titleUnlocked ? corum::ranked::ui::AchievementButtonStyle::Disabled
                        : equipped ? corum::ranked::ui::AchievementButtonStyle::Current : corum::ranked::ui::AchievementButtonStyle::Title,
                    0.20f);
                auto* titleButton = CCMenuItemSpriteExtra::create(titleFace, this, menu_selector(CorumRankedLayer::onAchievementTitle));
                titleButton->setPosition({right - 8.0f - buttonW / 2.0f, y0 + 10.0f});
                m_menu->addChild(titleButton);
                titleButton->setTag(static_cast<int>(index));
                titleButton->setEnabled(titleUnlocked && !view.achievements.loading);
                // CLAIM shows while a reward waits (the board's claimable chip).
                if (canClaim) {
                    auto* claimFace = corum::ranked::ui::achievementButtonFace(fmt::format("CLAIM +{}", card.claimableAp), {buttonW, buttonH},
                        corum::ranked::ui::AchievementButtonStyle::Claim, 0.20f);
                    auto* claim = CCMenuItemSpriteExtra::create(claimFace, this, menu_selector(CorumRankedLayer::onAchievementClaim));
                    claim->setPosition({right - 8.0f - buttonW - 4.0f - buttonW / 2.0f, y0 + 10.0f});
                    m_menu->addChild(claim);
                    claim->setTag(static_cast<int>(index));
                    claim->setEnabled(!view.achievements.loading);
                }
                if (equipped) {
                    // H3: the equipped card is outlined in lime and carries an
                    // EQUIPPED chip on its top edge.
                    auto const lime = corum::ranked::ui::Accent;
                    for (auto const& [pos, sz] : std::array<std::pair<CCPoint, CCSize>, 4>{{
                        {{x0, y0}, {cardWidth, 1.5f}}, {{x0, y0 + cardHeight - 1.5f}, {cardWidth, 1.5f}},
                        {{x0, y0}, {1.5f, cardHeight}}, {{x0 + cardWidth - 1.5f, y0}, {1.5f, cardHeight}}}}) {
                        auto* edge = CCLayerColor::create({lime.r, lime.g, lime.b, 255}, sz.width, sz.height);
                        edge->setPosition(pos); m_root->addChild(edge, 6);
                    }
                    auto* chip = corum::ranked::ui::slab({50.0f, 11.0f}, {x0 + 32.0f, y0 + cardHeight}, corum::ranked::ui::AccentFill);
                    chip->setID("ranked-achievement-equipped-chip"_spr); m_root->addChild(chip, 7);
                    auto* chipLabel = makeLabel(tr("title-equipped-chip", "EQUIPPED"), 0.17f, {x0 + 32.0f, y0 + cardHeight}, corum::ranked::ui::AccentInk, "black");
                    corum::ranked::ui::fitMenuLabelWidth(chipLabel, 44.0f, 0.17f, 0.11f); m_root->addChild(chipLabel, 8);
                }
            }
        }

        // Footer: n / m PAGE and the ghost page buttons on the right.
        if (pageCount > 1 && !titleSelectorOpen) {
            m_root->addChild(makeLabel(fmt::format(fmt::runtime(tr("page-count", "{} / {} PAGE")), m_achievementPage + 1, pageCount), fontScale(13.0f),
                {px(28.0f), 15.0f}, corum::ranked::ui::TextMuted, "semibold", {0.0f, 0.5f}), 5);
            boardButton("<", {24.0f, px(34.0f)}, {size.width - px(28.0f) - 24.0f - 4.0f - 12.0f, 15.0f}, menu_selector(CorumRankedLayer::onAchievementPrevious),
                14.0f, false, "ranked-achievement-previous"_spr, m_achievementPage > 0);
            boardButton(">", {24.0f, px(34.0f)}, {size.width - px(28.0f) - 12.0f, 15.0f}, menu_selector(CorumRankedLayer::onAchievementNext),
                14.0f, false, "ranked-achievement-next"_spr, m_achievementPage + 1 < pageCount);
        }
        renderAchievementTitleSelector(view);
    }

    CCLabelBMFont* historyText(CCNode* parent,std::string const& text,float scale,CCPoint position,float width,ccColor3B color=corum::ranked::ui::Text,bool primary=false,CCPoint anchor={.5f,.5f}) {
        auto* label=makeLabel(text,scale,position,color,primary?"black":"semibold",anchor);
        corum::ranked::ui::fitSingleLineMenuLabel(label,width,28,scale);parent->addChild(label,5);return label;
    }
    ccColor3B historyRoundColor(corum::ranked::HistoryRound const& round)const {
        if(!round.completed)return kGold;
        // false is an authoritative elimination; null means no confirmed own
        // participation/result. Never collapse these two states into gray.
        if(!round.ownQualified.has_value())return corum::ranked::ui::TextMuted;
        if(!*round.ownQualified)return kRed;
        if(round.ownRank){auto const color=corum::ranked::survivalRankColor(*round.ownRank,true);return ccc3(color.r,color.g,color.b);}
        return kGreen;
    }
    CCMenuItemSpriteExtra* historyButton(CCNode* parent,std::string const& text,CCPoint position,float width,SEL_MenuHandler handler,int tag=0,bool selected=false,std::optional<ccColor3B> accent=std::nullopt) {
        auto const tint=accent.value_or(selected?kAccent:kSteel);
        auto* face=makeNeonPanel({width,26},{},tint,kDeepPanel2,245);
        auto const labelColor=selected?kAccent:corum::ranked::ui::Text;
        historyText(face,text,.25f,{width/2,13},width-10,labelColor);
        auto* menu=CCMenu::create();menu->setAnchorPoint(CCPointZero);menu->setContentSize({width,26});menu->setPosition({position.x-width/2,position.y-13});parent->addChild(menu,20);
        auto* button=CCMenuItemSpriteExtra::create(face,this,handler);button->setPosition({width/2,13});button->setTag(tag);menu->addChild(button);return button;
    }
    // .tab: 44 px, 16 px label; the selected one is white with a 3 px lime
    // underline. A result tint marks the other tabs with a short line.
    CCMenuItemSpriteExtra* historyTab(std::string const& text,CCPoint position,float width,SEL_MenuHandler handler,int tag,bool selected,std::optional<ccColor3B> tint) {
        auto* face=CCNode::create();face->setContentSize({width,22.0f});
        auto* label=makeLabel(text,fontScale(16.0f),{width/2.0f,12.0f},selected?corum::ranked::ui::Text:corum::ranked::ui::TextMuted,"extrabold");
        corum::ranked::ui::fitMenuLabelWidth(label,width-6.0f,fontScale(16.0f),0.1f);face->addChild(label,2);
        auto const mark=selected?corum::ranked::ui::Accent:tint.value_or(corum::ranked::ui::SurfaceRaised);
        auto* line=CCLayerColor::create({mark.r,mark.g,mark.b,static_cast<GLubyte>(selected?255:tint?150:0)},selected?width:width*0.4f,1.5f);
        line->setPosition({selected?0.0f:width*0.3f,0.0f});face->addChild(line,3);
        auto* button=CCMenuItemSpriteExtra::create(face,this,handler);button->setPosition(position);button->setTag(tag);m_menu->addChild(button);return button;
    }
    std::string historyOutcome(HistoryMatchView const& item)const {
        if(item.inProgress)return tr("history-in-progress","MATCH IN PROGRESS");
        if(item.mode==corum::ranked::RankedMode::SoloRanked)return item.winnerSide.empty()?tr("tie","TIE"):item.side==item.winnerSide?tr("victory","VICTORY"):tr("defeat","DEFEAT");
        if(item.survivalRank==1)return tr("history-champion","CHAMPION");
        if(item.survivalRank>1&&item.survivalRank<=3)return tr("history-podium","PODIUM");
        if(item.record["reachedFinal"].asBool().unwrapOr(false))return tr("history-finalist","FINALIST");
        return item.survivalRank>0?tr("history-eliminated","ELIMINATED"):tr("history-unsettled","RESULT PENDING");
    }
    ccColor3B historyColor(HistoryMatchView const& item)const {
        if(item.inProgress)return kAccent;
        if(item.mode==corum::ranked::RankedMode::SoloRanked)return item.side==item.winnerSide?kGreen:kRed;
        return item.survivalRank==1?kGold:item.survivalRank>0&&item.survivalRank<=3?kAccent:item.record["reachedFinal"].asBool().unwrapOr(false)?kSteel:item.survivalRank>0?kRed:corum::ranked::ui::TextMuted;
    }
    std::string historyDelta(HistoryMatchView const& item)const {
        return item.inProgress||item.record["settlementPending"].asBool().unwrapOr(false)?tr("history-unsettled","RESULT PENDING"):item.ownRankedScoreDelta?fmt::format("{:+d} LP",*item.ownRankedScoreDelta):tr("history-unavailable","UNAVAILABLE");
    }
    std::string historyFilterText(std::string const& key)const {
        if(key=="WIN")return tr("victory","VICTORY");if(key=="LOSS")return tr("defeat","DEFEAT");
        if(key=="CHAMPION")return tr("history-champion","CHAMPION");if(key=="PODIUM")return tr("history-podium","PODIUM");
        if(key=="ELIMINATED")return tr("history-eliminated","ELIMINATED");if(key=="IN_PROGRESS")return tr("history-in-progress","MATCH IN PROGRESS");
        return tr("history-all-results","ALL RESULTS");
    }
    // H1 header: the back button, the leaning brand, the ALL / SURVIVAL /
    // SOLO RANKED segments and the result / order filters on the right, and
    // the caption row under it.
    void renderHistoryTabs() {
        auto const size=CCDirector::sharedDirector()->getWinSize();auto const headY=size.height-kLobbyHeader/2.0f;
        auto* brand=makeLabel(tr("match-history","MATCH HISTORY"),fontScale(30.0f),{px(28.0f)+24.0f+8.0f,headY},corum::ranked::ui::Text,"black",{0.0f,0.5f});
        corum::ranked::ui::leanLabel(brand);corum::ranked::ui::fitMenuLabelWidth(brand,size.width*0.30f,fontScale(30.0f),0.2f);brand->setID("ranked-screen-title"_spr);m_root->addChild(brand,6);
        auto const orderW=72.0f,filterW=72.0f,segW=54.0f;
        auto x=size.width-px(28.0f);
        boardButton(m_historyOrder=="OLDEST"?tr("history-oldest","OLDEST FIRST"):tr("history-newest","NEWEST FIRST"),{orderW,px(34.0f)},{x-orderW/2.0f,headY},
            menu_selector(CorumRankedLayer::onHistoryOrder),13.0f,false,"ranked-history-order"_spr);
        x-=orderW+4.0f;
        boardButton(historyFilterText(m_historyResult),{filterW,px(34.0f)},{x-filterW/2.0f,headY},menu_selector(CorumRankedLayer::onHistoryFilter),13.0f,false,"ranked-history-filter"_spr);
        x-=filterW+8.0f;
        // Segments in board order: ALL, SURVIVAL, SOLO RANKED (tags keep the scope ids).
        for(int slot=0;slot<3;++slot){
            auto const tab=slot==0?2:slot==1?0:1;
            auto const scope=tab==0?corum::ranked::HistoryScope::Survival:tab==1?corum::ranked::HistoryScope::SoloRanked:corum::ranked::HistoryScope::All;
            auto* item=boardButton(tab==0?tr("survival","SURVIVAL"):tab==1?tr("solo-ranked","SOLO RANKED"):tr("all-history","ALL"),{segW,px(34.0f)},
                {x-(2-slot)*(segW+2.0f)-segW/2.0f,headY},menu_selector(CorumRankedLayer::onHistoryScope),13.0f,m_historyScope==scope,fmt::format("ranked-history-scope-{}",tab));
            item->setTag(tab);
        }
        auto const capY=size.height-kLobbyHeader-6.0f;
        m_root->addChild(makeLabel(tr("history-latest-30","UP TO 30 MATCHES"),fontScale(13.0f),{px(28.0f),capY},corum::ranked::ui::TextMuted,"semibold",{0.0f,0.5f}),6);
        m_root->addChild(makeLabel(fmt::format("{} · {}",historyFilterText(m_historyResult),m_historyOrder=="OLDEST"?tr("history-oldest","OLDEST FIRST"):tr("history-newest","NEWEST FIRST")),
            fontScale(13.0f),{size.width-px(28.0f),capY},corum::ranked::ui::TextMuted,"semibold",{1.0f,0.5f}),6);
        historyButton(m_root,tr("refresh","REFRESH"),{size.width/2,22},90,menu_selector(CorumRankedLayer::onHistoryRefresh));
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        // Debug build: every record stored on the server (server checks the channel).
        auto const everyone=RankedRuntime::get().historyAllRecords();
        historyButton(m_root,fmt::format("{}: {}",tr("history-all-records","ALL RECORDS"),everyone?"ON":"OFF"),{size.width/2-120,22},120,
            menu_selector(CorumRankedLayer::onHistoryAllRecords),0,everyone);
#endif
    }
    // H1 row (.tr, 64 px): the left rule in the result colour, the result
    // chip, the mode / opponent line with its caption, the date, the LP change,
    // the tier after and the chevron.
    static constexpr float kHistoryRowHeight = 32.0f, kHistoryRowStep = 35.0f;
    CCNode* historyRow(HistoryMatchView const& item,float width) {
        bool const survival=item.mode==corum::ranked::RankedMode::Survival;
        auto* face=CCNode::create();face->setContentSize({width,kHistoryRowHeight});
        auto const pending=item.inProgress||item.record["settlementPending"].asBool().unwrapOr(false);
        auto* fill=CCLayerColor::create({corum::ranked::ui::Surface.r,corum::ranked::ui::Surface.g,corum::ranked::ui::Surface.b,static_cast<GLubyte>(pending?178:255)},width,kHistoryRowHeight);
        face->addChild(fill,0);
        // Result: chip text, chip fill and the rule colour.
        std::string chip;ccColor3B tone=corum::ranked::ui::SurfaceRaised;bool dim=false;
        auto const own=item.side=="B"?item.roundWinsB:item.roundWinsA,rival=item.side=="B"?item.roundWinsA:item.roundWinsB;
        if(item.inProgress){chip=tr("history-in-progress","MATCH IN PROGRESS");dim=true;}
        else if(!survival){auto const won=item.side==item.winnerSide;chip=fmt::format("{} {}:{}",item.winnerSide.empty()?tr("tie","TIE"):won?tr("victory","VICTORY"):tr("defeat","DEFEAT"),own,rival);
            tone=item.winnerSide.empty()?corum::ranked::ui::SurfaceRaised:won?corum::ranked::ui::AccentFill:kRed;dim=item.winnerSide.empty();}
        else if(item.survivalRank==1){chip=tr("survival-winner","WINNER");tone=corum::ranked::ui::AccentFill;}
        else if(item.survivalRank>1&&item.survivalRank<=3){chip=fmt::format("{} · {}",corum::ranked::survivalOrdinal(item.survivalRank),tr("history-podium","PODIUM"));tone=kGold;}
        else if(item.survivalRank>0){chip=fmt::format("{} · {}",corum::ranked::survivalOrdinal(item.survivalRank),tr("history-eliminated","ELIMINATED"));dim=true;}
        else{chip=tr("history-unsettled","RESULT PENDING");dim=true;}
        if(!pending){auto const rule=dim&&survival&&item.survivalRank>3?kRed:dim?corum::ranked::ui::SurfaceSoft:tone;
            auto* edge=CCLayerColor::create({rule.r,rule.g,rule.b,255},2.0f,kHistoryRowHeight);face->addChild(edge,1);}
        addChip(face,chip,{8.0f,kHistoryRowHeight/2.0f},tone,dim?corum::ranked::ui::TextMuted:corum::ranked::ui::AccentInk,12.0f);
        // Columns from the right: chevron, tier, LP, date.
        auto const chevronX=width-8.0f-6.0f,tierX=chevronX-6.0f-6.0f-55.0f,lpX=tierX-6.0f-60.0f,dateX=lpX-6.0f-75.0f,titleX=8.0f+55.0f+6.0f;
        std::string title,caption;
        if(survival){
            auto const total=item.record["participantCount"].asInt().unwrapOr(0);
            title=fmt::format("{} · {}",tr("survival","SURVIVAL"),fmt::format(fmt::runtime(tr("players-count-format","{} PLAYERS")),total));
            auto const rounds=corum::ranked::historyRounds(item.record,true);
            caption=fmt::format(fmt::runtime(tr("survival-rounds-count","{} ROUNDS")),rounds.size());
            if(!rounds.empty()&&!rounds.back().title.empty())caption+=" · "+rounds.back().label+" "+rounds.back().title;
        }else{
            title=fmt::format("{} · vs {}",tr("solo-ranked","SOLO RANKED"),item.opponentName);
            for(auto const& round:corum::ranked::historyRounds(item.record,false)){if(round.title.empty())continue;if(!caption.empty())caption+=" · ";caption+=round.deathmatch?tr("death-match","DEATH MATCH"):round.title;}
            if(auto bo=item.record["bestOf"].asInt();bo.isOk()&&caption.empty())caption=fmt::format("BO{}",bo.unwrap());
        }
        if(item.debug)caption+=caption.empty()?"DEBUG":"  DEBUG";
        auto* titleLabel=makeLabel(title,fontScale(16.0f),{titleX,21.0f},corum::ranked::ui::Text,"extrabold",{0.0f,0.5f});
        corum::ranked::ui::fitMenuLabelWidth(titleLabel,dateX-titleX-6.0f,fontScale(16.0f),0.1f);face->addChild(titleLabel,3);
        auto* captionLabel=makeLabel(caption,fontScale(13.0f),{titleX,10.0f},corum::ranked::ui::TextMuted,"semibold",{0.0f,0.5f});
        corum::ranked::ui::fitMenuLabelWidth(captionLabel,dateX-titleX-6.0f,fontScale(13.0f),0.08f);face->addChild(captionLabel,3);
        auto const when=item.finishedAt.empty()?item.createdAt:item.finishedAt;
        auto const date=when.size()>=16?when.substr(0,10)+" "+when.substr(11,5):when.substr(0,std::min<std::size_t>(10,when.size()));
        face->addChild(makeLabel(date,fontScale(13.0f),{dateX,kHistoryRowHeight/2.0f},corum::ranked::ui::TextMuted,"semibold",{0.0f,0.5f}),3);
        auto const delta=item.ownRankedScoreDelta;
        auto* lp=makeLabel(pending?tr("history-unsettled","RESULT PENDING"):delta?fmt::format("{:+d} LP",*delta):std::string("-"),fontScale(16.0f),{lpX,kHistoryRowHeight/2.0f},
            pending||!delta||*delta==0?corum::ranked::ui::TextMuted:*delta>0?corum::ranked::ui::Accent:kRed,"black",{0.0f,0.5f});
        corum::ranked::ui::fitMenuLabelWidth(lp,60.0f,fontScale(16.0f),0.1f);face->addChild(lp,3);
        auto const tierAfter=survival?item.record["result"]["profileAfter"]["tier"].asString().unwrapOr(""):item.record["profileAfter"][item.side]["displayedTier"].asString().unwrapOr("");
        if(!tierAfter.empty()){auto* tier=makeLabel(corum::ranked::rankTierDisplay(upper(tierAfter)),fontScale(13.0f),{tierX,kHistoryRowHeight/2.0f},tierColor(upper(tierAfter)),"semibold",{0.0f,0.5f});
            corum::ranked::ui::fitMenuLabelWidth(tier,55.0f,fontScale(13.0f),0.08f);face->addChild(tier,3);}
        face->addChild(makeLabel(">",fontScale(16.0f),{chevronX,kHistoryRowHeight/2.0f},corum::ranked::ui::TextMuted,"black"),3);
        return face;
    }
    // A placement stage in the list: PLACEMENT n · grade with both map grades.
    CCNode* historyPlacementRow(corum::ranked::PlacementStageResultView const& stage,float width) {
        auto* face=CCNode::create();face->setContentSize({width,kHistoryRowHeight});
        face->addChild(CCLayerColor::create({corum::ranked::ui::Surface.r,corum::ranked::ui::Surface.g,corum::ranked::ui::Surface.b,255},width,kHistoryRowHeight),0);
        face->addChild(CCLayerColor::create({kGold.r,kGold.g,kGold.b,255},2.0f,kHistoryRowHeight),1);
        addChip(face,fmt::format("{} {}",tr("placement","PLACEMENT"),stage.stageIndex),{8.0f,kHistoryRowHeight/2.0f},kGold,corum::ranked::ui::AccentInk,12.0f);
        auto const first=stage.mapGrades.empty()?"?":stage.mapGrades[0],second=stage.mapGrades.size()<2?"?":stage.mapGrades[1];
        auto* line=makeLabel(fmt::format("STAGE {} · {}   MAP 1 {} + MAP 2 {}   {:.1f}/500",stage.stageIndex,stage.stageGrade,first,second,stage.stageScore),fontScale(15.0f),
            {8.0f+55.0f+6.0f,kHistoryRowHeight/2.0f},corum::ranked::ui::Text,"extrabold",{0.0f,0.5f});
        corum::ranked::ui::fitMenuLabelWidth(line,width-80.0f,fontScale(15.0f),0.1f);face->addChild(line,3);
        return face;
    }
    void renderHistoryList(RuntimeView const& view) {
        auto const size=CCDirector::sharedDirector()->getWinSize();addTopBack(false);renderHistoryTabs();
        if(view.historyLoading){auto* spinner=LoadingSpinner::create(40);spinner->setPosition({size.width/2,size.height/2});m_root->addChild(spinner,3);return;}
        if(!view.historyError.empty()){
            historyText(m_root,view.historyError,.27f,{size.width/2,size.height/2},size.width-60,kRed);return;
        }
        auto const placements=m_historyScope==corum::ranked::HistoryScope::Survival||m_historyResult!="ALL"?0:std::min<std::size_t>(5,view.placement.stageResults.size());
        auto const matches=std::min<std::size_t>(30,view.history.size());
        if(!placements&&!matches){historyText(m_root,tr("history-empty","NO MATCHES FOR THIS FILTER"),.30f,{size.width/2,size.height/2-15},size.width-60,kAccent);return;}
        // H1 list: rows between the caption row and the footer (REFRESH).
        auto const viewport=CCSize{size.width-28,std::max(55.f,size.height-kLobbyHeader-14-43)};
        auto* scroll=corum::ranked::ui::createRankedTapScroll(viewport,[weak=WeakRef(this),revision=view.historyRevision](CCPoint start,CCPoint end){
            auto owner=weak.lock();if(!owner || !owner->isRunning() || owner->m_page!=Page::HistoryList || !owner->m_historyListScroll || revision!=RankedRuntime::get().view().historyRevision)return;
            if(owner->onHistoryPlayerTap(start,end))return;
            auto const inside=[](CCNode* node,CCPoint point){auto p=node->convertToNodeSpace(point);auto s=node->getContentSize();return p.x>=0&&p.y>=0&&p.x<=s.width&&p.y<=s.height;};
            for(auto* row:owner->m_historyListScroll->m_contentLayer->getChildrenExt())if(row->getTag()>=0&&inside(row,start)&&inside(row,end)){
                queueInMainThread([weak,target=WeakRef(row),revision]{auto owner=weak.lock();auto row=target.lock();
                    if(owner&&row&&owner->isRunning()&&owner->m_page==Page::HistoryList&&owner->m_historyListScroll&&row->getParent()==owner->m_historyListScroll->m_contentLayer&&revision==RankedRuntime::get().view().historyRevision)owner->onHistoryDetail(row.data());});
                break;
            }
        });scroll->setID("ranked-history-matches-scroll"_spr);
        scroll->setPosition({(size.width-viewport.width)/2,43});m_root->addChild(scroll,3);m_historyListScroll=scroll;
        auto* content=scroll->m_contentLayer;auto const height=std::max(viewport.height,static_cast<float>((matches+placements)*kHistoryRowStep+4));content->setContentSize({viewport.width,height});
        float y=height-2;
        for(std::size_t i=0;i<matches;++i,y-=kHistoryRowStep){
            // The ScrollLayer owns the entire gesture, including a drag begun
            // on a card. Only an unmoved, still-current tap selects its match.
            auto* face=historyRow(view.history[i],viewport.width);
            face->setAnchorPoint(CCPointZero);face->setPosition({0,y-kHistoryRowHeight});face->setTag(static_cast<int>(i));content->addChild(face,3);
        }
        for(std::size_t i=0;i<placements;++i,y-=kHistoryRowStep){
            auto* panel=historyPlacementRow(view.placement.stageResults[i],viewport.width);
            panel->setAnchorPoint(CCPointZero);panel->setPosition({0,y-kHistoryRowHeight});panel->setTag(-1);content->addChild(panel,2);
        }
        scroll->scrollToTop();if(m_historyListOffset)content->setPositionY(std::clamp(*m_historyListOffset,viewport.height-height,0.f));
    }
    void historyPlayerName(CCNode* parent,corum::ranked::SurvivalStanding const& player,CCPoint position,float width,ccColor3B color,std::string const& match="",bool survival=true) {
        auto const& rows=RankedRuntime::get().view().history;
        auto const id=match.empty()?m_historySelectedId:match;
        auto const mode=match.empty()&&m_historyIndex<rows.size()?rows[m_historyIndex].mode==corum::ranked::RankedMode::Survival:survival;
        auto record=std::find_if(rows.begin(),rows.end(),[&](auto const& item){return item.matchId==id;});
        matjson::Value profile;
        if(!player.bot&&player.accountId>0&&record!=rows.end())profile=record->record["currentPlayers"][std::to_string(player.accountId)];
        if(profile["gdAccountId"].asString().unwrapOr("")!=std::to_string(player.accountId))profile=matjson::Value();
        auto const current=profile[mode?"survival":"solo"];
        auto const hasRank=current.isObject();
        auto* hit=CCNode::create();hit->setContentSize({width,34});hit->setPosition({position.x-width/2,position.y-17});parent->addChild(hit,8);
        auto* name=corum::ranked::ui::playerNameLabel(player.name,.32f,{0,25},color,corum::ranked::ui::FontWeight::ExtraBold);name->setAnchorPoint({0,.5f});
        corum::ranked::ui::fitSingleLineMenuLabel(name,width,18,.32f);hit->addChild(name,5);
        corum::ranked::ui::publicPlayerTitle(hit,profile["equippedTitle"],{width/2,10},width,.17f);
        if(hasRank){
            auto value=current["promotion"].isObject()?tr("promotion-match","PROMOTION MATCH"):current["lp"].isNumber()?std::to_string(current["lp"].asInt().unwrapOr(0))+" LP":"";
            historyText(hit,value,.17f,{width/2,0},width,corum::ranked::ui::TextMuted);
        }
        hit->setID("ranked-history-player-name"_spr);
        m_historyProfiles.push_back({WeakRef<CCNode>(hit),player,id,mode});
    }
    bool onHistoryPlayerTap(CCPoint start,CCPoint end) {
        if((m_page!=Page::HistoryDetail&&m_page!=Page::HistoryList) || RankedRuntime::get().spectatorLiveSessionToken().empty())return false;
        if(m_historyReconnect)if(auto popup=m_historyReconnect->lock();popup && popup->getParent())return false;
        auto* scene=CCDirector::sharedDirector()->getRunningScene();
        if(!scene || CCDirector::sharedDirector()->getNextScene() || scene->getChildByIDRecursive("survival-player-info-popup"_spr))return false;
        for(auto const& hit:m_historyProfiles){auto node=hit.node.lock();if(!node || !node->isRunning())continue;
            auto const inside=[&](CCPoint p){auto local=node->convertToNodeSpace(p);auto size=node->getContentSize();return local.x>=0&&local.x<=size.width&&local.y>=0&&local.y<=size.height;};
            if(!inside(start)||!inside(end))continue;
            auto const player=hit.player;auto const match=hit.match;auto const survival=hit.survival;auto const page=m_page;
            auto const session=RankedRuntime::get().spectatorLiveSessionToken();auto const generation=m_historyUiGeneration;
            queueInMainThread([weak=WeakRef(this),player,match,survival,session,generation,page]{auto owner=weak.lock();
                if(!owner || !owner->isRunning() || owner->m_page!=page || generation!=owner->m_historyUiGeneration || session!=RankedRuntime::get().spectatorLiveSessionToken())return;
                if(page==Page::HistoryDetail&&owner->m_historySelectedId!=match)return;
                if(auto* popup=corum::ranked::ui::SurvivalPlayerInfoPopup::createForHistory(player,owner.data(),match,survival))popup->show();
            });return true;
        }
        return false;
    }
    void historyProgress(CCNode* parent,std::optional<double> progress,bool cleared,bool dm,CCPoint center,float width,ccColor3B color) {
        historyText(parent,progress?corum::ranked::rankedScoreText(*progress,dm)+"%":tr("history-unavailable","UNAVAILABLE"),.56f,center,width,color);
        auto* track=CCLayerColor::create({52, 56, 73,255});track->setContentSize({width,5});track->setPosition({center.x-width/2,center.y-22});parent->addChild(track,2);
        auto* fill=CCLayerColor::create({color.r,color.g,color.b,255});fill->setContentSize({width*static_cast<float>(std::clamp(progress.value_or(0.)/100.,0.,1.)),5});track->addChild(fill);
        historyText(parent,!progress?tr("history-no-attempts","NO RECORDED ATTEMPTS"):cleared?tr("history-cleared","CLEARED"):tr("history-not-cleared","NOT CLEARED"),.22f,{center.x,center.y-36},width,cleared?kGreen:corum::ranked::ui::TextMuted);
    }
    bool historyHasThumbnail(corum::ranked::HistoryRound const& round)const {
        return round.levelId>0 && corum::ranked::SurvivalThumbnailProvider::supported();
    }
    void historyMapPanel(CCNode* content,corum::ranked::HistoryRound const& round,CCPoint position,float width,float height) {
        using namespace corum::ranked;
        auto* panel=makeNeonPanel({width,height},position,kSteel,kDeepPanel2,250);content->addChild(panel,2);
        panel->setID("ranked-history-level-panel"_spr);
        historyText(panel,round.title.empty()?tr("history-level-name-unavailable","LEVEL NAME UNAVAILABLE"):round.title,.43f,{width/2,height-21},width-24,ui::Text,true);
        historyText(panel,round.creator.empty()?tr("history-unavailable","UNAVAILABLE"):"by "+round.creator,.27f,{width/2,height-45},width-24,ui::TextMuted);
        auto const earned=round.roundBadge.isObject();
        auto const thumbnail=historyHasThumbnail(round),horizontal=thumbnail&&width>=300&&!earned;
        if(thumbnail){
            auto const thumbWidth=earned?std::min(150.f,width-94):horizontal?150.f:std::min(width-24,150.f),thumbHeight=thumbWidth*9/16;
            auto* frame=makeNeonPanel({thumbWidth,thumbHeight},{earned?(width-74)/2:horizontal?87.f:width/2,height-(earned?108.f:horizontal?111.f:98.f)},kSteel,kDeepPanel,240);
            frame->setID("ranked-history-thumbnail"_spr);panel->addChild(frame,3);
            auto* placeholder=historyText(frame,tr("loading","Loading..."),.24f,{thumbWidth/2,thumbHeight/2},thumbWidth-12,ui::TextMuted);m_historyThumbnailPlaceholder=placeholder;
            auto* sprite=CCSprite::create();sprite->setPosition({thumbWidth/2,thumbHeight/2});frame->addChild(sprite,5);m_historyThumbnail=sprite;
            m_historyThumbnailSize=CCSize{thumbWidth-4,thumbHeight-4};m_historyTexture=nullptr;
            m_historyThumbnailContext={m_historySelectedId,round.key,0,static_cast<std::uint64_t>(round.levelId),++m_historyThumbnailGeneration};
        }
        if(earned)if(auto* badge=ui::survivalRoundBadge(round.roundBadge,{62,62})){badge->setPosition({thumbnail?width-43:width/2,height-108});panel->addChild(badge,5);}
        float const infoLeft=horizontal?174.f:12.f,infoWidth=width-infoLeft-12;
        float const infoTop=earned?height-166:thumbnail&&!horizontal?height-156:height-62;
        MapMetadata metadata;metadata.kind=round.difficulty.find("DEMON")!=std::string::npos?"DEMON":round.difficulty.empty()?"UNKNOWN":"NORMAL";
        if(round.stars>0)metadata.stars=round.stars;
        metadata.featureScore=round.featureScore;metadata.epic=round.epic;
        auto* difficulty=ui::mapDifficultyBadge(round.levelId,metadata,60,25,false,recordedMapDifficulty(round.difficulty));difficulty->setPosition({infoLeft,infoTop-26});panel->addChild(difficulty,5);
        historyText(panel,round.difficulty.empty()?tr("history-unavailable","UNAVAILABLE"):round.difficulty,.30f,{infoLeft+64,infoTop-13},std::max(12.f,infoWidth-64),ui::Text,true,{0,.5f});
        historyText(panel,round.levelId>0?fmt::format("GD #{}",round.levelId):tr("history-level-unavailable","Map information could not be found."),.28f,{infoLeft+infoWidth/2,infoTop-41},infoWidth,kAccent,true);
        historyText(panel,(round.length.empty()?tr("history-unavailable","UNAVAILABLE"):round.length)+(round.stars>0?fmt::format("  ·  {}*",round.stars):""),.27f,{infoLeft+infoWidth/2,infoTop-63},infoWidth,ui::Text);
        historyText(panel,round.grade.empty()?round.label:round.grade,.25f,{infoLeft+infoWidth/2,infoTop-83},infoWidth,round.grade.empty()?ui::TextMuted:kGold,true);
        auto* button=historyButton(panel,tr("history-view-level","VIEW LEVEL"),{width/2,21},std::min(width-24,170.f),menu_selector(CorumRankedLayer::onHistoryLevel),round.levelId);
        if(round.levelId<=0)ui::grayButtonVisual(button);
    }
    void historyOwnPanel(CCNode* parent,corum::ranked::HistoryRound const& round,CCPoint position,float width,float height) {
        auto* panel=makeNeonPanel({width,height},position,historyRoundColor(round),kDeepPanel2,240);parent->addChild(panel,2);
        historyText(panel,tr("history-your-record","YOUR RECORD"),.30f,{width/2,height-18},width-14,kAccent);
        auto const own=std::find_if(round.standings.begin(),round.standings.end(),[](auto const& p){return p.own;});
        if(own==round.standings.end()){historyText(panel,tr("history-not-participant","DID NOT PLAY THIS ROUND"),.23f,{width/2,height/2},width-14,corum::ranked::ui::TextMuted);return;}
        auto const rank=own->rank>0?fmt::format("{}#{}",own->tiedPlayers>1?"= ":"",own->rank):"-";
        historyText(panel,rank,.48f,{width/2,height-46},width-14,kAccent);
        historyProgress(panel,own->progress,own->cleared,round.deathmatch,{width/2,height-81},width-30,own->cleared?kGold:kAccent);
        historyText(panel,fmt::format("{}  {}",own->attempts,tr("history-attempts","ATTEMPTS")),.24f,{width/2,69},width-14);
        historyText(panel,own->qualified?tr("history-qualified","QUALIFIED"):tr("history-eliminated","ELIMINATED"),.25f,{width/2,48},width-14,own->qualified?kGreen:kRed);
        historyText(panel,round.cutoff?fmt::format("{} {}%",tr("history-cutoff-gap","CUTOFF GAP"),corum::ranked::rankedScoreText((round.deathmatch?own->progress:std::trunc(own->progress))-*round.cutoff,round.deathmatch,true)):"",.22f,{width/2,28},width-14,corum::ranked::ui::TextMuted);
    }
    // H2 left column (.panel, 300 board px): thumbnail or difficulty face,
    // round badge, map name, rate with creator and cutoff, YOUR RECORD and
    // VIEW LEVEL. The record lines come from the caller.
    void historyMapColumn(corum::ranked::HistoryRound const& round,float top,float bottom,std::string const& recordValue,ccColor3B recordTone,std::string const& recordDetail) {
        using namespace corum::ranked;
        auto const left=px(28.0f);
        constexpr float panelW=150.0f;
        auto* panel=makeNeonPanel({panelW,top-bottom},{left+panelW/2.0f,(top+bottom)/2.0f},kDeepPanel,kDeepPanel,255);m_root->addChild(panel,2);
        panel->setID("ranked-history-level-panel"_spr);
        constexpr float thumbH=75.0f;
        auto const thumbY=top-thumbH/2.0f;
        if(historyHasThumbnail(round)){
            auto* frame=makeNeonPanel({panelW,thumbH},{left+panelW/2.0f,thumbY},kDeepPanel,ccc3(32,35,46),255);frame->setID("ranked-history-thumbnail"_spr);m_root->addChild(frame,3);
            auto* placeholder=historyText(frame,tr("loading","Loading..."),.22f,{panelW/2.0f,thumbH/2.0f},panelW-12.0f,ui::TextMuted);m_historyThumbnailPlaceholder=placeholder;
            auto* sprite=CCSprite::create();sprite->setPosition({panelW/2.0f,thumbH/2.0f});frame->addChild(sprite,5);m_historyThumbnail=sprite;
            m_historyThumbnailSize=CCSize{panelW-2.0f,thumbH-2.0f};m_historyTexture=nullptr;
            m_historyThumbnailContext={m_historySelectedId,round.key,0,static_cast<std::uint64_t>(round.levelId),++m_historyThumbnailGeneration};
        }else{
            auto* frame=CCLayerColor::create({32,35,46,255},panelW,thumbH);frame->setPosition({left,top-thumbH});m_root->addChild(frame,3);
        }
        MapMetadata metadata;metadata.kind=round.difficulty.find("DEMON")!=std::string::npos?"DEMON":round.difficulty.empty()?"UNKNOWN":"NORMAL";
        if(round.stars>0)metadata.stars=round.stars;
        metadata.featureScore=round.featureScore;metadata.epic=round.epic;
        if(!historyHasThumbnail(round)){auto* face=ui::mapDifficultyBadge(round.levelId,metadata,60,25,false,recordedMapDifficulty(round.difficulty));face->setPosition({left+panelW/2.0f-30.0f,thumbY-12.0f});m_root->addChild(face,5);}
        if(round.roundBadge.isObject())if(auto* badge=ui::survivalRoundBadge(round.roundBadge,{40,40},false)){badge->setScale(0.7f);badge->setPosition({left+panelW-18.0f,top-18.0f});m_root->addChild(badge,6);}
        float y=top-thumbH-14.0f;
        auto* name=makeLabel(round.title.empty()?tr("history-level-name-unavailable","LEVEL NAME UNAVAILABLE"):round.title,fontScale(24.0f),{left+8.0f,y},ui::Text,"black",{0.0f,0.5f});
        ui::fitMenuLabelWidth(name,panelW-16.0f,fontScale(24.0f),0.12f);m_root->addChild(name,5);
        y-=16.0f;
        auto* rate=ui::mapDifficultyBadge(round.levelId,metadata,34,12,true,recordedMapDifficulty(round.difficulty));rate->setPosition({left+8.0f,y-6.0f});m_root->addChild(rate,5);
        auto const cutoffText=round.target>0?fmt::format(fmt::runtime(tr("history-cutoff-players","CUTOFF {}")),round.target):std::string();
        auto* by=makeLabel(fmt::format("{}{}",round.creator.empty()?std::string():"by "+round.creator,cutoffText.empty()?"":" · "+cutoffText),fontScale(13.0f),{left+46.0f,y},
            ui::TextMuted,"semibold",{0.0f,0.5f});
        ui::fitMenuLabelWidth(by,panelW-54.0f,fontScale(13.0f),0.08f);m_root->addChild(by,5);
        // YOUR RECORD (#20232E): the lime progress and place, attempts and the gap to first.
        y-=14.0f;
        constexpr float recordH=42.0f;
        auto* record=makeNeonPanel({panelW-16.0f,recordH},{left+panelW/2.0f,y-recordH/2.0f},historyRoundColor(round),ccc3(32,35,46),255);m_root->addChild(record,4);
        m_root->addChild(makeLabel(tr("history-your-record","YOUR RECORD"),fontScale(13.0f),{left+14.0f,y-8.0f},ui::TextMuted,"semibold",{0.0f,0.5f}),6);
        auto* value=makeLabel(recordValue,fontScale(recordDetail.empty()?15.0f:24.0f),{left+14.0f,y-21.0f},recordTone,recordDetail.empty()?"extrabold":"black",{0.0f,0.5f});
        ui::fitMenuLabelWidth(value,panelW-28.0f,fontScale(24.0f),0.12f);m_root->addChild(value,6);
        if(!recordDetail.empty()){
            auto* detail=makeLabel(recordDetail,fontScale(13.0f),{left+14.0f,y-34.0f},ui::TextMuted,"semibold",{0.0f,0.5f});
            ui::fitMenuLabelWidth(detail,panelW-28.0f,fontScale(13.0f),0.08f);m_root->addChild(detail,6);
        }
        auto* view=historyButton(m_root,tr("history-view-level","VIEW LEVEL"),{left+panelW/2.0f,bottom+14.0f},panelW-16.0f,menu_selector(CorumRankedLayer::onHistoryLevel),round.levelId);
        if(round.levelId<=0)ui::grayButtonVisual(view);
    }
    // Solo round: YOU and the opponent as two table rows (name, tier before,
    // best progress, cleared, round result), then the series panel.
    void historySoloBoard(HistoryMatchView const& item,corum::ranked::HistoryRound const& round,CCSize size) {
        using namespace corum::ranked;
        auto const top=size.height-kLobbyHeader-22.0f-6.0f,bottom=40.0f,left=px(28.0f),right=size.width-px(28.0f);
        auto const ownB=item.side=="B";
        auto const ownBest=ownB?round.bestB:round.bestA,otherBest=ownB?round.bestA:round.bestB;
        auto const ownCleared=ownB?round.clearedB:round.clearedA;
        auto const outcome=round.deathmatch?round.winnerSide:round.result;
        auto const tied=outcome.empty()||outcome=="DRAW",won=!tied&&outcome==item.side;
        historyMapColumn(round,top,bottom,ownBest?rankedScoreText(*ownBest,round.deathmatch)+"%":tr("history-unavailable","UNAVAILABLE"),
            tied?kGold:won?ui::Accent:kRed,ownCleared?tr("history-cleared","CLEARED"):tr("history-not-cleared","NOT CLEARED"));
        constexpr float panelW=150.0f;
        auto const tableX=left+panelW+9.0f,tableW=right-tableX;
        auto const colName=tableX+10.0f,colTier=right-150.0f,colRecord=right-80.0f,colResult=right-26.0f;
        auto const headY=top-6.0f;
        auto const head=[&](std::string const& text,float x,CCPoint anchor){m_root->addChild(makeLabel(text,fontScale(13.0f),{x,headY},ui::TextMuted,"extrabold",anchor),5);};
        head(tr("leaderboard-player","PLAYER"),colName,{0.0f,0.5f});head(tr("leaderboard-tier","TIER"),colTier,{0.0f,0.5f});
        head(tr("history-record","RECORD"),colRecord,{1.0f,0.5f});head(tr("result","RESULT"),colResult,{0.5f,0.5f});
        constexpr float rowH=30.0f;
        // The rows sit in a tap scroll so a name opens the public profile.
        auto const viewportH=2.0f*rowH+3.0f,viewportTop=headY-9.0f;
        auto* scroll=ui::createRankedTapScroll({tableW,viewportH},[weak=WeakRef(this)](CCPoint start,CCPoint end){if(auto owner=weak.lock())owner->onHistoryPlayerTap(start,end);});
        m_historyBoardScroll=scroll;scroll->setID("ranked-history-board-scroll"_spr);scroll->setPosition({tableX,viewportTop-viewportH});m_root->addChild(scroll,3);
        auto* content=scroll->m_contentLayer;content->setContentSize({tableW,viewportH});
        float rowY=viewportH-rowH/2.0f;
        for(auto const* side:{item.side=="B"?"B":"A",item.side=="B"?"A":"B"}){
            auto const own=std::string(side)==item.side;
            auto const person=item.record["players"][side];SurvivalStanding player;
            player.id=side;player.name=person["name"].asString().unwrapOr("");player.bot=person["isBot"].asBool().unwrapOr(false);
            player.accountId=player.bot?0:numFromString<int>(person["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
            player.tier=item.record["profileBefore"][side]["displayedTier"].asString().unwrapOr("");
            if(player.name.empty())player.name=own?tr("you","YOU"):item.opponentName;
            auto const fill=own?ccc3(32,38,42):ui::Surface;
            auto* stripe=CCLayerColor::create({fill.r,fill.g,fill.b,255},tableW,rowH);stripe->setPosition({0.0f,rowY-rowH/2.0f});content->addChild(stripe,1);
            if(own){auto* edge=CCLayerColor::create({ui::Accent.r,ui::Accent.g,ui::Accent.b,255},2.0f,rowH);edge->setPosition({0.0f,rowY-rowH/2.0f});content->addChild(edge,2);}
            // The name keeps the public-profile tap (title and LP under it).
            historyPlayerName(content,player,{colName-tableX+(colTier-colName-10.0f)/2.0f,rowY},colTier-colName-10.0f,own?ui::Accent:ui::Text,item.matchId,false);
            auto const tier=upper(player.tier);
            if(!tier.empty()){auto* tierLabel=makeLabel(rankTierDisplay(tier),fontScale(13.0f),{colTier-tableX,rowY},tierColor(tier),"black",{0.0f,0.5f});
                ui::fitMenuLabelWidth(tierLabel,60.0f,fontScale(13.0f),0.08f);content->addChild(tierLabel,5);}
            auto const best=own?ownBest:otherBest;auto const cleared=own?ownCleared:(ownB?round.clearedA:round.clearedB);
            content->addChild(makeLabel(best?rankedScoreText(*best,round.deathmatch)+"%":std::string("-"),fontScale(20.0f),{colRecord-tableX,rowY+3.0f},cleared?kGold:ui::Text,"black",{1.0f,0.5f}),5);
            content->addChild(makeLabel(cleared?tr("history-cleared","CLEARED"):tr("history-not-cleared","NOT CLEARED"),fontScale(12.0f),{colRecord-tableX,rowY-9.0f},ui::TextMuted,"semibold",{1.0f,0.5f}),5);
            auto const rowWon=!tied&&outcome==side;
            auto const result=tied?tr("tie","TIE"):rowWon?tr("win","WIN"):tr("lose","LOSE");
            auto* probe=makeLabel(result,fontScale(12.0f),{},ui::Text,"black");auto const chipW=probe->getScaledContentSize().width+px(16.0f);
            addChip(content,result,{colResult-tableX-chipW/2.0f,rowY},tied?ccc3(32,35,46):rowWon?ui::AccentFill:kRed,tied?ui::TextMuted:ui::AccentInk,12.0f);
            rowY-=rowH+3.0f;
        }
        scroll->scrollToTop();
        rowY=viewportTop-viewportH-rowH/2.0f+3.0f;
        // Series panel: the series score, this round's verdict and the DM link.
        auto const panelTop=rowY+rowH/2.0f-6.0f,panelH=std::min(70.0f,panelTop-bottom);
        m_root->addChild(makeNeonPanel({tableW,panelH},{tableX+tableW/2.0f,panelTop-panelH/2.0f},tied?kGold:won?kAccent:kRed,kDeepPanel,255),2);
        auto const ownScore=ownB?item.roundWinsB:item.roundWinsA,otherScore=ownB?item.roundWinsA:item.roundWinsB;
        m_root->addChild(makeLabel(tr("series","SERIES"),fontScale(13.0f),{tableX+10.0f,panelTop-12.0f},ui::TextMuted,"extrabold",{0.0f,0.5f}),5);
        auto* series=makeLabel(fmt::format("{} : {}",ownScore,otherScore),fontScale(34.0f),{tableX+10.0f,panelTop-32.0f},ui::Text,"black",{0.0f,0.5f});
        ui::leanLabel(series);m_root->addChild(series,5);
        auto* verdict=makeLabel(tied?tr("tie","TIE"):won?tr("history-your-round-win","YOU WIN THIS ROUND"):tr("history-rival-round-win","RIVAL WINS THIS ROUND"),fontScale(16.0f),
            {right-10.0f,panelTop-22.0f},tied?kGold:won?ui::Accent:kRed,"black",{1.0f,0.5f});
        ui::fitMenuLabelWidth(verdict,tableW*0.6f,fontScale(16.0f),0.1f);m_root->addChild(verdict,5);
        if(round.nextDeathmatch>0)m_root->addChild(makeLabel(fmt::format("{} DM {}",tr("history-continued","CONTINUED IN"),round.nextDeathmatch),fontScale(13.0f),
            {right-10.0f,panelTop-38.0f},ui::TextMuted,"semibold",{1.0f,0.5f}),5);
    }
    // A round without a confirmed result: the map column and the note.
    void historyPendingBoard(HistoryMatchView const& item,corum::ranked::HistoryRound const& round,CCSize size) {
        using namespace corum::ranked;
        auto const top=size.height-kLobbyHeader-22.0f-6.0f,bottom=40.0f,left=px(28.0f),right=size.width-px(28.0f);
        historyMapColumn(round,top,bottom,item.inProgress?tr("history-in-progress","MATCH IN PROGRESS"):tr("history-unavailable","UNAVAILABLE"),kGold,"");
        constexpr float panelW=150.0f;
        auto const tableX=left+panelW+9.0f,tableW=right-tableX;
        m_root->addChild(makeNeonPanel({tableW,58.0f},{tableX+tableW/2.0f,top-29.0f},kGold,kDeepPanel,255),2);
        auto* line=makeLabel(item.inProgress?tr("history-round-live","This round is still in progress."):tr("history-round-unfinished","This round has no confirmed result."),
            fontScale(18.0f),{tableX+10.0f,top-20.0f},ui::Text,"black",{0.0f,0.5f});
        ui::fitMenuLabelWidth(line,tableW-20.0f,fontScale(18.0f),0.1f);m_root->addChild(line,5);
        auto* help=makeLabel(item.inProgress?tr("history-round-live-help","Final records and rankings appear after the round ends."):tr("history-unavailable","UNAVAILABLE"),
            fontScale(13.0f),{tableX+10.0f,top-40.0f},ui::TextMuted,"semibold",{0.0f,0.5f});
        ui::fitMenuLabelWidth(help,tableW-20.0f,fontScale(13.0f),0.08f);m_root->addChild(help,5);
    }
    void historySurvivalBoard(corum::ranked::HistoryRound const& round,CCSize size) {
        using namespace corum::ranked;
        auto const top=size.height-kLobbyHeader-22.0f-6.0f,bottom=40.0f,left=px(28.0f),right=size.width-px(28.0f);
        auto const own=std::find_if(round.standings.begin(),round.standings.end(),[](auto const& p){return p.own;});
        std::string recordValue=tr("history-not-participant","DID NOT PLAY THIS ROUND"),recordDetail;ccColor3B recordTone=ui::TextMuted;
        if(own!=round.standings.end()){
            auto const place=own->rank>0?fmt::format("{}#{}",own->tiedPlayers>1?"=":"",own->rank):std::string("-");
            recordValue=fmt::format("{}% · {}",rankedScoreText(own->progress,round.deathmatch),place);recordTone=own->qualified?ui::Accent:kRed;
            std::string gap;
            if(!round.standings.empty()&&round.standings.front().rank==1&&round.standings.front().id!=own->id)
                gap=fmt::format(" · {} {}%p",tr("history-gap-first","TO 1ST"),rankedScoreText(own->progress-round.standings.front().progress,round.deathmatch,true));
            recordDetail=fmt::format("{} {}{}",own->attempts,tr("history-attempts","ATTEMPTS"),gap);
        }
        historyMapColumn(round,top,bottom,recordValue,recordTone,recordDetail);
        constexpr float panelW=150.0f;
        // Right: # / PLAYER / RECORD / ATTEMPTS / RESULT (.th, .tr; own row .tr.me).
        auto const tableX=left+panelW+9.0f,tableW=right-tableX;
        auto const colRank=tableX+8.0f,colIcon=tableX+30.0f,colName=tableX+42.0f,colResult=right-24.0f,colAttempts=right-58.0f,colRecord=right-90.0f;
        auto const headY=top-6.0f;
        auto const head=[&](std::string const& text,float x,CCPoint anchor){m_root->addChild(makeLabel(text,fontScale(13.0f),{x,headY},ui::TextMuted,"extrabold",anchor),5);};
        head("#",colRank,{0.0f,0.5f});head(tr("leaderboard-player","PLAYER"),colName,{0.0f,0.5f});head(tr("history-record","RECORD"),colRecord,{1.0f,0.5f});
        head(tr("history-attempts","ATTEMPTS"),colAttempts,{1.0f,0.5f});head(tr("result","RESULT"),colResult,{0.5f,0.5f});
        constexpr float rowH=23.0f,rowGap=2.5f,noteH=24.0f;
        auto const viewTop=headY-9.0f,viewBottom=bottom+noteH+4.0f;
        auto const viewport=CCSize{tableW,std::max(rowH,viewTop-viewBottom)};
        auto* scroll=ui::createRankedTapScroll(viewport,[weak=WeakRef(this)](CCPoint start,CCPoint end){if(auto owner=weak.lock())owner->onHistoryPlayerTap(start,end);});
        m_historyBoardScroll=scroll;scroll->setID("ranked-history-board-scroll"_spr);scroll->setPosition({tableX,viewBottom});m_root->addChild(scroll,3);
        auto* content=scroll->m_contentLayer;
        auto const contentH=std::max(viewport.height,static_cast<float>(round.standings.size())*(rowH+rowGap));content->setContentSize({tableW,contentH});
        float rowY=contentH-rowH/2.0f;
        for(auto const& p:round.standings){
            auto const fill=p.own?ccc3(32,38,42):ui::Surface;
            auto* stripe=CCLayerColor::create({fill.r,fill.g,fill.b,255},tableW,rowH);stripe->setPosition({0.0f,rowY-rowH/2.0f});content->addChild(stripe,1);
            if(p.own){auto* edge=CCLayerColor::create({ui::Accent.r,ui::Accent.g,ui::Accent.b,255},2.0f,rowH);edge->setPosition({0.0f,rowY-rowH/2.0f});content->addChild(edge,2);}
            auto const rankTone=p.own?ui::Accent:p.rank==1?kGold:p.rank==2?ccc3(201,215,235):p.rank==3?ccc3(208,137,70):ui::TextMuted;
            content->addChild(makeLabel(p.rank>0?fmt::format("{}{}",p.tiedPlayers>1?"=":"",p.rank):std::string("-"),fontScale(20.0f),{colRank-tableX,rowY},rankTone,"black",{0.0f,0.5f}),4);
            SurvivalStanding player;player.id=p.id;player.name=p.name;player.rank=p.rank;player.bot=p.bot;player.accountId=p.accountId;player.tier=p.tier;
            auto* iconHolder=CCNode::create();iconHolder->setPosition({colIcon-tableX,rowY});iconHolder->setScale(0.62f);content->addChild(iconHolder,5);m_historyIcons.push_back({WeakRef<CCNode>(iconHolder),player});
            auto const nameWidth=colRecord-colName-50.0f;
            auto* hit=CCNode::create();hit->setContentSize({nameWidth,rowH});hit->setPosition({colName-tableX,rowY-rowH/2.0f});content->addChild(hit,6);
            auto* nameLabel=ui::playerNameLabel(p.name,fontScale(16.0f),{0.0f,rowH/2.0f},p.own?ui::Accent:ui::Text,ui::FontWeight::ExtraBold);nameLabel->setAnchorPoint({0.0f,0.5f});
            ui::fitSingleLineMenuLabel(nameLabel,nameWidth,14.0f,fontScale(16.0f));hit->addChild(nameLabel,5);hit->setID("ranked-history-player-name"_spr);
            m_historyProfiles.push_back({WeakRef<CCNode>(hit),player,m_historySelectedId,true});
            content->addChild(makeLabel(rankedScoreText(p.progress,round.deathmatch)+"%",fontScale(16.0f),{colRecord-tableX,rowY},p.cleared?kGold:ui::Text,"black",{1.0f,0.5f}),4);
            content->addChild(makeLabel(std::to_string(p.attempts),fontScale(15.0f),{colAttempts-tableX,rowY},ui::TextMuted,"extrabold",{1.0f,0.5f}),4);
            auto const result=p.forfeited?tr("history-forfeit","FORFEIT"):p.qualified?tr("history-qualified","QUALIFIED"):tr("history-eliminated","ELIMINATED");
            auto const chipFill=p.forfeited?ccc3(32,35,46):p.qualified?ui::AccentFill:kRed;
            auto* chipProbe=makeLabel(result,fontScale(12.0f),{},ui::Text,"black");auto const chipW=chipProbe->getScaledContentSize().width+px(16.0f);
            addChip(content,result,{colResult-tableX-chipW/2.0f,rowY},chipFill,p.forfeited?ui::TextMuted:ui::AccentInk,12.0f);
            if(p.cutoffBoundary){auto* line=CCLayerColor::create({kGold.r,kGold.g,kGold.b,220},tableW,1.0f);line->setPosition({0.0f,rowY-rowH/2.0f-rowGap/2.0f});content->addChild(line,3);}
            rowY-=rowH+rowGap;
        }
        scroll->scrollToTop();
        // Note panel: cutoff / qualified on the left, the tap hint on the right.
        m_root->addChild(makeNeonPanel({tableW,noteH},{tableX+tableW/2.0f,bottom+noteH/2.0f},kDeepPanel,kDeepPanel,255),2);
        auto const cutoff=round.cutoff?rankedScoreText(*round.cutoff,round.deathmatch)+"%":std::string("-");
        auto* note=makeLabel(fmt::format("{} {} · {} {}/{}",tr("history-cutoff","CUTOFF"),cutoff,tr("history-qualified","QUALIFIED"),round.qualifiedCount,round.target),fontScale(13.0f),
            {tableX+8.0f,bottom+noteH/2.0f},ui::TextMuted,"semibold",{0.0f,0.5f});
        ui::fitMenuLabelWidth(note,tableW*0.55f,fontScale(13.0f),0.08f);m_root->addChild(note,4);
        auto* hint=makeLabel(tr("history-tap-player","TAP A PLAYER FOR INFO"),fontScale(13.0f),{right-8.0f,bottom+noteH/2.0f},ui::TextMuted,"semibold",{1.0f,0.5f});
        ui::fitMenuLabelWidth(hint,tableW*0.4f,fontScale(13.0f),0.08f);m_root->addChild(hint,4);
    }
    void renderHistoryDetail(RuntimeView const& view) {
        auto const size=CCDirector::sharedDirector()->getWinSize();addTopBack(false);
        if(m_historyIndex>=view.history.size()){m_page=Page::HistoryList;renderHistoryList(view);return;}
        auto const& item=view.history[m_historyIndex];auto const survival=item.mode==corum::ranked::RankedMode::Survival;
        // H2 header: back, the 24 px leaning brand, the result chip and the
        // date / field caption.
        {   auto const headY=size.height-kLobbyHeader/2.0f;
            auto* brand=makeLabel(survival?tr("history-survival-title","SURVIVAL MATCH HISTORY"):tr("history-solo-title","SOLO RANKED / MATCH HISTORY"),fontScale(24.0f),
                {px(28.0f)+24.0f+8.0f,headY},corum::ranked::ui::Text,"black",{0.0f,0.5f});
            corum::ranked::ui::leanLabel(brand);corum::ranked::ui::fitMenuLabelWidth(brand,size.width*0.36f,fontScale(24.0f),0.16f);brand->setID("ranked-screen-title"_spr);m_root->addChild(brand,6);
            auto const chipX=brand->getPositionX()+brand->getScaledContentSize().width+10.0f;
            auto const tone=historyColor(item);
            addChip(m_root,historyOutcome(item),{chipX,headY},tone,corum::ranked::ui::AccentInk,12.0f);
            auto const when=item.finishedAt.empty()?item.createdAt:item.finishedAt;
            auto const date=when.size()>=16?when.substr(0,10)+" "+when.substr(11,5):when;
            auto const field=survival?fmt::format(fmt::runtime(tr("players-count-format","{} PLAYERS")),item.record["participantCount"].asInt().unwrapOr(0)):historyDelta(item);
            auto* caption=makeLabel(fmt::format("{} · {}",date,field),fontScale(13.0f),{chipX+makeLabel(historyOutcome(item),fontScale(12.0f),{},corum::ranked::ui::AccentInk,"black")->getScaledContentSize().width+px(16.0f)+10.0f,headY},
                corum::ranked::ui::TextMuted,"semibold",{0.0f,0.5f});
            corum::ranked::ui::fitMenuLabelWidth(caption,size.width*0.3f,fontScale(13.0f),0.09f);m_root->addChild(caption,6);
        }
        historyButton(m_root,tr("refresh","REFRESH"),{size.width/2,22},90,menu_selector(CorumRankedLayer::onHistoryRefresh));
        if(view.historyDetailLoading&&!item.detailsLoaded){auto* spinner=LoadingSpinner::create(40);spinner->setPosition({size.width/2,size.height/2});m_root->addChild(spinner,3);return;}
        if(!view.historyDetailError.empty()){
            historyText(m_root,view.historyDetailError,.25f,{size.width/2,size.height-80},size.width-80,kRed);
            if(!item.detailsLoaded)return;
        }
        auto const rounds=corum::ranked::historyRounds(item.record,survival);
        if(rounds.empty()){historyText(m_root,tr("history-no-rounds","NO RECORDED ROUNDS"),.3f,{size.width/2,size.height/2},size.width-60,corum::ranked::ui::TextMuted);return;}
        auto selected=std::find_if(rounds.begin(),rounds.end(),[&](auto const& r){return r.key==m_historyRound;});
        if(selected==rounds.end()){m_historyRound=rounds.front().key;selected=rounds.begin();}
        auto const& round=*selected;
        if(survival && round.completed){
            auto width=std::min(140.f,(size.width-120)/2);
            if(auto* button=corum::ranked::replay::HistoryReplayButton::create(round.replayIdentity,width)){button->setPosition({width/2+8,18});m_root->addChild(button,12);}
        }
        auto const layout=corum::ranked::historyLayout(size.width,size.height);
        auto const tabs=std::max(1,static_cast<int>((layout.width-68)/54));auto const pages=(static_cast<int>(rounds.size())+tabs-1)/tabs;m_historyTabPage=std::clamp(m_historyTabPage,0,pages-1);
        // H2 line tabs (.tabs): a 2 px base line, the selected tab in white
        // with the lime underline; Survival rounds keep their result colour
        // as a short mark under the label.
        auto const tabY=size.height-kLobbyHeader-11.0f;
        {   auto* base=CCLayerColor::create({corum::ranked::ui::SurfaceRaised.r,corum::ranked::ui::SurfaceRaised.g,corum::ranked::ui::SurfaceRaised.b,255},layout.width,1.0f);
            base->setPosition({(size.width-layout.width)/2,tabY-11.0f});m_root->addChild(base,4);}
        for(int i=m_historyTabPage*tabs;i<std::min(static_cast<int>(rounds.size()),(m_historyTabPage+1)*tabs);++i)
            historyTab(rounds[i].label,{(size.width-layout.width)/2+50+(i%tabs)*54.f,tabY},49,menu_selector(CorumRankedLayer::onHistoryRound),i,rounds[i].key==m_historyRound,survival?std::optional<ccColor3B>{historyRoundColor(rounds[i])}:std::nullopt);
        if(pages>1){historyButton(m_root,"<",{(size.width-layout.width)/2+12,tabY},24,menu_selector(CorumRankedLayer::onHistoryRoundPage),-1);historyButton(m_root,">",{(size.width+layout.width)/2-12,tabY},24,menu_selector(CorumRankedLayer::onHistoryRoundPage),1);}
        // H2 board body, two columns: the map / own-record column on the left;
        // the round table (Survival), the versus rows (Solo) or the pending
        // note on the right.
        if(!round.completed){historyPendingBoard(item,round,size);return;}
        if(survival){historySurvivalBoard(round,size);return;}
        historySoloBoard(item,round,size);
    }
    void updateHistoryIcons() {
        auto* scroll=m_historyBoardScroll;if(!scroll)return;
        auto const viewport=scroll->getContentSize();
        for(auto& row:m_historyIcons){auto holder=row.holder.lock();if(!holder)continue;
            auto const at=scroll->convertToNodeSpace(holder->convertToWorldSpace(CCPointZero));
            auto const visible=at.y+20>=0&&at.y-20<=viewport.height;
            auto* icon=holder->getChildByID("history-player-icon");
            if(!visible){if(icon)icon->removeFromParentAndCleanup(true);continue;}
            if(icon)continue;
            auto const& p=row.player;
            auto* value=p.bot?corum::ranked::ui::PlayerIcon::createBot(p.rank):p.accountId>0&&p.accountId==GJAccountManager::sharedState()->m_accountID?corum::ranked::ui::PlayerIcon::createLocal():corum::ranked::ui::PlayerIcon::createRemote(p.accountId);
            if(value){value->setID("history-player-icon");value->setScale(.62f);holder->addChild(value,5);}
        }
    }
    void updateHistoryThumbnail() {
        if(m_page!=Page::HistoryDetail || !m_historyThumbnail || !m_historyThumbnailContext.levelId)return;
        auto& provider=corum::ranked::SurvivalThumbnailProvider::history();provider.activate(m_historyThumbnailContext);
        auto texture=provider.selected();if(!texture || texture==m_historyTexture)return;
        m_historyTexture=texture;auto const size=texture->getContentSize();if(size.width<=0||size.height<=0)return;
        m_historyThumbnail->setTexture(texture.data());m_historyThumbnail->setTextureRect({0,0,size.width,size.height});
        m_historyThumbnail->setScale(std::min(m_historyThumbnailSize.width/size.width,m_historyThumbnailSize.height/size.height));
        if(m_historyThumbnailPlaceholder)m_historyThumbnailPlaceholder->setVisible(false);
    }

    bool isPlayableLevel(GJGameLevel* level) const {
        if (!level) return false;
        if (static_cast<int>(level->m_levelID) != RankedRuntime::get().currentLevelId()) return false;
        // getSavedLevel() may return only list metadata. PlayLayer needs the actual
        // downloaded level string; entering on metadata alone is what caused the
        // repeated "Load Failed" screen in alpha.11.
        return level->m_levelType == GJLevelType::Saved && !level->m_levelString.empty() && !level->m_levelNotDownloaded;
    }

    GJGameLevel* findPlayableMap() const {
        auto* level = findLevelInfoMap();
        return isPlayableLevel(level) ? level : nullptr;
    }

    bool exactLevelLookupStateAllowed() const {
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) return false;
        auto const& state = view.match.state;
        return state == "ROUND_PREPARE" || state == "DEATHMATCH_PREPARE" ||
            state == "ROUND_ENTERING" || state == "DEATHMATCH_ENTERING" ||
            state == "MAP_PREPARE" || state == "ROUND_PLAYING" ||
            state == "FINAL_ATTEMPT_WINDOW" || state == "LAST_ATTEMPT_WINDOW" ||
            state == "ROUND_SETTLING" || state == "DEATHMATCH_PLAYING" ||
            state == "MAP_PLAYING";
    }

    void beginExactLevelLookup() {
        if (!exactLevelLookupStateAllowed()) return;
        auto& preparation = corum::ranked::RankedLevelPreparation::get();
        preparation.poll();
        m_levelLookupBusy = preparation.busy();
        m_levelLookupError = preparation.error();
        if (m_levelLookupStartedAt == SteadyClock::time_point{}) {
            m_levelLookupStartedAt = SteadyClock::now();
        }
    }

    GJGameLevel* findLevelInfoMap() const {
        // Never substitute a raw GD saved pointer for attested online metadata.
        return corum::ranked::RankedLevelPreparation::get().level();
    }

    float preparationProgressTarget() {
        auto const& preparation = corum::ranked::RankedRoundPreparation::get().view();
        if (!preparation.mapReady) return 0.0f; // native map endpoint has no byte progress
        return (1.0f + preparation.audioProgress * (preparation.songsTotal + preparation.sfxTotal)) /
            (1.0f + preparation.songsTotal + preparation.sfxTotal);
    }

    std::string preparationStageText() {
        auto const& preparation = corum::ranked::RankedRoundPreparation::get().view();
        if (!preparation.error.empty()) {
            if (preparation.error.find("WAITING") != std::string::npos)
                return tr("waiting-download-response", "WAITING FOR DOWNLOAD...");
            if (preparation.error.find("RETRYING") != std::string::npos)
                return tr("retrying", "RETRYING");
            return tr("download-failed", "DOWNLOAD FAILED");
        }
        if (!preparation.mapReady) return tr("downloading-map", "DOWNLOADING MAP...");
        if (preparation.audioReady) return tr("preparation-ready", "PREPARATION READY");
        return fmt::format("{}  {}/{}", tr("downloading-audio", "DOWNLOADING AUDIO..."),
            preparation.songsDone + preparation.sfxDone, preparation.songsTotal + preparation.sfxTotal);
    }

    void addPreparationStatusRows(CCPoint mapPosition, CCPoint audioPosition, float width,
        bool clearSyncPending = false) {
        auto const& preparation = corum::ranked::RankedRoundPreparation::get().view();
        auto const mapReady = preparation.mapReady && findPlayableMap() != nullptr;
        auto const audioReady = mapReady && preparation.audioReady;
        auto const failed = !preparation.error.empty() &&
            preparation.error.find("WAITING") == std::string::npos &&
            preparation.error.find("RETRYING") == std::string::npos;
        auto addStatus = [&](std::string const& text, CCPoint position, ccColor3B color, char const* id) {
            // Passive Corum plate: no CCMenuItem or native download selector.
            auto* plate = makeTextPlate(text, {width, 23.0f}, position, color, 0.23f, color, "semibold");
            plate->setID(id);
            m_root->addChild(plate, 3);
        };
        auto mapText = clearSyncPending ? tr("clear-recorded", "CLEAR RECORDED")
            : (mapReady ? tr("map-ready", "MAP READY")
                : (failed ? tr("download-failed", "DOWNLOAD FAILED") : tr("downloading", "DOWNLOADING...")));
        auto audioText = clearSyncPending ? tr("result-sync", "RESULT SYNC")
            : (audioReady ? tr("audio-ready", "AUDIO READY")
                : (failed && mapReady ? tr("download-failed", "DOWNLOAD FAILED") : tr("downloading", "DOWNLOADING...")));
        addStatus("01  " + mapText, mapPosition,
            clearSyncPending || mapReady ? kGreen : (failed ? kRed : kAccent), "ranked-map-download-status"_spr);
        addStatus("02  " + audioText, audioPosition,
            audioReady ? kGreen : (failed && mapReady ? kRed : kAccent), "ranked-audio-download-status"_spr);
    }

    std::vector<int> collectSongIds(GJGameLevel* level) const {
        std::vector<int> result;
        if (!level) return result;

        auto add = [&result](int id) {
            if (id <= 0) return;
            if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        };
        add(static_cast<int>(level->m_songID));

        // GD 2.2 stores every custom song referenced by the downloaded level in
        // m_songIDs as a comma-separated list. Do not only check m_songID.
        std::stringstream stream(std::string(level->m_songIDs.c_str()));
        std::string token;
        while (std::getline(stream, token, ',')) {
            try {
                add(std::stoi(token));
            } catch (...) {
                // Ignore malformed/empty entries. The primary m_songID is still
                // handled above and the game remains playable without song audio.
            }
        }
        return result;
    }

    void refreshSongIds(GJGameLevel* level) {
        auto ids = collectSongIds(level);
        if (ids == m_songIds) return;
        m_songIds = std::move(ids);
    }

    bool songIdReady(MusicDownloadManager* manager, int id) const {
        return corum::ranked::rankedSongResourceReady(manager, id);
    }

    bool isSongReady(GJGameLevel* level) {
        return level && corum::ranked::RankedRoundPreparation::get().view().audioReady;
    }

    void updateAutomation() {
        auto& runtime = RankedRuntime::get();
        auto const& view = runtime.view();
        if (view.stage != RuntimeStage::Matched) return;
        auto const& match = view.match;
        if (match.state == "MATCHED") {
            if (!m_matchFoundReadySent && phaseSeconds() >= 5.0) {
                m_matchFoundReadySent = true;
                runtime.submitReady();
            }
            return;
        }
        if (!corum::ranked::roundAcquireState(match.state)) return;
        auto& preparation = corum::ranked::RankedRoundPreparation::get();
        preparation.pump();
        m_levelLookupBusy = corum::ranked::RankedLevelPreparation::get().busy();
        m_levelLookupError = preparation.view().error;
        bool ownReady = match.side == "A" ? match.readyA : match.readyB;
        // deadlineAt is the server's resource safety budget (at least 60s),
        // not the 10s/5s visual countdown. Converting it with 10 - remaining
        // delayed READY until expiry and raced ROUND_1_MAP_DOWNLOAD_TIMEOUT.
        if (!ownReady && corum::ranked::roundReadyDue(match.state, match.placement, phaseSeconds(), preparation.ready())) {
            auto now = SteadyClock::now();
            if (m_lastReadySubmitAt == SteadyClock::time_point{} || now - m_lastReadySubmitAt >= std::chrono::seconds(1)) {
                m_lastReadySubmitAt = now;
                log::info("[RankedPrepare] auto-ready context={} phase={} resourcesReady=true", m_resourceKey, phaseSeconds());
                runtime.submitReady();
            }
        }
    }

    void maybeEnterLevel() {
        auto const& view = RankedRuntime::get().view();
        if (m_enteringLevel || view.stage != RuntimeStage::Matched ||
            corum::ranked::roundPrepareState(view.match.state) ||
            !corum::ranked::roundAcquireState(view.match.state)) return;
        auto& runtime = RankedRuntime::get();
        if (!runtime.canEnterCurrentLevel() && !runtime.canEnterNativeRoundScene() && !runtime.canEnterCurrentLevelAsSpectator()) return;
        // Spectating is opt-in: only the board's SPECTATE button arms it, once.
        auto const spectateOnly = !runtime.canEnterCurrentLevel() && !runtime.canEnterNativeRoundScene();
        auto const spectateKey = fmt::format("{}:{}:{}", view.match.matchId, view.match.roundNumber, view.match.deathmatchSequence);
        if (spectateOnly && m_spectateRequestKey != spectateKey) return;
        // Owned Ranked dialogs must yield before the native modal gate.
        // Close only dialogs opened by this exact carrier; other modals retain
        // their existing launch protection.
        if (m_jukebox) if (auto popup = m_jukebox->lock()) popup->dismiss();
        m_jukebox.reset();
        if (m_playerInfo) if (auto popup = m_playerInfo->lock()) popup->dismiss();
        m_playerInfo.reset();
        if (m_settings) if (auto popup = m_settings->lock()) popup->dismiss();
        m_settings.reset();
        if (m_leaderboard) if (auto popup = m_leaderboard->lock()) popup->dismiss();
        m_leaderboard.reset();
        if (m_mapPoolView) if (auto view = m_mapPoolView->lock()) view->closePopups();
        if (m_eliminationFx) if (auto popup = m_eliminationFx->lock()) popup->dismiss();
        if (m_achievementGrade) if (auto popup = m_achievementGrade->lock()) popup->dismiss();
        closeJukeboxNotice();
        if (!corum::ranked::RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene())) return;
        if (spectateOnly) m_spectateRequestKey.clear();
        m_enteringLevel = true;
        m_rankedChildScenePushed = true;
    }

    // Never rebuild the tree inside the pressed item's own activation: the
    // menu and item would be destroyed while CCMenu is still dispatching.
    void renderAfterInput() {
        queueInMainThread([weak = WeakRef(this)] {
            auto self = weak.lock(); if (!self || !self->getParent()) return;
            self->adoptCurrentPresentation(); self->render(); self->applyFadeInIfNeeded();
        });
    }

    void onLowDetail(CCObject*) {
        auto* level = findPlayableMap();
        if (!level || !level->m_lowDetailMode || m_enteringLevel) return;
        corum::ranked::RankedRoundPreparation::get().setLowDetail(!level->m_lowDetailModeToggled);
        // The exact native Saved object persists user options; never overwrite
        // the map author's m_lowDetailMode capability or any score/metadata.
        renderAfterInput();
    }

    void onDisableShake(CCObject*) {
        auto* level = findPlayableMap();
        if (!level || m_enteringLevel) return;
        level->m_disableShakeToggled = !level->m_disableShakeToggled;
        renderAfterInput();
    }

    void addRoundOptions(float y, float centerX = -1.0f) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const cx = centerX >= 0.0f ? centerX : size.width / 2.0f;
        auto* level = findPlayableMap();
        auto* ldm = addButton(fmt::format("{}: {}", tr("low-detail", "LDM"), level && level->m_lowDetailModeToggled ? "ON" : "OFF"),
            {cx - 103.0f, y}, menu_selector(CorumRankedLayer::onLowDetail), false, 0.35f);
        auto* shake = addButton(fmt::format("{}: {}", tr("disable-shake", "DISABLE SHAKE"), level && level->m_disableShakeToggled ? "ON" : "OFF"),
            {cx, y}, menu_selector(CorumRankedLayer::onDisableShake), false, 0.35f);
        ldm->setID("ranked-round-ldm"_spr);
        shake->setID("ranked-round-disable-shake"_spr);
        ldm->setEnabled(level && level->m_lowDetailMode);
        if (!(level && level->m_lowDetailMode)) corum::ranked::ui::grayButtonVisual(ldm->getNormalImage());
        shake->setEnabled(level != nullptr);
        addJukeboxButton({cx + 116.0f, y}, 0.35f);
    }

    void onDownloadAllResources(CCObject*) {
        m_localMessage.clear();
        RankedAudioManager::get().downloadAll();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        if (assets.view().state == corum::ranked::ui::RankAssetPackState::Failed) assets.retry();
        else assets.prepare();
    }

    // Fetch the active map pool and open one map at random through GD's level
    // page; downloads continue and nothing is recorded.
    void onRandomRankedMap(CCObject*) {
        if (m_randomMapBusy) return;
        m_randomMapBusy = true;
        ResourceReadyWatcher::arm();
        auto pending = corum::ranked::ui::mappool::request();
        m_randomMapRequest.spawn(pending.get(RankedRuntime::get().mapPoolUrl()), [weak = WeakRef(this)](geode::utils::web::WebResponse response) {
            queueInMainThread([weak, response = std::move(response)] {
                auto self = weak.lock(); if (!self) return;
                self->m_randomMapBusy = false;
                std::vector<int> ids;
                auto const data = response.json().unwrapOr(matjson::Value());
                for (auto const& map : data["maps"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
                    if (auto const id = corum::ranked::ui::mappool::levelNumber(corum::ranked::ui::mappool::parseRow(map).levelId); id > 0) ids.push_back(id);
                }
                if (!response.ok() || ids.empty()) {
                    Notification::create(corum::ranked::i18n::text("map-pool-load-failed", "Could not load the map pool."), NotificationIcon::Error, 2.0f)->show();
                    return;
                }
                auto const pick = ids[static_cast<std::size_t>(std::rand()) % ids.size()];
                corum::ranked::showHistoryLevel(pick);
            });
        });
    }

    void onResourceFreePlay(CCObject* sender) {
        ResourceReadyWatcher::arm();
        onCloseLayer(sender);
    }

    void onContinueResources(CCObject*) {
        if (!RankedAudioManager::get().resourcesReady()) return;
        if (corum::ranked::ui::RankAssetPackManager::get().requiresDownload()) return;
        m_resourceGateAcknowledged = true;
        m_resourceGateWasMissing = false;
        m_resourceGateMenuRestored = false;
        RankedRuntime::get().finishAdmission();
        syncAudioMode();
    }

    void onJoin(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().joinQueue();
    }

    void onOpenPlacement(CCObject*) {
        if (!RankedRuntime::get().view().placement.required) return;
        m_localMessage.clear();
        m_selectedPlacementStageCount = 0;
        m_selectedPlacementPool = 0;
        m_placementPoolStep = false;
        m_page = Page::PlacementPoolSelection;
    }

    void onPlacementStageCount(CCObject* sender) {
        auto const count = sender ? sender->getTag() : 0;
        if (count != 3 && count != 5) return;
        m_selectedPlacementStageCount = count;
        m_localMessage.clear();
    }

    void onPlacementPoolSelect(CCObject* sender) {
        auto const pool = sender ? sender->getTag() : 0;
        if (pool < 1 || pool > 6) return;
        m_selectedPlacementPool = pool;
        m_localMessage.clear();
    }

    void onStartPlacement(CCObject*) {
        if (m_selectedPlacementStageCount != 3 && m_selectedPlacementStageCount != 5) {
            m_localMessage = tr("select-placement-count-first", "SELECT 3 OR 5 STAGES FIRST.");
            return;
        }
        if (m_selectedPlacementPool < 1 || m_selectedPlacementPool > 6) {
            m_localMessage = tr("select-placement-pool-first", "SELECT A STARTING POOL FIRST.");
            return;
        }
        m_localMessage.clear();
        m_page = Page::Live;
        RankedRuntime::get().startPlacement(m_selectedPlacementPool, m_selectedPlacementStageCount);
    }

    void onOpenPlacementSkip(CCObject*) {
        m_localMessage.clear();
        m_page = Page::PlacementSkipConfirm;
    }

    void onPlacementPoolBack(CCObject*) {
        m_page = Page::PlacementPoolSelection;
    }

    void onConfirmPlacementSkip(CCObject*) {
        m_page = Page::Live;
        m_localMessage.clear();
        RankedRuntime::get().skipPlacement();
    }

    void onContinuePlacement(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().continuePlacement();
    }

    void onPausePlacement(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().pausePlacement();
        m_page = Page::Live;
    }

    void onLeave(CCObject*) {
        RankedRuntime::get().leaveQueue();
    }

    void onRetry(CCObject*) {
        RankedRuntime::get().begin();
    }

    void onEnvironmentRecheck(CCObject*) {
        RankedRuntime::get().recheckEnvironment();
    }

    void selectLanguage(corum::ranked::i18n::Language language) {
        if (corum::ranked::i18n::language() == language) return;
        corum::ranked::i18n::setLanguage(language);
        // The selected menu item is still inside CCMenu::activate() here. Let the
        // next scheduled UI pass rebuild the tree after touch dispatch finishes.
        m_renderedUiKey.clear();
        m_fadeInNextRender = false;
    }

    void onEnglish(CCObject*) {
        selectLanguage(corum::ranked::i18n::Language::English);
    }

    void onKorean(CCObject*) {
        selectLanguage(corum::ranked::i18n::Language::Korean);
    }

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
    void onDebugMenu(CCObject*) {
        corum::ranked::debug::showDebugMenuPopup(m_modeTransition.selected().value_or(corum::ranked::RankedMode::SoloRanked));
    }
#endif

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    std::optional<WeakRef<corum::ranked::ui::RankedConfirmPopup>> m_debugEndRoundConfirm;
    void onDebugEndRound(CCObject*) {
        auto& runtime = RankedRuntime::get();
        if (!runtime.canDebugEndRound()) return;
        if (m_debugEndRoundConfirm) if (auto open = m_debugEndRoundConfirm->lock(); open && open->getParent()) return;
        auto const matchId = runtime.view().match.matchId; auto const round = runtime.view().match.roundNumber;
        auto* popup = corum::ranked::ui::RankedConfirmPopup::create(tr("survival-end-round", "END ROUND"),
            tr("solo-end-round-question", "End this round now using the current scores?"),
            [matchId, round](bool accepted) {
                auto& current = RankedRuntime::get();
                if (accepted && current.canDebugEndRound() && current.view().match.matchId == matchId && current.view().match.roundNumber == round)
                    current.debugEndRound();
            }, tr("survival-end-round", "END ROUND"));
        if (popup) { m_debugEndRoundConfirm.emplace(popup); popup->show(); }
    }
    void onHistoryAllRecords(CCObject*) {
        auto& runtime = RankedRuntime::get();
        runtime.setHistoryAllRecords(!runtime.historyAllRecords());
        if (m_historyListScroll) m_historyListOffset = m_historyListScroll->m_contentLayer->getPositionY();
        onHistory(nullptr);
    }
#endif

    void onBan(CCObject* sender) {
        if (!m_selectedBan.empty() || !m_pendingBan.empty()) return;
        auto const index = sender ? sender->getTag() : -1;
        auto const& maps = RankedRuntime::get().view().match.candidateMaps;
        if (index < 0 || static_cast<std::size_t>(index) >= maps.size()) return;
        m_pendingBan = maps[static_cast<std::size_t>(index)].canonicalLevelId;
        RankedRuntime::get().submitBan(m_pendingBan);
    }

    void onNoBan(CCObject*) {
        if (!m_selectedBan.empty() || !m_pendingBan.empty()) return;
        m_pendingBan = kNoBanChoice;
        RankedRuntime::get().submitBan(std::nullopt);
    }

    void onQueueAgain(CCObject*) {
        m_page = Page::Live;
        RankedRuntime::get().queueAgain();
    }

    void onResultClose(CCObject*) {
        RankedRuntime::get().dismissMatch();
        RankedRuntime::get().fetchAchievements();
        m_page = Page::Live;
    }

    void onHistory(CCObject*) {
        if (m_modeTransition.moving()) return;
        if (m_page != Page::HistoryList && m_page != Page::HistoryDetail)
            m_historyScope = corum::ranked::historyScopeForLobby(m_modeTransition.selected());
        m_page = Page::HistoryList;
        RankedRuntime::get().setHistoryFilters(m_historyOrder,m_historyResult);
        RankedRuntime::get().fetchHistoryScope(m_historyScope);
    }

    void onHistoryScope(CCObject* sender) {
        if (!sender || m_modeTransition.moving() || m_page != Page::HistoryList) return;
        auto const tab = sender->getTag();
        if (tab < 0 || tab > 2) return;
        auto const scope = tab == 0 ? corum::ranked::HistoryScope::Survival : tab == 1 ? corum::ranked::HistoryScope::SoloRanked : corum::ranked::HistoryScope::All;
        if (scope == m_historyScope) return;
        m_historyScope = scope; m_historyIndex = 0; m_historyResult="ALL";m_historyListOffset.reset();
        RankedRuntime::get().setHistoryFilters(m_historyOrder,m_historyResult);
        RankedRuntime::get().fetchHistoryScope(scope);
    }

    void onBadgeCollection(CCObject*) {corum::ranked::ui::showSurvivalBadgeCollection();}
    void onAchievements(CCObject*) {
        if (m_modeTransition.moving()) return;
        auto const selected = m_modeTransition.selected();
        m_achievementScope = !selected ? "SHARED" : *selected == corum::ranked::RankedMode::Survival ? "SURVIVAL" : "SOLO_RANKED";
        m_page = Page::Achievements;
        m_achievementPage = 0;
        m_titleSelectorAchievement.clear();
        RankedRuntime::get().fetchAchievements();
    }

    // Private achievement grade ladder (thresholds, unlocks, current grade).
    void onAchievementGrade(CCObject*) {
        if (m_page != Page::Achievements || m_uiFadingOut) return;
        if (m_achievementGrade) if (auto open = m_achievementGrade->lock()) return;
        if (auto* popup = corum::ranked::ui::RankedAchievementGradePopup::create(RankedRuntime::get().view().achievements.totalAp)) {
            m_achievementGrade.emplace(popup); popup->show();
        }
    }

    void onAchievementClaimAll(CCObject*) {
        auto& runtime=RankedRuntime::get();
        auto const pending=corum::ranked::achievementPending(runtime.view().achievements.cards,"ALL");
        if(pending.rewards>0 && !runtime.view().achievements.loading)runtime.claimAllAchievements("ALL");
    }

    void onAchievementScope(CCObject* sender) {
        if (!sender || !m_titleSelectorAchievement.empty()) return;
        auto const tag = sender->getTag();
        if (tag < 0 || tag > 2) return;
        m_achievementScope = tag == 0 ? "SOLO_RANKED" : tag == 1 ? "SURVIVAL" : "SHARED";
        m_achievementPage = 0;
    }

    void onAchievementPrevious(CCObject*) {
        if (m_achievementPage > 0) --m_achievementPage;
    }

    void onAchievementNext(CCObject*) {
        auto const count = corum::ranked::achievementDisplayOrderForScope(RankedRuntime::get().view().achievements.cards,m_achievementScope).size();
        auto const pages = std::max<std::size_t>(1, (count + 3) / 4);
        if (m_achievementPage + 1 < pages) ++m_achievementPage;
    }

    void onAchievementClaim(CCObject* sender) {
        auto const index = sender ? sender->getTag() : -1;
        auto const& cards = RankedRuntime::get().view().achievements.cards;
        if (index < 0 || static_cast<std::size_t>(index) >= cards.size()) return;
        RankedRuntime::get().claimAchievement(cards[static_cast<std::size_t>(index)].id);
    }

    void onAchievementTitle(CCObject* sender) {
        auto const index = sender ? sender->getTag() : -1;
        auto const& cards = RankedRuntime::get().view().achievements.cards;
        if (index < 0 || static_cast<std::size_t>(index) >= cards.size()) return;
        auto const& card = cards[static_cast<std::size_t>(index)];
        if (card.type == "PROGRESSIVE") {
            m_titleSelectorAchievement = card.id;
            return;
        }
        auto const& equipped=RankedRuntime::get().view().achievements.equippedTitle;
        if(equipped.active && equipped.achievementId==card.id)RankedRuntime::get().clearAchievementTitle();
        else RankedRuntime::get().equipAchievementTitle(card.id, std::nullopt);
    }

    void onAchievementTitleLevel(CCObject* sender) {
        auto const level = sender ? sender->getTag() : 0;
        if (m_titleSelectorAchievement.empty() || level < 1 || level > 5) return;
        auto const id = m_titleSelectorAchievement;
        m_titleSelectorAchievement.clear();
        RankedRuntime::get().equipAchievementTitle(id, level);
    }

    void onAchievementTitleClear(CCObject*) {
        m_titleSelectorAchievement.clear();
        RankedRuntime::get().clearAchievementTitle();
    }

    void onAchievementTitleSelectorClose(CCObject*) {
        m_titleSelectorAchievement.clear();
    }

    void onHistoryDetail(CCObject* sender) {
        auto const index=sender?sender->getTag():-1;auto const& rows=RankedRuntime::get().view().history;
        if(index<0||static_cast<std::size_t>(index)>=rows.size())return;
        if(m_historyListScroll)m_historyListOffset=m_historyListScroll->m_contentLayer->getPositionY();
        m_historyIndex=static_cast<std::size_t>(index);m_historySelectedId=rows[m_historyIndex].matchId;
        m_historyRound.clear();m_historyTabPage=0;m_page=Page::HistoryDetail;
        RankedRuntime::get().fetchHistoryDetail(rows[m_historyIndex].mode,m_historySelectedId,true,rows[m_historyIndex].participantId);
    }
    void onHistoryOrder(CCObject*) {m_historyOrder=m_historyOrder=="NEWEST"?"OLDEST":"NEWEST";m_historyListOffset.reset();onHistory(nullptr);}
    void onHistoryFilter(CCObject*) {
        if(m_page!=Page::HistoryList)return;
        if(m_historyFilterPopup)if(auto popup=m_historyFilterPopup->lock();popup&&popup->getParent())return;
        auto const scope=m_historyScope;auto const session=RankedRuntime::get().spectatorLiveSessionToken();if(session.empty())return;
        auto filters=corum::ranked::historyResultFilters(m_historyScope!=corum::ranked::HistoryScope::Survival,m_historyScope!=corum::ranked::HistoryScope::SoloRanked);
        std::vector<corum::ranked::ui::RankedHistoryFilterChoice> choices;for(auto const& key:filters)choices.push_back({key,historyFilterText(key)});
        if(auto* popup=corum::ranked::ui::RankedHistoryFilterPopup::create(std::move(choices),m_historyResult,[weak=WeakRef(this),scope,session](std::string selected){
            auto owner=weak.lock();if(!owner||!owner->isRunning()||owner->m_page!=Page::HistoryList||owner->m_historyScope!=scope||session!=RankedRuntime::get().spectatorLiveSessionToken())return;
            owner->m_historyFilterPopup.reset();if(selected==owner->m_historyResult)return;
            owner->m_historyResult=std::move(selected);owner->m_historyListOffset.reset();owner->onHistory(nullptr);
        })){m_historyFilterPopup.emplace(popup);popup->show();}
    }
    void closeHistoryFilter(){if(m_historyFilterPopup)if(auto popup=m_historyFilterPopup->lock())popup->dismiss();m_historyFilterPopup.reset();}
    void onHistoryRefresh(CCObject*) {
        auto const& rows=RankedRuntime::get().view().history;
        if(m_page==Page::HistoryDetail&&m_historyIndex<rows.size())RankedRuntime::get().fetchHistoryDetail(rows[m_historyIndex].mode,rows[m_historyIndex].matchId,true,rows[m_historyIndex].participantId);
        else {if(m_historyListScroll)m_historyListOffset=m_historyListScroll->m_contentLayer->getPositionY();onHistory(nullptr);}
    }
    void onHistoryRound(CCObject* sender) {
        auto const& rows=RankedRuntime::get().view().history;if(!sender||m_historyIndex>=rows.size())return;
        auto const& item=rows[m_historyIndex];auto const rounds=corum::ranked::historyRounds(item.record,item.mode==corum::ranked::RankedMode::Survival);
        auto const index=sender->getTag();if(index>=0&&static_cast<std::size_t>(index)<rounds.size())m_historyRound=rounds[index].key;
    }
    void onHistoryRoundPage(CCObject* sender){if(sender)m_historyTabPage+=sender->getTag();}
    void onHistoryLevel(CCObject* sender){if(sender)corum::ranked::showHistoryLevel(sender->getTag());}

    void onHistoryBack(CCObject*) {
        closeHistoryFilter();
        RankedRuntime::get().cancelHistoryReads();
        corum::ranked::SurvivalThumbnailProvider::history().release(m_historyThumbnailContext);
        if (m_page == Page::Achievements && !m_titleSelectorAchievement.empty()) {
            m_titleSelectorAchievement.clear();
        } else if (m_page == Page::PlacementSkipConfirm) {
            m_page = Page::PlacementPoolSelection;
        } else if (m_page == Page::HistoryDetail) {
            m_page = Page::HistoryList;
        } else {
            m_page = Page::Live;
        }
    }

    void onSettings(CCObject*) {
        if (m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending || !m_titleSelectorAchievement.empty()) return;
        if (m_settings) {
            auto popup = m_settings->lock();
            if (popup && popup->getParent()) return;
            m_settings.reset();
        }
        if (auto* popup = corum::ranked::ui::RankedSettingsPopup::create()) {
            m_settings.emplace(popup); popup->show();
        }
    }

    void onPlayerInfo(CCObject* sender) {
        if (m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending || !m_titleSelectorAchievement.empty()) return;
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
        if (!item) return;
        if (m_playerInfo) if (auto popup = m_playerInfo->lock(); popup && popup->getParent()) return;
        m_playerInfo.reset();
        if (auto* popup = corum::ranked::ui::RankedPlayerInfoPopup::create(item->getTag() == 0 ? "A" : "B")) {
            m_playerInfo.emplace(popup); popup->show();
        }
    }

    void onJukebox(CCObject*) {
        if (!corum::ranked::ui::rankedJukeboxLoaded()) {
            if (m_jukeboxNotice) if (auto notice = m_jukeboxNotice->lock(); notice && notice->getParent()) return;
            auto* alert = FLAlertLayer::create("Jukebox",tr("jukebox-required","The Jukebox mod is required!"),"OK");
            alert->setID("ranked-jukebox-required"_spr); m_jukeboxNotice.emplace(alert); alert->show(); return;
        }
        if (m_jukebox) if (auto popup = m_jukebox->lock(); popup && popup->getParent()) return;
        auto* level = findPlayableMap();
        if (auto* popup = corum::ranked::ui::RankedJukeboxPopup::create(level)) {
            m_jukebox.emplace(popup); popup->show(); popup->openWhenReady();
        }
    }

    void closeJukeboxNotice() {
        if (m_jukeboxNotice) if (auto notice = m_jukeboxNotice->lock(); notice && notice->getParent()) {
            notice->onBtn1(nullptr); CCTouchDispatcher::get()->unregisterForcePrio(notice.data());
        }
        m_jukeboxNotice.reset();
    }

    void closeModeNotice() {
        ++m_modeNoticeGeneration;
        m_modeNoticeOpen = false;
        if (m_modeNotice) if (auto popup = m_modeNotice->lock()) popup->dismiss();
        m_modeNotice.reset();
    }

    void closeSettings() {
        closeModeNotice();
        if (m_leaderboard) if (auto popup = m_leaderboard->lock()) popup->dismiss();
        m_leaderboard.reset();
        if (m_mapPoolView) if (auto view = m_mapPoolView->lock()) view->closePopups();
        if (m_eliminationFx) if (auto popup = m_eliminationFx->lock()) popup->dismiss();
        if (m_achievementGrade) if (auto popup = m_achievementGrade->lock()) popup->dismiss();
        if (m_sessionConflict) if (auto popup = m_sessionConflict->lock()) popup->dismiss();
        m_sessionConflict.reset();
        closeJukeboxNotice();
        if (m_playerInfo) if (auto popup = m_playerInfo->lock()) popup->dismiss();
        m_playerInfo.reset();
        if (m_jukebox) if (auto popup = m_jukebox->lock()) popup->dismiss();
        m_jukebox.reset();
        if (m_settings) if (auto popup = m_settings->lock()) popup->dismiss();
        m_settings.reset();
        if (m_settingsMenu) {
            corum::ranked::ui::cancelMenuTouch(m_settingsMenu);
            m_settingsMenu->setTouchEnabled(false);
        }
    }

    void onLeaderboard(CCObject*) {
        if (m_modeTransition.moving()) return;
        if (m_leaderboard) if (auto popup = m_leaderboard->lock(); popup && popup->getParent()) return;
        if (auto* popup = corum::ranked::ui::RankedLeaderboardPopup::create(m_modeTransition.selected().value_or(corum::ranked::RankedMode::SoloRanked))) {
            m_leaderboard.emplace(popup); popup->show();
        }
    }

    void onHudEditor(CCObject*) {
        if (m_modeTransition.moving() || m_uiFadingOut || lobbyQueueActive()) return;
        if (RankedRuntime::get().view().stage != RuntimeStage::Ready) return;
        auto const mode = m_modeTransition.selected();
        if (!mode) return;
        auto* popup = *mode == corum::ranked::RankedMode::Survival
            ? corum::ranked::ui::HudEditorPopup::create(corum::ranked::HudMode::Survival)
            : corum::ranked::ui::HudEditorPopup::create(corum::ranked::HudMode::Solo);
        if (popup) popup->show();
    }

    void onTierGuide(CCObject*) {
        if(m_modeTransition.moving() || m_modeTransition.selected()!=corum::ranked::RankedMode::SoloRanked)return;
        if(auto* popup=corum::ranked::ui::RankedTierGuidePopup::create())popup->show();
    }

    void onCloseLayer(CCObject*) {
        if (m_modeNoticeOpen) { closeModeNotice(); syncLobbyInput(); return; }
        if (m_modeTransition.moving() || m_uiFadingOut) return;
        auto& runtime = RankedRuntime::get();
        if (m_page == Page::Live && m_modeTransition.selected() && m_lobbyPanel && runtime.view().stage != RuntimeStage::Matched) {
            if (m_queueAnimating) return;
            if (*m_modeTransition.selected() == corum::ranked::RankedMode::Survival) {
                auto& survival = corum::ranked::SurvivalRuntime::get();
                if (survival.creating() || survival.queue().status == "JOINING" || survival.queue().status == "MATCHED") return;
                if (survival.queueing()) { m_returnToModesAfterQueue = true; survival.leaveQueue(); return; }
            }
            if (runtime.view().stage == RuntimeStage::JoiningQueue) return;
            if (runtime.view().stage == RuntimeStage::Queued) {
                m_returnToModesAfterQueue = true;
                runtime.leaveQueue();
                return;
            }
            auto const mode = *m_modeTransition.selected();
            if (m_modeTransition.back()) animateModeLobby(mode,false);
            return;
        }
        if (runtime.view().stage == RuntimeStage::Matched && runtime.view().match.state != "MATCH_RESULT" &&
            runtime.view().match.state != "PLACEMENT_RESULT" && runtime.view().match.state != "CANCELLED") return;
        closeSettings();
        runtime.leaveRanked();
        RankedAudioManager::get().restoreMenuMusic();
        unschedule(schedule_selector(CorumRankedLayer::refresh));
        unschedule(schedule_selector(CorumRankedLayer::radarFrame));
        unschedule(schedule_selector(CorumRankedLayer::audioFrame));
        setKeypadEnabled(false);
        if (m_menu) {
            corum::ranked::ui::cancelMenuTouch(m_menu);
            m_menu->setTouchEnabled(false);
        }
        // Ranked now lives in its own GD-style scene/tab. Return to the previous
        // Geometry Dash scene instead of merely deleting the fullscreen child.
        CCDirector::sharedDirector()->popScene();
    }

    void onExit() override {
        RankedRuntime::get().setLobbyPopulationVisible(false);
        if(m_updateChannelPopup)if(auto popup=m_updateChannelPopup->lock())popup->dismiss();
        m_updateChannelPopup.reset();
        closeHistoryFilter();
        RankedRuntime::get().cancelHistoryReads();
        suspendHistoryThumbnail();
        if(m_historyReconnect)if(auto popup=m_historyReconnect->lock())popup->cancel();
        m_historyReconnect.reset();
        closeSettings();
        unschedule(schedule_selector(CorumRankedLayer::refresh));
        unschedule(schedule_selector(CorumRankedLayer::radarFrame));
        unschedule(schedule_selector(CorumRankedLayer::audioFrame));
        setKeypadEnabled(false);
        if (m_menu) {
            corum::ranked::ui::cancelMenuTouch(m_menu);
            m_menu->setTouchEnabled(false);
        }
        CCLayerColor::onExit();
    }

    void keyBackClicked() override {
        if (m_modeNoticeOpen) { closeModeNotice(); syncLobbyInput(); return; }
        if (m_modeTransition.moving()) return;
        if (m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending) return;
        if (m_page != Page::Live) {
            onHistoryBack(nullptr);
            return;
        }
        auto const& view = RankedRuntime::get().view();
        if (view.stage == RuntimeStage::Matched &&
            (view.match.state == "MATCH_RESULT" || view.match.state == "PLACEMENT_RESULT" || view.match.state == "CANCELLED")) {
            onResultClose(nullptr);
            return;
        }
        if (
            view.stage == RuntimeStage::Matched &&
            view.match.state != "MATCH_RESULT" &&
            view.match.state != "CANCELLED"
        ) {
            m_localMessage = "Ranked is active. Finish the match before leaving this screen.";
            return;
        }
        onCloseLayer(nullptr);
    }

public:
    static CorumRankedLayer* create() {
        auto* layer = new CorumRankedLayer();
        if (layer && layer->init()) {
            layer->autorelease();
            return layer;
        }
        delete layer;
        return nullptr;
    }
};

} // namespace

namespace corum::ranked {

void showRankedPopup() {
    auto* running = CCDirector::sharedDirector()->getRunningScene();
    if (!running || running->getChildByID("corum-ranked-fullscreen"_spr)) return;

    auto* scene = CCScene::create();
    scene->setID("corum-ranked-scene"_spr);
    if (auto* layer = CorumRankedLayer::create()) {
        scene->addChild(layer, 1);
    } else {
        return;
    }
    CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.25f, scene));
}

} // namespace corum::ranked
