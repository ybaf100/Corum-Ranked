#include "replay/HistoryReplayButton.hpp"
#include "ui/PublicPlayerProfile.hpp"
#include "ui/LobbyPopulationView.hpp"
#include "RankedPopup.hpp"
#include "domain/HistoryPresentation.hpp"
#include "HistoryLevelLookup.hpp"
#include "SurvivalThumbnailProvider.hpp"
#include "ui/ModeLobbyMotion.hpp"
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
#include "ui/RankedTapScroll.hpp"
#include "ui/RankedTierGuidePopup.hpp"
#include "ui/PlacementGradeBadge.hpp"
#include "ui/PlayerIcon.hpp"
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
constexpr ccColor3B kAccent = corum::ranked::ui::Cyan;
constexpr ccColor3B kGreen = corum::ranked::ui::Green;
constexpr ccColor3B kRed = corum::ranked::ui::Red;
constexpr ccColor3B kGold = corum::ranked::ui::Gold;
constexpr ccColor3B kCyan = corum::ranked::ui::Cyan;
constexpr ccColor3B kBlue = corum::ranked::ui::Blue;
constexpr ccColor3B kDeepPanel = corum::ranked::ui::Ink;
constexpr ccColor3B kDeepPanel2 = corum::ranked::ui::Surface;
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
    if (!title.displayColor.empty()) return colorFromCssHex(title.displayColor, kCyan);
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

CCNode* makePanel(CCSize size, CCPoint position, ccColor3B color = kPanelColor, GLubyte opacity = 235) {
    return corum::ranked::ui::surface(size, position, color, kDeepPanel2, opacity, false);
}

CCNode* makeNeonPanel(
    CCSize size,
    CCPoint position,
    ccColor3B accent = kCyan,
    ccColor3B fill = kDeepPanel,
    GLubyte fillOpacity = 245
) {
    return corum::ranked::ui::surface(size, position, accent, fill, fillOpacity, true);
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

CCNode* makeActionButtonVisual(
    std::string const& text,
    bool danger = false,
    bool selected = false
) {
    auto const width = std::clamp(46.0f + static_cast<float>(text.size()) * 7.0f, 104.0f, 178.0f);
    constexpr float height = 34.0f;
    auto const accent = selected ? kGold : (danger ? kRed : kCyan);

    auto* root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({width, height});

    auto* panel = corum::ranked::ui::surface(
        {width, height},
        {width / 2.0f, height / 2.0f},
        accent,
        danger ? ccc3(39, 15, 28) : corum::ranked::ui::SurfaceRaised,
        250,
        true
    );
    root->addChild(panel, 0);

    auto* caption = corum::ranked::ui::menuLabel(
        text,
        0.62f,
        {width / 2.0f, height / 2.0f},
        selected ? kGold : (danger ? ccc3(255, 224, 229) : corum::ranked::ui::Text),
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
    };

    CCLayerColor* m_fadeRoot = nullptr;
    CCNode* m_root = nullptr;
    CCMenu* m_menu = nullptr;
    CCMenu* m_settingsMenu = nullptr;
    CCLabelBMFont* m_settingsLabel = nullptr;
    std::optional<WeakRef<corum::ranked::ui::RankedSettingsPopup>> m_settings;
    std::optional<WeakRef<corum::ranked::ui::RankedLeaderboardPopup>> m_leaderboard;
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
    std::string m_selectedBan;
    std::string m_pendingBan;
    std::string m_localMessage;
    int m_selectedPlacementStageCount = 0;
    int m_selectedPlacementPool = 0;
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
        auto* settingsFace = corum::ranked::ui::surface({92, 28}, {}, kBlue, kDeepPanel2, 250, true);
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

        m_concessionNotice = CCLayerColor::create({5, 17, 29, 240}, std::min(290.0f, winSize.width - 70), 26);
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

        if (m_rankTransitionLayer || m_resultScoreLayer || m_resultScorePending) {
            updateDynamicUi(); syncAudioMode(); return;
        }

        auto const uiKey = presentationKey();
        if (prepareUiTransition(uiKey)) {
            render();
            applyFadeInIfNeeded();
        }
        updateDynamicUi();
        updateHistoryThumbnail();
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
        corum::ranked::RankedMapDetails::get().request(std::move(ids));
    }

    void addMapDifficulty(RankedMapView const& map, CCPoint center, float width, float height = 21) {
        auto* badge = corum::ranked::ui::mapDifficultyBadge(map.levelId,map.metadata,width,height);
        badge->setPosition({center.x - width / 2, center.y - height / 2});
        m_root->addChild(badge, 7);
    }

    void addJukeboxButton(CCPoint position, float scale = 0.32f) {
        m_jukeboxButton = addButton("Jukebox", position, menu_selector(CorumRankedLayer::onJukebox), false, scale);
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
            return fmt::format("placement:pool:{}:{}:{}:{}", RankedRuntime::get().view().revision, m_selectedPlacementStageCount, m_selectedPlacementPool, m_localMessage) + detailsKey;
        }
        if (m_page == Page::PlacementSkipConfirm) return "placement:skip-confirm";
        if (view.stage != RuntimeStage::Matched) {
            return fmt::format(
                "stage:{}:placement:{}:{}:standing:{}:{}",
                static_cast<int>(view.stage),
                view.placement.required,
                view.placement.active,
                view.standing.rank.value_or(0), view.standing.total
            ) + fmt::format(":mode:{}:solo:{}:sur:{}:{}:{}:{}:{}:{}:title:{}:{}:ap:{}:{}:size:{}:{}",
                m_modeTransition.selected() ? static_cast<int>(*m_modeTransition.selected()) : -1,
                view.soloEnabled,view.survivalEnabled,view.survival.loaded,view.survival.error,
                view.survival.profile.visibleRankedScore.value_or(-1),view.survival.profile.demotionWarning,
                view.survival.standing.rank.value_or(0),view.survival.standing.total,
                view.achievements.equippedTitle.achievementId,view.achievements.equippedTitle.level.value_or(0),
                view.achievements.totalAp,view.achievements.unclaimedRewardCount,
                CCDirector::sharedDirector()->getWinSize().width,CCDirector::sharedDirector()->getWinSize().height) + fmt::format(":sur-record:{}:{}:format:{}",view.survival.wins,view.survival.losses,view.survivalPlayerCount);
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
        if (m_modeTransition.moving()) { m_pendingUiKey = key; return false; }
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
        if (key == m_renderedUiKey) return false;
        m_pendingUiKey = key;
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
            if (view.promotion.active) return RankedAudioMode::LobbyPromotion;
            if (view.demotionWarning) return RankedAudioMode::LobbyDemotionWarning;
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
                m_selectedBan = view.match.ownBanCanonicalLevelId;
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
    }

    void suspendHistoryThumbnail() {
        if(m_historyThumbnailContext.levelId)corum::ranked::SurvivalThumbnailProvider::history().release(m_historyThumbnailContext);
    }
    void resetHistoryUi() {
        ++m_historyUiGeneration;suspendHistoryThumbnail();m_historyThumbnail=nullptr;m_historyThumbnailPlaceholder=nullptr;m_historyTexture=nullptr;m_historyListScroll=nullptr;m_historyBoardScroll=nullptr;m_historyProfiles.clear();m_historyIcons.clear();
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
        resetDynamicUiPointers();
        corum::ranked::ui::cancelMenuTouch(m_menu);
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
            if (m_resourceProgressLabel) m_resourceProgressLabel->setString(fmt::format("{}%",
                static_cast<int>(std::round(corum::ranked::ui::RankedModUpdateManager::get().view().progress * 100))).c_str());
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
                corum::ranked::ui::fitMenuLabelWidth(m_mainStatusLabel,std::max(100.0f,width-264),0.24f,0.14f);
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

        if (m_prepareFooterLabel) {
            auto const ownA = match.side == "A";
            auto& runtime = RankedRuntime::get();
            auto const clearSyncPending =
                match.deathmatchSequence <= 0 &&
                runtime.localDisplayClears() >= 3 &&
                runtime.hasLocalAttemptInFlight();
            auto const seconds = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
            std::string footer = fmt::format("{}  {:02d}", tr("starts-in", "STARTS IN"), seconds);
            if (clearSyncPending) {
                footer = tr("syncing-clear", "SYNCING CLEAR...");
            } else if (match.state == "DEATHMATCH_PLAYING") {
                auto displayOwnUsed = ownA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
                auto displayOpponentUsed = ownA ? match.deathmatchAttemptsUsedB : match.deathmatchAttemptsUsedA;
                displayOwnUsed = std::max(displayOwnUsed, runtime.localDeathmatchVisualAttemptsUsed());
                footer = displayOwnUsed >= 3
                    ? fmt::format("{}   {}/3", tr("waiting-opponent", "WAITING FOR OPPONENT"), displayOpponentUsed)
                    : corum::ranked::i18n::attemptOfThree(std::min(3, displayOwnUsed + 1));
            } else if (phaseSeconds() >= 10.0) {
                auto const ownReady = ownA ? match.readyA : match.readyB;
                auto const opponentReady = ownA ? match.readyB : match.readyA;
                if (!ownReady) footer = corum::ranked::RankedRoundPreparation::get().ready()
                    ? tr("sending-ready", "SENDING READY...") : tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
                else if (!opponentReady) footer = corum::ranked::i18n::waitingForPlayer(upper(shorten(match.opponentName, 13)));
                else footer = tr("starting", "STARTING...");
            }
            m_prepareFooterLabel->setString(footer.c_str());
        }

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
                    m_levelLookupError.empty() ? kCyan : kGold
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
        auto* sprite = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        sprite->setScale(0.62f);
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
        ccColor3B accent = kCyan
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
            auto* line = CCLayerColor::create({accent.r, accent.g, accent.b, 95});
            line->setContentSize({lineWidth, 1.0f});
            line->setPosition({x, size.height - 53.5f});
            m_root->addChild(line, 4);
        }
    }

    CCMenuItemSpriteExtra* addButton(
        std::string const& text,
        CCPoint position,
        SEL_MenuHandler selector,
        bool danger = false,
        float scale = 0.72f
    ) {
        auto* visual = makeActionButtonVisual(text, danger);
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
            selected == corum::ranked::i18n::Language::English
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
            selected == corum::ranked::i18n::Language::Korean
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
        addScreenHeader(
            tr("mod-check-required", "MOD CHECK REQUIRED"),
            tr("ranked-access-blocked", "RANKED ACCESS BLOCKED"),
            kRed,
            kRed
        );

        auto const panelWidth = std::min(448.0f, size.width - 64.0f);
        auto const panelHeight = std::min(194.0f, size.height - 112.0f);
        auto const panelCenter = CCPoint{size.width / 2.0f, size.height / 2.0f - 5.0f};
        auto* panel = makeNeonPanel(
            {panelWidth, panelHeight},
            panelCenter,
            kRed,
            ccc3(28, 11, 23),
            250
        );
        m_root->addChild(panel, 1);

        auto* body = makeLabel(
            tr("environment-warning-body", "RANKED CANNOT BE USED WITH THE CURRENT MOD SETUP."),
            0.29f,
            {size.width / 2.0f, panelCenter.y + panelHeight / 2.0f - 21.0f},
            ccc3(255, 232, 236),
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(body, panelWidth - 34.0f, 0.29f, 0.19f);
        m_root->addChild(body, 4);

        auto* action = makeLabel(
            tr(
                "environment-warning-action",
                "FIX THE ITEMS BELOW, RESTART THE GAME, THEN CHECK AGAIN."
            ),
            0.22f,
            {size.width / 2.0f, panelCenter.y + panelHeight / 2.0f - 43.0f},
            corum::ranked::ui::TextMuted,
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(action, panelWidth - 34.0f, 0.22f, 0.15f);
        m_root->addChild(action, 4);

        float rowY = panelCenter.y + panelHeight / 2.0f - 68.0f;
        auto addSection = [&](std::string const& heading, std::string const& detail) {
            if (detail.empty()) return;
            auto* headingLabel = makeLabel(
                heading,
                0.23f,
                {size.width / 2.0f, rowY},
                kRed,
                "black"
            );
            corum::ranked::ui::fitMenuLabelWidth(headingLabel, panelWidth - 36.0f, 0.23f, 0.16f);
            m_root->addChild(headingLabel, 4);

            auto* detailLabel = makeLabel(
                detail,
                0.23f,
                {size.width / 2.0f, rowY - 14.0f},
                corum::ranked::ui::Text,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(detailLabel, panelWidth - 36.0f, 0.23f, 0.13f);
            m_root->addChild(detailLabel, 4);
            rowY -= 32.0f;
        };

        addSection(
            tr("unauthorized-active-mods", "UNAUTHORIZED ACTIVE MODS"),
            joinEnvironmentMods(view.environmentWarning.unauthorizedMods, false)
        );
        addSection(
            tr("missing-required-mods", "MISSING OR INACTIVE REQUIRED MODS"),
            joinEnvironmentMods(view.environmentWarning.missingRequiredMods, true)
        );
        addSection(
            tr("incompatible-mod-versions", "INCOMPATIBLE MOD VERSIONS"),
            joinEnvironmentMods(view.environmentWarning.versionViolations, true)
        );
        addSection(
            tr("cbf-settings-required", "CBF SETTINGS REQUIRED"),
            cbfSettingsText(view.environmentWarning.cbfIssues)
        );

        addButton(
            tr("retry-check", "CHECK AGAIN"),
            {size.width / 2.0f, 24.0f},
            menu_selector(CorumRankedLayer::onEnvironmentRecheck),
            false,
            0.64f
        );
    }

    void addRatingMarks(
        std::vector<std::string> const& results,
        int slots,
        CCPoint center,
        float spacing = 13.0f,
        float scale = 0.24f
    ) {
        slots = std::clamp(slots, 1, 9);
        auto const totalWidth = spacing * static_cast<float>(slots - 1);
        for (int index = 0; index < slots; ++index) {
            std::string text = "-";
            ccColor3B color = ccc3(145, 151, 166);
            if (index < static_cast<int>(results.size())) {
                if (results[index] == "WIN") {
                    text = "O";
                    color = kGreen;
                } else if (results[index] == "LOSS") {
                    text = "X";
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
        m_root->addChild(glow, 3);

        for (auto const x : {center.x - size.width / 2.0f + 5.0f, center.x + size.width / 2.0f - 6.5f}) {
            auto* rail = CCLayerColor::create({accent.r, accent.g, accent.b, static_cast<GLubyte>(perfect ? 185 : 132)});
            rail->setContentSize({1.5f, perfect ? 24.0f : 17.0f});
            rail->setPosition({x, bottom});
            m_root->addChild(rail, 3);
        }
    }

    void addRankStatus(RankedProfileView const& profile, CCPoint position, float scale, float width, bool lobbyFallback = false) {
        std::string text;
        auto color = kCyan;
        if (std::abs(profile.resultStreak) >= 2) {
            text = corum::ranked::i18n::streak(std::abs(profile.resultStreak),profile.resultStreak > 0);
            color = profile.resultStreak > 0 ? kGold : kRed;
        }
        std::string state;
        if (profile.promotion.active) {
            state = corum::ranked::i18n::promotionMatch(profile.promotion.winsRequired);
            color = ccc3(145,255,125);
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
        bool combineTierAndScore = false
    ) {
        auto const& tier = profile.displayedTier;
        auto const accent = tierColor(upper(tier));
        auto* panel = makeNeonPanel({150.0f, 184.0f}, center, accent, kDeepPanel, 246);
        m_root->addChild(panel, 1);
        if (profile.promotion.active) addPromotionAura(center, {150.0f, 184.0f});

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
            auto* face = makeTextPlate(tr("info", "INFO"), {34, 18}, {}, kCyan, 0.18f);
            auto* info = CCMenuItemSpriteExtra::create(face, this, menu_selector(CorumRankedLayer::onPlayerInfo));
            info->setID("ranked-player-info-button"_spr);
            info->setTag(self ? (match.side == "A" ? 0 : 1) : (match.side == "A" ? 1 : 0));
            info->setPosition({center.x + (self ? -55 : 55), center.y + 78}); m_menu->addChild(info, 10);
        }

        auto* iconPlate = makeNeonPanel({66.0f, 66.0f}, {center.x, center.y + 29.0f}, accent, {5, 11, 23}, 248);
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
        auto* lpCaption = makeLabel(captionText, 0.23f, {center.x, captionY}, kCyan, "semibold");
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

    void renderModUpdate() {
        using corum::ranked::ui::ModUpdateState;
        auto const& update = corum::ranked::ui::RankedModUpdateManager::get().view();
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const center = CCPoint{size.width / 2, size.height / 2};
        addTopBack(true);
        m_root->addChild(makeNeonPanel({std::min(390.0f, size.width - 90), 210}, center, kCyan, kDeepPanel, 248), 1);
        auto const restart = update.state == ModUpdateState::RestartRequired;
        auto const busy = update.state == ModUpdateState::Downloading || update.state == ModUpdateState::Verifying;
        auto* heading = makeLabel(tr("mod-update", "MOD UPDATE"), 0.48f, {center.x, center.y + 70}, kCyan, "black");
        m_root->addChild(heading, 4);
        auto* versions = makeLabel(fmt::format("{} -> {} ({})", update.installedVersion, update.targetVersion,
            corum::ranked::ui::RankedModUpdateManager::get().channel()), 0.30f, {center.x, center.y + 39}, kGold, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(versions, 330, 0.30f, 0.18f); m_root->addChild(versions, 4);
        auto const message = restart ? tr("mod-restart-ready", "Update installed. Press Restart to apply it.") :
            !update.error.empty() ? update.error : busy ? tr("mod-verifying", "Downloading and verifying the mod...") :
            tr("mod-update-required", "Update the mod before entering Ranked.");
        auto* detail = makeLabel(message, 0.29f, {center.x, center.y + 7}, update.error.empty() ? kCyan : kRed, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(detail, 335, 0.29f, 0.16f); m_root->addChild(detail, 4);
        if (busy) {
            m_resourceProgressLabel = makeLabel(fmt::format("{}%", static_cast<int>(update.progress * 100)),
                0.35f, {center.x, center.y - 33}, kGold, "black"); m_root->addChild(m_resourceProgressLabel, 4);
        } else {
            auto const canDownload = corum::ranked::ui::RankedModUpdateManager::get().canDownload();
            addButton(restart ? tr("restart", "RESTART") : canDownload ? tr("mod-update-download", "DOWNLOAD UPDATE") : tr("retry", "RETRY"),
                {center.x+48, center.y - 60}, restart ? menu_selector(CorumRankedLayer::onRestartModUpdate) :
                canDownload ? menu_selector(CorumRankedLayer::onDownloadModUpdate) : menu_selector(CorumRankedLayer::onRetry), false, 0.70f);
        }
        auto* branch=historyButton(m_root,corum::ranked::ui::RankedModUpdateManager::get().channel()=="debug"?"DEBUG":"RELEASE",
            {center.x-std::min(390.f,size.width-90)/2+54,center.y-86},100,menu_selector(CorumRankedLayer::onUpdateChannel));branch->setEnabled(!busy&&!restart);
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
    void onRestartModUpdate(CCObject*) { corum::ranked::ui::RankedModUpdateManager::get().restart(); }

    void renderResources() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        auto const audio = RankedAudioManager::get().downloadView();
        auto& manager = corum::ranked::ui::RankAssetPackManager::get();
        auto const assets = manager.view();
        auto const downloading = audio.state == RankedResourceDownloadState::Downloading ||
            assets.state == corum::ranked::ui::RankAssetPackState::Downloading;
        auto const failed = audio.state == RankedResourceDownloadState::Failed ||
            assets.state == corum::ranked::ui::RankAssetPackState::Failed;
        auto const complete = !resourceWorkMissing();
        auto const accent = failed ? kRed : complete ? kGreen : downloading ? kGold : kCyan;
        auto const center = CCPoint{size.width / 2, size.height / 2};
        m_root->addChild(makeNeonPanel({std::min(390.0f, size.width - 90), 230}, center,
            accent, kDeepPanel, 248), 1);
        // Installation identity remains stable during verification and retry.
        auto title = tr("additional-downloads", "ADDITIONAL DOWNLOADS");
        if (complete) title = tr("all-resources-ready", "ALL RESOURCES READY");
        auto* heading = makeLabel(title, 0.48f, {center.x, center.y + 85}, accent, "black");
        corum::ranked::ui::fitMenuLabelWidth(heading, 330, 0.48f, 0.27f); m_root->addChild(heading, 4);
        auto subtitle = failed ? tr("retry-before-continuing", "Retry the failed download before continuing.") :
            assets.verifying ? tr("assets-verifying", "Verifying and installing image pack...") :
            downloading ? tr("keep-open", "Keep this screen open until the download is complete.") :
            complete ? tr("everything-ready", "Everything required for Ranked is ready.") :
            tr("assets-one-download", "Download all required resources in one step.");
        auto* detail = makeLabel(subtitle, 0.25f, {center.x, center.y + 57}, corum::ranked::ui::TextMuted, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(detail, 335, 0.25f, 0.16f); m_root->addChild(detail, 4);
        auto const [ready, total] = resourceCounts();
        m_resourceCountLabel = makeLabel(fmt::format("{} / {} {}", ready, total, tr("ready", "READY")),
            0.35f, {center.x, center.y + 28}, ready == total ? kGreen : corum::ranked::ui::Text, "black");
        m_root->addChild(m_resourceCountLabel, 4);
        if (manager.required()) {
            auto const version = assets.update ? assets.activeVersion + " -> " + assets.targetVersion : assets.targetVersion;
            auto* versions = makeLabel(version, 0.23f, {center.x, center.y + 7}, kCyan, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(versions, 335, 0.23f, 0.14f); m_root->addChild(versions, 4);
        }
        auto const images = !manager.required() ? tr("assets-basic-ui", "Built-in interface") : manager.ready() ?
            tr("ready", "READY") : failed ? tr("resources-failed", "DOWNLOAD FAILED") :
            assets.verifying ? tr("assets-verifying-short", "VERIFYING") : downloading ?
            tr("downloading-resources", "DOWNLOADING") : tr("resources-required", "DOWNLOAD REQUIRED");
        auto* imageRow = makeLabel(tr("assets-images", "Images") + " · " + images,
            0.22f, {center.x, center.y - 13}, accent, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(imageRow, 335, 0.22f, 0.14f); m_root->addChild(imageRow, 4);
        if (audio.total > 0 && assets.error.empty()) {
            auto* songs = makeLabel(fmt::format("{} · {} / {}", tr("assets-music", "Music"), audio.ready, audio.total),
                0.22f, {center.x, center.y - 30}, audio.ready == audio.total ? kGreen : kGold, "semibold");
            m_root->addChild(songs, 4);
        }
        if (failed && !assets.error.empty()) {
            auto* error = makeLabel(assets.error, 0.19f, {center.x, center.y - 30}, kRed, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(error, 335, 0.19f, 0.12f); m_root->addChild(error, 4);
        }
        auto const barWidth = std::min(270.0f, size.width - 180);
        auto* back = CCLayerColor::create({9, 17, 32, 235}, barWidth, 10);
        back->setPosition({center.x - barWidth / 2, center.y - 47}); m_root->addChild(back, 3);
        m_resourceProgressFill = CCLayerColor::create({accent.r, accent.g, accent.b, 245},
            static_cast<float>(barWidth * resourceProgress()), 10);
        m_resourceProgressFill->setPosition(back->getPosition()); m_resourceProgressWidth = barWidth;
        m_root->addChild(m_resourceProgressFill, 4);
        if (downloading) {
            m_resourceProgressLabel = makeLabel(fmt::format("{}%", static_cast<int>(std::round(resourceProgress() * 100))),
                0.23f, {center.x, center.y - 70}, kGold); m_root->addChild(m_resourceProgressLabel, 4);
        } else if (complete) {
            addButton(tr("continue", "CONTINUE"), {center.x, center.y - 83}, menu_selector(CorumRankedLayer::onContinueResources), false, 0.72f);
        } else {
            addButton(failed ? tr("retry-failed", "RETRY FAILED") : tr("download-all", "DOWNLOAD ALL"),
                {center.x, center.y - 83}, menu_selector(CorumRankedLayer::onDownloadAllResources), failed, 0.70f);
        }
    }

    void renderPlacementPoolSelection(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        addLanguageSelector();
        addScreenHeader(
            tr("placement-matches", "PLACEMENT"),
            tr("placement-setup", "PLACEMENT SETUP"),
            kGold,
            kCyan
        );

        auto* countTitle = makeLabel(
            tr("placement-count", "CHOOSE PLACEMENT LENGTH"),
            0.25f,
            {size.width / 2.0f, size.height - 72.0f},
            corum::ranked::ui::Text,
            "black"
        );
        m_root->addChild(countTitle, 5);

        auto const threeCountText = fmt::format(fmt::runtime(tr("placement-stage-count","{} STAGES")),3);
        auto const fiveCountText = fmt::format(fmt::runtime(tr("placement-stage-count","{} STAGES")),5);
        auto* three = addButton(
            m_selectedPlacementStageCount == 3 ? fmt::format("[ {} ]", threeCountText) : threeCountText,
            {size.width / 2.0f - 78.0f, size.height - 97.0f},
            menu_selector(CorumRankedLayer::onPlacementStageCount),
            false,
            0.54f
        );
        three->setTag(3);
        auto* five = addButton(
            m_selectedPlacementStageCount == 5 ? fmt::format("[ {} ]", fiveCountText) : fiveCountText,
            {size.width / 2.0f + 78.0f, size.height - 97.0f},
            menu_selector(CorumRankedLayer::onPlacementStageCount),
            false,
            0.54f
        );
        five->setTag(5);

        std::string countHint;
        ccColor3B countHintColor = corum::ranked::ui::TextMuted;
        if (m_selectedPlacementStageCount == 3) {
            countHint = tr(
                "placement-three-stage-warning",
                "WARNING: 3 STAGES LOWERS YOUR MAX PLACEMENT. MAX: AQUA. CANNOT CHANGE AFTER START."
            );
            countHintColor = kRed;
        } else if (m_selectedPlacementStageCount == 5) {
            countHint = tr(
                "placement-five-stage-info",
                "5 STAGES · 10 MAPS · FULL PLACEMENT RANGE · CANNOT CHANGE AFTER START."
            );
            countHintColor = kGreen;
        } else {
            countHint = tr(
                "placement-count-lock",
                "SELECT 3 OR 5 STAGES. THIS CANNOT BE CHANGED AFTER START."
            );
        }
        auto* countHintLabel = makeLabel(
            countHint,
            0.20f,
            {size.width / 2.0f, size.height - 123.0f},
            countHintColor,
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(countHintLabel, size.width - 74.0f, 0.20f, 0.14f);
        m_root->addChild(countHintLabel, 5);

        auto* panel = makeNeonPanel(
            {std::min(430.0f, size.width - 60.0f), 132.0f},
            {size.width / 2.0f, size.height / 2.0f - 36.0f},
            kBlue,
            kDeepPanel,
            244
        );
        m_root->addChild(panel, 1);
        auto* poolTitle = makeLabel(
            tr("select-starting-pool", "SELECT YOUR STARTING POOL"),
            0.22f,
            {size.width / 2.0f, size.height / 2.0f + 25.0f},
            kCyan,
            "black"
        );
        m_root->addChild(poolTitle, 5);

        constexpr float spacing = 124.0f;
        for (int pool = 1; pool <= 6; ++pool) {
            auto const row = (pool - 1) / 3;
            auto const column = (pool - 1) % 3;
            auto const x = size.width / 2.0f + (static_cast<float>(column) - 1.0f) * spacing;
            auto const y = size.height / 2.0f - 2.0f - static_cast<float>(row) * 48.0f;
            auto* button = addButton(
                m_selectedPlacementPool == pool ? fmt::format("[ POOL {} ]", pool) : fmt::format("POOL {}", pool),
                {x, y},
                menu_selector(CorumRankedLayer::onPlacementPoolSelect),
                false,
                0.50f
            );
            button->setTag(pool);

            auto const reference = std::find_if(
                view.placement.referenceMaps.begin(),
                view.placement.referenceMaps.end(),
                [pool](RankedMapView const& map) { return map.pool == pool; }
            );
            auto const referenceText = reference == view.placement.referenceMaps.end()
                ? tr("reference-loading", "REFERENCE MAP LOADING...")
                : reference->title;
            auto* referenceLabel = makeLabel(
                referenceText,
                0.14f,
                {x, y - 16.0f},
                corum::ranked::ui::TextMuted,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(referenceLabel, 112.0f, 0.14f, 0.10f);
            m_root->addChild(referenceLabel, 5);
            if (reference != view.placement.referenceMaps.end()) {
                addMapDifficulty(*reference,{x,y-28},107,14);
                auto const lengthMetadata=corum::ranked::ui::displayedMapMetadata(reference->levelId,reference->metadata);
                auto* length=makeLabel(corum::ranked::mapLengthText(lengthMetadata),0.14f,{x,y-40},corum::ranked::ui::TextMuted,"semibold");
                m_root->addChild(length,5);
            }
        }

        addButton(
            tr("start-placement", "START PLACEMENT"),
            {size.width / 2.0f + 78.0f, 23.0f},
            menu_selector(CorumRankedLayer::onStartPlacement),
            false,
            0.53f
        );
        addButton(
            tr("skip-placement", "SKIP PLACEMENT"),
            {size.width / 2.0f - 78.0f, 23.0f},
            menu_selector(CorumRankedLayer::onOpenPlacementSkip),
            true,
            0.50f
        );

        auto detail = !m_localMessage.empty() ? m_localMessage : (view.error.empty() ? view.status : view.error);
        if (!detail.empty() && (!view.error.empty() || !m_localMessage.empty())) {
            auto* status = makeLabel(
                shorten(detail, 96),
                0.20f,
                {size.width / 2.0f, 7.0f},
                view.error.empty() ? kGold : kRed,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(status, size.width - 90.0f, 0.20f, 0.14f);
            m_mainStatusLabel = status;
            m_root->addChild(status, 5);
        }
    }

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
        setLobbyNodeInput(m_lobbyPanel,ready && selected.has_value());
        setLobbyNodeInput(m_lobbyToolbar,ready);
        if (m_menu) m_menu->setTouchEnabled(ready);
    }

    CCLayerColor* makeLobbyGroup(CCPoint position, float scale, std::string const& id) {
        auto* group = CCLayerColor::create({0,0,0,255},0,0);
        group->setPosition(position); group->setScale(scale);
        group->setCascadeOpacityEnabled(false); group->setID(id);
        m_root->addChild(group,3); return group;
    }

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
            view.placementResults.clear(); view.placement = {}; view.promotion = {};
            view.demotionWarning = profile.demotionWarning;
        }
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        m_root = group; m_menu = CCMenu::create(); m_menu->setPosition(CCPointZero); group->addChild(m_menu,20);
        auto const leftCenter = CCPointZero;
        RankedProfileView profile;
        profile.displayedTier = view.profileTier;
        if (view.profileScoreVisible) profile.visibleRankedScore = view.profileScore;
        profile.placementGames = view.placementGames;
        profile.placementGamesRequired = view.placementGamesRequired;
        profile.placementResults = view.placementResults;
        profile.resultStreak = view.resultStreak;
        profile.promotion = view.promotion;
        profile.demotionWarning = view.demotionWarning;
        profile.equippedTitle = view.achievements.equippedTitle;

        auto* profilePanel = makeNeonPanel({188.0f, 210.0f}, leftCenter, tierColor(upper(view.profileTier)), kDeepPanel, 246);
        m_root->addChild(profilePanel, 1);
        if (profile.promotion.active) addPromotionAura(leftCenter, {188.0f, 210.0f});
        auto standing = view.profileScoreVisible ? view.standing : corum::ranked::StandingView{};
        auto* rankLine = corum::ranked::ui::standingLine(standing,0.37f);
        if (rankLine->getContentSize().width > 168) rankLine->setScale(168 / rankLine->getContentSize().width);
        rankLine->setID("ranked-profile-standing"_spr);
        // Keep the rank immediately above the full-size badge and below the
        // streak. Its higher z keeps decorative background halos behind text.
        rankLine->setPosition({leftCenter.x,leftCenter.y + 68.0f}); m_root->addChild(rankLine,8);
        auto* emblem = corum::ranked::ui::createAnimatedRankBadge(
            view.profileTier,
            {74.0f, 74.0f},
            corum::ranked::ui::RankBadgeDisplay::Large
        );
        emblem->setPosition({leftCenter.x, leftCenter.y + 18.0f});
        m_root->addChild(emblem, 4);
        addRankStatus(profile,{leftCenter.x,leftCenter.y + 91.0f},0.26f,174.0f,true);
        auto* tier = makeLabel(corum::ranked::rankTierDisplay(upper(view.profileTier)), 0.54f, {leftCenter.x, leftCenter.y - 25.0f}, tierColor(upper(view.profileTier)), "black");
        corum::ranked::ui::fitMenuLabelWidth(tier, 150.0f, 0.54f, 0.32f);
        m_root->addChild(tier, 4);
        auto const placement = profile.placementGames < profile.placementGamesRequired;
        if (placement) {
            auto const completedStages = std::max(
                profile.placementGames,
                static_cast<int>(view.placement.stageResults.size())
            );
            auto const placementStageTotal = view.placement.active ? view.placement.totalStages : profile.placementGamesRequired;
            addPlacementProgress(completedStages, std::max(1, placementStageTotal), {leftCenter.x, leftCenter.y - 51.0f}, 16.0f, 0.30f);
        } else if (survival && !view.survival.loaded) {
            auto* pending = makeLabel(view.survival.error.empty() ? tr("syncing", "SYNCING...") : tr("connection-issue", "CONNECTION ISSUE"), 0.25f, {leftCenter.x,leftCenter.y - 53}, kGold, "semibold");
            m_root->addChild(pending,4);
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {leftCenter.x, leftCenter.y - 49.0f}, 16.0f, 0.30f);
        } else {
            if (view.profileTier == "GOLD") {
                auto* rating = corum::ranked::ui::standingLine(standing,0.42f,view.profileScore);
                if (rating->getContentSize().width > 168) rating->setScale(168 / rating->getContentSize().width);
                rating->setID("ranked-profile-gold-lp"_spr);
                rating->setPosition({leftCenter.x,leftCenter.y - 53.0f}); m_root->addChild(rating,4);
            } else {
                auto* rating = makeLabel(fmt::format("{} LP", view.profileScore), 0.46f, {leftCenter.x, leftCenter.y - 53.0f}, ccc3(240, 244, 250), "black");
                corum::ranked::ui::fitMenuLabelWidth(rating, 150.0f, 0.46f, 0.28f);
                m_root->addChild(rating, 4);
            }
        }
        auto const hasTitle = view.achievements.equippedTitle.active;
        if (hasTitle) {
            auto* equippedTitle = makeLabel(
                achievementTitleText(view.achievements.equippedTitle),
                0.20f,
                {leftCenter.x, leftCenter.y - 68.0f},
                achievementTitleColor(view.achievements.equippedTitle),
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(equippedTitle, 168.0f, 0.20f, 0.14f);
            equippedTitle->setID("ranked-profile-equipped-title"_spr);
            m_root->addChild(equippedTitle, 4);
            corum::ranked::ui::styleAchievementText(equippedTitle,view.achievements.equippedTitle.level.value_or(corum::ranked::achievementRarityTier(view.achievements.equippedTitle.rarity)));
        }

        auto* record = makeLabel(survival?corum::ranked::survivalRecordText(source.survival.wins,source.survival.losses,corum::ranked::i18n::isKorean()):corum::ranked::matchRecordText(view.profileWins,view.profileLosses,
            corum::ranked::i18n::isKorean()),0.26f,{leftCenter.x,leftCenter.y - 92.0f},
            corum::ranked::ui::TextMuted,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(record,170,0.26f,0.20f);
        record->setID("ranked-profile-record"_spr); m_root->addChild(record,8);


        auto* modeName = makeLabel(survival ? "SURVIVAL" : "SOLO RANKED",0.35f,{0,115},survival ? kCyan : kGold,"black");
        corum::ranked::ui::fitMenuLabelWidth(modeName,150,0.35f,0.25f); m_root->addChild(modeName,8);
        if (survival && source.survival.warningRecoveryLp) {
            auto const text = fmt::format(fmt::runtime(tr("lp-warning-recovery","Clears at {} LP")),*source.survival.warningRecoveryLp);
            auto* condition = makeLabel(text,0.17f,{0,80},kRed,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(condition,174,0.17f,0.12f); m_root->addChild(condition,8);
        }
        // A transparent full-card hit area unifies entry and OFF browsing.
        // Match admission remains separately gated on both client and server.
        auto* hitArea = CCNode::create(); hitArea->setContentSize({188,210});
        auto* select = CCMenuItemSpriteExtra::create(hitArea,this,menu_selector(CorumRankedLayer::onSelectMode));
        select->setTag(static_cast<int>(mode)); select->setPosition(CCPointZero);
        select->setEnabled(source.stage == RuntimeStage::Ready);
        select->setID(survival ? "survival-card-select"_spr : "solo-ranked-card-select"_spr); m_menu->addChild(select);
        m_root = savedRoot; m_menu = savedMenu;
    }

    void renderModePanel(RuntimeView const& view, corum::ranked::RankedMode mode) {
        if (!m_lobbyPanel) return;
        setLobbyNodeInput(m_lobbyPanel,false);
        m_lobbyPanel->removeAllChildrenWithCleanup(true);
        auto* savedRoot = m_root; auto* savedMenu = m_menu;
        m_lobbyContents=CCLayerColor::create({0,0,0,255},0,0);m_lobbyContents->setID("ranked-mode-panel-contents"_spr);
        m_lobbyContents->setCascadeOpacityEnabled(true);m_lobbyPanel->addChild(m_lobbyContents,10);
        m_root = m_lobbyContents; m_menu = CCMenu::create(); m_menu->setPosition(CCPointZero); m_root->addChild(m_menu,20);
        auto const rightCenter = CCPoint{94,0};
        if (mode == corum::ranked::RankedMode::SoloRanked) {
        auto* actionPanel = makeNeonPanel({188.0f, 210.0f}, rightCenter, kBlue, kDeepPanel, 246);
        m_lobbyPanel->addChild(actionPanel, 1);
        auto* leaderboardFace = corum::ranked::ui::surface({30,30},{},kGold,kDeepPanel2,250,true);
        if (auto* icon = CCSprite::createWithSpriteFrameName("rankIcon_1_001.png")) {
            corum::ranked::ui::fitRankSprite(icon,{24,24}); icon->setPosition({15,15}); leaderboardFace->addChild(icon,5);
        }
        auto* leaderboard = CCMenuItemSpriteExtra::create(leaderboardFace,this,menu_selector(CorumRankedLayer::onLeaderboard));
        leaderboard->setPosition({rightCenter.x + 73,rightCenter.y + 78});
        leaderboard->setID("ranked-leaderboard-trophy"_spr); m_menu->addChild(leaderboard);
        auto* guideFace=corum::ranked::ui::surface({30,30},{},kGold,kDeepPanel2,250,true);
        auto* guideBadge=corum::ranked::ui::createAnimatedRankBadge("GOLD",{25,25},corum::ranked::ui::RankBadgeDisplay::Matched);
        guideBadge->setPosition({15,15});guideFace->addChild(guideBadge,5);
        auto* guide=CCMenuItemSpriteExtra::create(guideFace,this,menu_selector(CorumRankedLayer::onTierGuide));
        guide->setPosition({rightCenter.x-73,rightCenter.y+78});guide->setID("ranked-tier-guide-gold"_spr);m_menu->addChild(guide);
        auto* actionTitle = makeLabel(tr("matchmaking", "MATCHMAKING"), 0.34f, {rightCenter.x,rightCenter.y + 80.0f}, kCyan, "semibold");
        corum::ranked::ui::fitMenuLabelWidth(actionTitle,96,0.34f,0.24f);
        m_root->addChild(actionTitle, 4);

        if (view.stage == RuntimeStage::Ready) {
            auto* playHalo = makeNeonPanel(
                {86.0f, 86.0f},
                {rightCenter.x, rightCenter.y + 18.0f},
                kCyan,
                {7, 20, 40},
                248
            );
            m_root->addChild(playHalo, 2);
            auto* playCircle = CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png");
            playCircle->setScale(0.70f);
            auto* play = CCMenuItemSpriteExtra::create(
                playCircle,
                this,
                view.placement.required
                    ? (view.placement.active && view.placement.state == "STAGE_RESULT"
                        ? menu_selector(CorumRankedLayer::onContinuePlacement)
                        : menu_selector(CorumRankedLayer::onOpenPlacement))
                    : menu_selector(CorumRankedLayer::onJoin)
            );
            play->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            play->setEnabled(view.soloEnabled || view.placement.active);
            if (!play->isEnabled()) { corum::ranked::ui::grayButtonVisual(playCircle); play->setCascadeOpacityEnabled(true); play->setOpacity(95); }
            m_menu->addChild(play);
            auto* actionText = makeLabel(
                view.placement.required
                    ? (view.placement.active && view.placement.state == "STAGE_RESULT"
                        ? tr("continue-placement", "CONTINUE PLACEMENT")
                        : tr("start-placement", "START PLACEMENT"))
                    : tr("start-queue", "START QUEUE"),
                0.44f,
                {rightCenter.x, rightCenter.y - 40.0f},
                kAccent,
                "black"
            );
            corum::ranked::ui::fitMenuLabelWidth(actionText,170,0.44f,0.26f);
            m_root->addChild(actionText, 4);
            auto* actionHint = makeLabel(
                view.placement.required
                    ? (view.placement.active && view.placement.state == "STAGE_RESULT"
                        ? fmt::format("STAGE {} / {} SAVED", view.placement.currentStageIndex, view.placement.totalStages)
                        : tr("choose-first-pool", "CHOOSE YOUR FIRST STAGE POOL"))
                    : tr("find-match", "FIND A 1V1 MATCH"),
                0.27f,
                {rightCenter.x, rightCenter.y - 68.0f},
                corum::ranked::ui::TextMuted,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(actionHint,170,0.27f,0.19f);
            m_root->addChild(actionHint, 4);
        } else if (view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue) {
            auto* spinner = LoadingSpinner::create(58.0f);
            spinner->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            m_root->addChild(spinner, 4);
            auto* queueing = makeLabel(tr("searching", "SEARCHING..."), 0.44f, {rightCenter.x, rightCenter.y - 40.0f}, kGold, "black");
            m_root->addChild(queueing, 4);
            addButton(tr("leave-queue", "LEAVE QUEUE"), {rightCenter.x, rightCenter.y - 75.0f}, menu_selector(CorumRankedLayer::onLeave), true, 0.56f);
        } else if (view.stage == RuntimeStage::Loading) {
            auto* spinner = LoadingSpinner::create(58.0f);
            spinner->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            m_root->addChild(spinner, 4);
            auto* loading = makeLabel(tr("syncing", "SYNCING..."), 0.44f, {rightCenter.x, rightCenter.y - 40.0f}, kGold, "black");
            m_root->addChild(loading, 4);
        } else {
            auto* failed = makeLabel(tr("connection-issue", "CONNECTION ISSUE"), 0.34f, {rightCenter.x, rightCenter.y - 12.0f}, kRed, "black");
            m_root->addChild(failed, 4);
            addButton(tr("retry", "RETRY"), {rightCenter.x, rightCenter.y - 58.0f}, menu_selector(CorumRankedLayer::onRetry), false, 0.66f);
        }

        } else {
            m_lobbyPanel->addChild(makeNeonPanel({188,210},rightCenter,kCyan,kDeepPanel,246),1);
            auto* heading = makeLabel(tr("matchmaking", "MATCHMAKING"),0.34f,{94,80},kCyan,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(heading,116,0.34f,0.24f); m_root->addChild(heading,5);
            auto* face = corum::ranked::ui::surface({30,30},{},kGold,kDeepPanel2,250,true);
            if (auto* icon = CCSprite::createWithSpriteFrameName("rankIcon_1_001.png")) {
                corum::ranked::ui::fitRankSprite(icon,{24,24}); icon->setPosition({15,15}); face->addChild(icon,5);
            }
            auto* leaderboard = CCMenuItemSpriteExtra::create(face,this,menu_selector(CorumRankedLayer::onLeaderboard));
            leaderboard->setPosition({167,78}); leaderboard->setID("survival-leaderboard-trophy"_spr); m_menu->addChild(leaderboard);
            m_root->addChild(makeNeonPanel({86,86},{94,18},kCyan,{7,20,40},248),2);
            auto* playCircle = CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png"); playCircle->setScale(0.70f);
            auto const online=view.survivalEnabled && view.survival.onlineAvailable && view.stage==RuntimeStage::Ready;
            if(!online)corum::ranked::ui::grayButtonVisual(playCircle);
            auto* queue = CCMenuItemSpriteExtra::create(playCircle,this,menu_selector(CorumRankedLayer::onSurvivalQueue));
            queue->setPosition({94,18}); m_menu->addChild(queue);
            queue->setEnabled(online); queue->setCascadeOpacityEnabled(true); queue->setOpacity(online?255:100);
            auto* hint = makeLabel(tr("start-queue", "START QUEUE"),0.44f,{94,-40},online?kCyan:corum::ranked::ui::TextMuted,"black");
            corum::ranked::ui::fitMenuLabelWidth(hint,170,0.44f,0.26f); m_root->addChild(hint,5);
            auto* details = makeLabel(online?fmt::format(fmt::runtime(tr("survival-online-ready-format","2-{} PLAYERS")),view.survivalPlayerCount):tr("mode-disabled","MODE DISABLED"),0.27f,{94,-68},corum::ranked::ui::TextMuted,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(details,170,0.27f,0.19f); m_root->addChild(details,5);
            if (!view.survival.loaded || !view.survival.error.empty()) {
                addButton(tr("retry","RETRY"),{94,-88},menu_selector(CorumRankedLayer::onRetrySurvival),false,0.44f);
            }
        }
        m_root = savedRoot; m_menu = savedMenu;
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

    void renderLobbyTools(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        m_lobbyToolbar = CCMenu::create(); m_lobbyToolbar->setPosition(CCPointZero);
        m_lobbyToolbar->setID("ranked-mode-toolbar"_spr); m_root->addChild(m_lobbyToolbar,20);
        auto* savedMenu = m_menu; m_menu = m_lobbyToolbar;
        auto const soloHistory = m_modeTransition.selected() == corum::ranked::RankedMode::SoloRanked;
        auto* historyVisual = makeActionButtonVisual(tr("history", "HISTORY"),false,soloHistory);
        historyVisual->setScale(0.66f);
        auto* history = CCMenuItemSpriteExtra::create(historyVisual,this,menu_selector(CorumRankedLayer::onHistory));
        auto const historyWidth = history->getContentSize().width;
        history->setPosition({size.width - 20 - historyWidth / 2,size.height - 28}); history->setID("ranked-lobby-history"_spr); m_menu->addChild(history);
        auto* trophyVisual = makeActionButtonVisual(tr("achievement-trophy", "TROPHY")); trophyVisual->setScale(0.66f);
        auto* trophy = CCMenuItemSpriteExtra::create(trophyVisual,this,menu_selector(CorumRankedLayer::onAchievements));
        trophy->setPosition({size.width - 28 - historyWidth - trophy->getContentSize().width / 2,size.height - 28}); trophy->setID("ranked-achievement-trophy"_spr); m_menu->addChild(trophy);
        auto const badgeText = corum::ranked::achievementBadgeText(view.achievements.unclaimedRewardCount);
        if (!badgeText.empty()) {
            auto const trophySize = trophyVisual->getContentSize();
            auto* badge = corum::ranked::ui::menuPill(badgeText,{badgeText == "99+" ? 38.0f : 26.0f,19},
                {trophySize.width - 5,trophySize.height - 4},kRed,0.27f,corum::ranked::ui::Text,true);
            badge->setID("ranked-achievement-notification-badge"_spr); trophyVisual->addChild(badge,10);
        }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (view.stage == RuntimeStage::Ready) {
            auto* debug = addButton(tr("debug","DEBUG"),{96,30},menu_selector(CorumRankedLayer::onDebugMenu),true,0.54f);
            debug->setID("ranked-lobby-debug"_spr);
            debug->setVisible(m_modeTransition.selected().has_value());
        }
#endif
        m_lobbyToolbar->setVisible(true);
        m_menu = savedMenu;
        auto const layout=corum::ranked::modeLobbyLayout(size.width,size.height);
        auto const left=layout.leftX-94*layout.scale,right=layout.rightX+94*layout.scale;
        float textLeft=left+10,textRight=right-108;
        if(auto* debug=typeinfo_cast<CCMenuItemSpriteExtra*>(m_lobbyToolbar->getChildByID("ranked-lobby-debug"_spr))) {
            debug->setPosition({left+8+debug->getContentSize().width/2,27});
            if(debug->isVisible())textLeft=left+16+debug->getContentSize().width;
        }
        if(auto* settings=m_settingsMenu?m_settingsMenu->getChildByID("ranked-settings-button"_spr):nullptr)
            settings->setPosition({right-8-settings->getContentSize().width/2,27});
        auto detail=lobbyMessage(view);
        auto* statusPanel=makeNeonPanel({right-left,34},{(left+right)/2,27},view.error.empty()?kBlue:kRed,kDeepPanel2,238);
        m_root->addChild(statusPanel,1);
        auto* status=makeLabel(shorten(detail,90),.24f,{(textLeft+textRight)/2,27},view.error.empty()?ccc3(220,228,244):kRed,"semibold");
        corum::ranked::ui::fitMenuLabelWidth(status,std::max(80.0f,textRight-textLeft),.24f,.16f);
        m_mainStatusLabel=status;m_root->addChild(status,4);
    }

    void resumeModeSelectionAfterQueue(RuntimeView const& view) {
        if (!m_returnToModesAfterQueue) return;
        // Match assignment wins a concurrent Leave. Keep the recovery path.
        if (view.stage == RuntimeStage::Matched) { m_returnToModesAfterQueue = false; return; }
        if (view.stage != RuntimeStage::Ready || m_page != Page::Live || m_uiFadingOut ||
            m_modeTransition.moving() || !m_lobbyPanel) return;
        m_returnToModesAfterQueue = false;
        auto const mode = m_modeTransition.selected();
        if (mode && m_modeTransition.back()) animateModeLobby(*mode,false);
    }

    void updateModeLobbyHeader() {
        auto const selected = m_modeTransition.selected();
        if (m_lobbyToolbar) {
            m_lobbyToolbar->setVisible(true);
            if (auto* history = typeinfo_cast<CCMenuItemSpriteExtra*>(m_lobbyToolbar->getChildByID("ranked-lobby-history"_spr))) {
                auto const solo = selected == corum::ranked::RankedMode::SoloRanked;
                auto* visual = makeActionButtonVisual(tr("history","HISTORY"),false,solo);
                visual->setScale(0.66f); history->setNormalImage(visual); history->updateSprite();
                auto const size = CCDirector::sharedDirector()->getWinSize();
                auto const historyWidth = history->getContentSize().width;
                history->setPosition({size.width - 20 - historyWidth / 2,size.height - 28});
                if (auto* trophy = m_lobbyToolbar->getChildByID("ranked-achievement-trophy"_spr))
                    trophy->setPosition({size.width - 28 - historyWidth - trophy->getContentSize().width / 2,size.height - 28});
            }
            if (auto* debug = m_lobbyToolbar->getChildByID("ranked-lobby-debug"_spr))
                debug->setVisible(selected.has_value());
        }
        if(m_mainStatusLabel) {
            auto const size=CCDirector::sharedDirector()->getWinSize();auto const layout=corum::ranked::modeLobbyLayout(size.width,size.height);
            auto const left=layout.leftX-94*layout.scale,right=layout.rightX+94*layout.scale;
            auto textLeft=left+10.0f;
            if(auto* debug=m_lobbyToolbar?m_lobbyToolbar->getChildByID("ranked-lobby-debug"_spr):nullptr)
                if(debug->isVisible())textLeft=left+16+debug->getContentSize().width;
            auto const textRight=right-108;m_mainStatusLabel->setPosition({(textLeft+textRight)/2,27});
            corum::ranked::ui::fitMenuLabelWidth(m_mainStatusLabel,std::max(80.0f,textRight-textLeft),.24f,.16f);
        }
    }

    void animateModeLobby(corum::ranked::RankedMode mode, bool opening) {
        updateModeLobbyHeader();
        auto const layout = corum::ranked::modeLobbyLayout(CCDirector::sharedDirector()->getWinSize().width,CCDirector::sharedDirector()->getWinSize().height);
        constexpr float duration = 0.48f;
        if(m_lobbyHover)m_lobbyHover->setActive(false);
        if(opening && m_lobbyContents) {
            corum::ranked::ui::setModeLobbyOpacity(m_lobbyContents,0);
            m_lobbyContents->setVisible(false);
        }
        auto* other = mode == corum::ranked::RankedMode::Survival ? m_lobbySolo : m_lobbySurvival;
        auto* selectedCard = mode == corum::ranked::RankedMode::Survival ? m_lobbySurvival : m_lobbySolo;
        auto* entryControls = typeinfo_cast<CCLayerColor*>(selectedCard->getChildByID("ranked-mode-entry-controls"_spr));
        setLobbyNodeInput(m_lobbySurvival,false); setLobbyNodeInput(m_lobbySolo,false); setLobbyNodeInput(m_lobbyPanel,false);
        if (m_menu) { corum::ranked::ui::cancelMenuTouch(m_menu); m_menu->setTouchEnabled(false); }
        if (m_settingsMenu) { corum::ranked::ui::cancelMenuTouch(m_settingsMenu); m_settingsMenu->setTouchEnabled(false); }
        setLobbyNodeInput(m_lobbyToolbar,false);
        if (entryControls) entryControls->setVisible(true);
        selectedCard->setZOrder(5);
        m_lobbyPanel->setZOrder(4);
        other->setZOrder(3);
        selectedCard->setVisible(true);
        other->setVisible(true);
        m_lobbyPanel->setVisible(true);
        m_renderedUiKey = presentationKey();
        auto* motion = corum::ranked::ui::ModeLobbySlide::create(duration,m_lobbySurvival,m_lobbySolo,layout,mode,opening);
        auto* sequence=CCSequence::create(CCEaseSineInOut::create(motion), CallFuncExt::create([weak=WeakRef(this),opening,mode] {
            auto self=weak.lock();if(!self || !self->getParent())return;
            auto finish=[weak]{if(auto owner=weak.lock();owner && owner->getParent()){
                owner->m_modeTransition.finish();owner->syncLobbyInput();owner->refreshLobbyHover();}};
            if(opening) {
                auto* hidden=mode==corum::ranked::RankedMode::Survival?self->m_lobbySolo:self->m_lobbySurvival;hidden->setVisible(false);
                auto* card=mode==corum::ranked::RankedMode::Survival?self->m_lobbySurvival:self->m_lobbySolo;
                if(auto* controls=card->getChildByID("ranked-mode-entry-controls"_spr))controls->setVisible(false);
                if(self->m_lobbyContents) {
                    corum::ranked::ui::fadeModeLobbySurfaces(self->m_lobbyContents,.18f,true);
                    self->m_lobbyContents->setVisible(true);
                    self->m_lobbyContents->runAction(CCSequence::create(
                        CCEaseSineOut::create(corum::ranked::ui::ModeLobbyFade::create(.18f,255)),CallFuncExt::create(finish),nullptr));
                }
                else finish();
            } else {self->m_lobbyPanel->setVisible(false);finish();}
        }),nullptr);
        if(!opening && m_lobbyContents) {
            corum::ranked::ui::fadeModeLobbySurfaces(m_lobbyContents,.18f,false);
            auto keep=Ref<CCAction>(sequence);
            m_lobbyContents->runAction(CCSequence::create(CCEaseSineIn::create(corum::ranked::ui::ModeLobbyFade::create(.18f,0)),
                CallFuncExt::create([weak=WeakRef(this),keep]{if(auto self=weak.lock();self && self->m_lobbyPanel){
                    if(self->m_lobbyContents)self->m_lobbyContents->setVisible(false);
                    self->m_lobbyPanel->runAction(keep.data());}}),nullptr));
        } else m_lobbyPanel->runAction(sequence);
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
        addTopBack(true); addLanguageSelector();
        addScreenHeader("RANKED","",kGold,kCyan);
        if(auto* population=corum::ranked::ui::LobbyPopulationView::create()){
            population->setID("ranked-lobby-population"_spr);population->setPosition({size.width/2,size.height-53});
            population->updatePopulation({view.lobbyOffline,view.lobbyOnline,view.lobbyPopulationKnown});m_root->addChild(population,7);
        }
        if (view.stage != RuntimeStage::Ready && view.stage != RuntimeStage::Queued && view.stage != RuntimeStage::JoiningQueue) {
            auto* message = makeLabel(lobbyMessage(view),0.30f,{size.width/2,size.height/2},kGold,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(message,size.width-60,0.30f,0.17f); m_root->addChild(message,5);
            if (view.stage == RuntimeStage::Loading) { auto* spinner = LoadingSpinner::create(42); spinner->setPosition({size.width/2,size.height/2-45}); m_root->addChild(spinner,5); }
            else addButton(tr("retry","RETRY"),{size.width/2,size.height/2-45},menu_selector(CorumRankedLayer::onRetry),false,0.65f);
            return;
        }
        auto const layout = corum::ranked::modeLobbyLayout(size.width,size.height);
        auto const selected = m_modeTransition.selected();
        m_lobbySurvival = makeLobbyGroup({selected == corum::ranked::RankedMode::SoloRanked ? layout.rightX : layout.leftX,layout.centerY},layout.scale,"survival-profile-card"_spr);
        m_lobbySolo = makeLobbyGroup({selected == corum::ranked::RankedMode::SoloRanked ? layout.leftX : layout.rightX,layout.centerY},layout.scale,"solo-ranked-profile-card"_spr);
        m_lobbyPanel = makeLobbyGroup({(selected ? layout.rightX : layout.leftX) - 94 * layout.scale,layout.centerY},layout.scale,"ranked-mode-menu-panel"_spr);
        renderModeCard(view,corum::ranked::RankedMode::Survival,m_lobbySurvival);
        renderModeCard(view,corum::ranked::RankedMode::SoloRanked,m_lobbySolo);
        renderLobbyTools(view);
        if (selected) {
            renderModePanel(view,*selected);
            auto* hidden = *selected == corum::ranked::RankedMode::Survival ? m_lobbySolo : m_lobbySurvival;
            corum::ranked::ui::setModeLobbyOpacity(hidden,0); hidden->setVisible(false); hidden->setZOrder(3);
            auto* card = *selected == corum::ranked::RankedMode::Survival ? m_lobbySurvival : m_lobbySolo;
            card->setZOrder(5); m_lobbyPanel->setZOrder(4);
        } else { m_lobbyPanel->setVisible(false); }
        syncLobbyInput();refreshLobbyHover();
    }

    void renderPlacementMap(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& placement = view.placement;
        auto const& match = view.match;
        if (match.state == "MAP_PREPARE") {
            m_placementPrepareTimerLabel = makeLabel("", 0.27f, {size.width - 16, size.height - 16}, kGold, "extrabold");
            m_placementPrepareTimerLabel->setAnchorPoint({1, 1});
            m_placementPrepareTimerLabel->setID("ranked-placement-prepare-timer"_spr);
            m_root->addChild(m_placementPrepareTimerLabel, 20);
        }
        auto const pool = !placement.stagePools.empty()
            ? placement.stagePools.back()
            : placement.startingPool;
        addScreenHeader(
            fmt::format("STAGE {} · MAP {}", placement.currentStageIndex, placement.currentMapIndex),
            fmt::format("POOL {} · {}", pool, tr("solo-placement", "SOLO PLACEMENT")),
            corum::ranked::ui::Text,
            kCyan
        );

        auto* panel = makeNeonPanel(
            {std::min(388.0f, size.width - 74.0f), 174.0f},
            {size.width / 2.0f, size.height / 2.0f - 5.0f},
            match.state == "MAP_SETTLING" ? kGold : kBlue,
            kDeepPanel,
            244
        );
        m_root->addChild(panel, 1);

        if (match.currentMap) {
            corum::ranked::ui::addRankedMapHeading(m_root,match.currentMap->levelId,match.currentMap->title,match.currentMap->metadata,
                {size.width/2,size.height/2+58},difficultyColor(match.currentMap->difficulty));
        }

        auto* grade = corum::ranked::ui::PlacementGradeBadge::create(48.0f);
        if (grade) {
            grade->setGrade(placement.currentMapGrade, false);
            grade->setPosition({size.width / 2.0f - 76.0f, size.height / 2.0f - 15.0f});
            m_root->addChild(grade, 5);
        }
        auto* score = makeLabel(
            fmt::format("{:.1f} / 500", placement.provisionalMapScore),
            0.49f,
            {size.width / 2.0f + 70.0f, size.height / 2.0f - 4.0f},
            kGold,
            "black"
        );
        m_root->addChild(score, 5);
        addClearChecks(placement.currentMapClears, {size.width / 2.0f + 70.0f, size.height / 2.0f - 34.0f});

        if (match.state == "MAP_PREPARE") {
            addPreparationStatusRows(
                {size.width / 2.0f - 80.0f, size.height / 2.0f - 63.0f},
                {size.width / 2.0f + 80.0f, size.height / 2.0f - 63.0f},
                144.0f
            );
            m_prepareLoadPercentLabel = makeLabel("0%", 0.18f,
                {size.width / 2.0f, size.height / 2.0f - 82.0f}, kCyan, "black");
            m_root->addChild(m_prepareLoadPercentLabel, 5);
            addRoundOptions(59.0f);
        }
        std::string stateText = preparationStageText();
        if (match.state == "MAP_PLAYING") stateText = tr("placement-map-live", "MAP LIVE · SCORE + GRADE UPDATE IN REAL TIME");
        if (match.state == "MAP_SETTLING") stateText = tr("final-attempt-progress", "FINAL ATTEMPT IN PROGRESS");
        auto* state = makeLabel(
            stateText,
            0.25f,
            {size.width / 2.0f, 34.0f},
            match.state == "MAP_SETTLING" ? kGold : kCyan,
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(state, size.width - 90.0f, 0.25f, 0.16f);
        m_root->addChild(state, 5);
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
            kCyan
        );

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, std::string const& scoreText, float x, bool final) {
            auto* group = CCNode::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kBlue, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kCyan, "semibold");
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
        m_root->addChild(makeLabel(">", 0.46f, {center + 56.0f, size.height / 2.0f + 4.0f}, kCyan, "black"), 6);

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
        addScreenHeader(
            fmt::format("STAGE {} {}", result.stageIndex, tr("result", "RESULT")),
            fmt::format("POOL {}", result.pool),
            kGold,
            kCyan
        );

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, double scoreValue, float x, bool final) {
            auto* group = CCNodeRGBA::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            group->setCascadeColorEnabled(true);
            group->setCascadeOpacityEnabled(true);
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kBlue, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kCyan, "semibold");
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
        addPlacementProgress(result.stageIndex, view.placement.totalStages, {center, 67.0f}, 18.0f, 0.27f);
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
            {center + (finalStage ? 0.0f : 72.0f), 23.0f},
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
                {center - 72.0f, 23.0f},
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
        addScreenHeader(
            tr("placement-complete", "PLACEMENT COMPLETE"),
            finalResult.skipped
                ? tr("placement-skipped", "PLACEMENT SKIPPED")
                : fmt::format("{} MAPS · {} STAGES", view.placement.totalStages * view.placement.mapsPerStage, view.placement.totalStages),
            kGold,
            kCyan
        );
        auto* badge = corum::ranked::ui::createAnimatedRankBadge(
            finalResult.displayedTier,
            {112.0f, 112.0f},
            corum::ranked::ui::RankBadgeDisplay::Hero
        );
        if (badge) {
            badge->setPosition({size.width / 2.0f, size.height / 2.0f + 28.0f});
            m_root->addChild(badge, 5);
        }
        auto* tier = makeLabel(
            fmt::format("{} - {} LP", corum::ranked::rankTierDisplay(upper(finalResult.displayedTier)), finalResult.visibleRankedScore),
            0.55f,
            {size.width / 2.0f, size.height / 2.0f - 49.0f},
            tierColor(finalResult.displayedTier),
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(tier, size.width - 100.0f, 0.55f, 0.32f);
        m_root->addChild(tier, 5);
        auto* placed = makeLabel(tr("placed", "PLACED"), 0.38f, {size.width / 2.0f, size.height / 2.0f - 78.0f}, kGreen, "black");
        m_root->addChild(placed, 5);
        if (!view.error.empty()) {
            auto* error = makeLabel(
                shorten(view.error, 72),
                0.22f,
                {size.width / 2.0f, 51.0f},
                kRed,
                "semibold"
            );
            corum::ranked::ui::fitMenuLabelWidth(error, size.width - 100.0f, 0.22f, 0.15f);
            m_root->addChild(error, 5);
        }
        addButton(tr("continue", "CONTINUE"), {size.width / 2.0f, 26.0f}, menu_selector(CorumRankedLayer::onResultClose), false, 0.60f);

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

    void renderMatchFound(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        addPlayerCard({100.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerAProfile, ownA, match.playerAAccountId);
        addPlayerCard({size.width - 100.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBProfile, !ownA, match.playerBAccountId);

        addScreenHeader(
            tr("match-found", "MATCH FOUND"),
            tr("private-ban-phase", "PRIVATE BAN PHASE"),
            corum::ranked::ui::Text,
            kCyan
        );

        auto* versus = makeLabel("VS", 0.40f, {size.width / 2.0f, size.height / 2.0f + 61.0f}, kGold, "display");
        m_root->addChild(versus, 5);

        auto const left = std::max(0, 5 - static_cast<int>(std::floor(phaseSeconds())));
        auto* countdownPlate = makeNeonPanel({116.0f, 58.0f}, {size.width / 2.0f, size.height / 2.0f - 2.0f}, left <= 2 ? kSideRed : kBlue, {7, 17, 36}, 236);
        m_root->addChild(countdownPlate, 2);
        auto* phase = makeLabel(tr("map-ban-in", "MAP BAN IN"), 0.27f, {size.width / 2.0f, size.height / 2.0f + 14.0f}, kCyan, "semibold");
        m_root->addChild(phase, 5);
        auto* countdown = makeLabel(fmt::format("{}", left), 0.64f, {size.width / 2.0f, size.height / 2.0f - 8.0f}, left <= 2 ? kSideRed : kGold, "black");
        m_matchFoundCountdownLabel = countdown;
        m_root->addChild(countdown, 5);
        auto* hint = makeLabel(tr("prepare-private-ban", "GET READY TO CHOOSE A PRIVATE BAN"), 0.24f, {size.width / 2.0f, size.height / 2.0f - 35.0f}, ccc3(210, 219, 236), "semibold");
        corum::ranked::ui::fitMenuLabelWidth(hint, 176.0f, 0.24f, 0.16f);
        m_root->addChild(hint, 5);
    }

    void renderBan(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);

        addScreenHeader(
            tr("ban-map", "BAN MAP"),
            tr("choose-map-remove", "CHOOSE ONE MAP TO REMOVE"),
            corum::ranked::ui::Text,
            kCyan
        );

        auto const remaining = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
        auto* timerPlate = makeNeonPanel({54.0f, 48.0f}, {size.width - 42.0f, size.height - 41.0f}, remaining <= 3 ? kSideRed : kBlue, {8, 18, 40}, 250);
        m_root->addChild(timerPlate, 3);
        auto* timer = makeLabel(fmt::format("{}", remaining), 0.58f, {size.width - 42.0f, size.height - 34.0f}, remaining <= 3 ? kSideRed : ccc3(245, 248, 255), "black");
        m_banTimerLabel = timer;
        m_root->addChild(timer, 6);
        auto* timerCaption = makeLabel(tr("time-left", "TIME LEFT"), 0.24f, {size.width - 42.0f, size.height - 56.0f}, kCyan, "semibold");
        m_root->addChild(timerCaption, 6);

        auto const count = std::min<std::size_t>(5, match.candidateMaps.size());
        auto const cardWidth = (size.width - 34.0f) / 5.0f;
        auto const cardHeight = std::min(170.0f, size.height - 122.0f);
        auto const startX = 17.0f + cardWidth / 2.0f;
        auto const centerY = size.height / 2.0f + 2.0f;
        for (std::size_t i = 0; i < count; ++i) {
            auto const x = startX + cardWidth * static_cast<float>(i);
            auto const& candidate = match.candidateMaps[i];
            auto const diffColor = difficultyColor(candidate.difficulty);
            auto const selected = m_selectedBan == candidate.canonicalLevelId;
            auto const pending = m_pendingBan == candidate.canonicalLevelId;
            auto const accent = selected ? kRed : (pending ? kGold : kBlue);

            auto* panel = makeNeonPanel({cardWidth - 5.0f, cardHeight}, {x, centerY}, accent, kDeepPanel, 248);
            m_root->addChild(panel, 1);

            auto* mapIndex = makeLabel(
                fmt::format("{} {:02d}", tr("map", "MAP"), static_cast<int>(i + 1)),
                0.22f,
                {x, centerY + cardHeight / 2.0f - 13.0f},
                selected ? kRed : (pending ? kGold : corum::ranked::ui::TextMuted),
                "semibold"
            );
            m_root->addChild(mapIndex, 5);

            // Decorative map emblem. This stays entirely inside base GD assets and
            // gives each card a stronger visual anchor than title-only rectangles.
            auto* emblemBack = makeNeonPanel({40.0f, 40.0f}, {x, centerY + 47.0f}, diffColor, {5, 12, 27}, 250);
            m_root->addChild(emblemBack, 2);
            auto* emblem = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
            if (emblem) {
                emblem->setScale(0.34f);
                emblem->setColor(diffColor);
                emblem->setPosition({x, centerY + 47.0f});
                m_root->addChild(emblem, 4);
            }

            auto* map = makeLabel(candidate.title, 0.31f, {x, centerY + 13.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitFullMenuLabel(map, cardWidth - 12.0f, 26.0f, 0.31f);
            m_root->addChild(map, 4);

            auto* difficultyPlate = makeNeonPanel({cardWidth - 22.0f, 38.0f}, {x, centerY - 20.0f}, diffColor, {11, 20, 36}, 245);
            m_root->addChild(difficultyPlate, 2);
            addMapDifficulty(candidate, {x, centerY - 12}, cardWidth - 28, 18);
            auto const lengthMetadata=corum::ranked::ui::displayedMapMetadata(candidate.levelId,candidate.metadata);
            auto* diff = makeLabel(corum::ranked::mapLengthText(lengthMetadata), 0.25f, {x, centerY - 27.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(diff, cardWidth - 28.0f, 0.25f, 0.19f);
            m_root->addChild(diff, 4);

            auto const actionY = centerY - cardHeight / 2.0f + 21.0f;
            if (selected) {
                auto* state = makeTextPlate(tr("banned", "BANNED"), {cardWidth - 18.0f, 24.0f}, {x, actionY}, kRed, 0.18f, kRed);
                m_root->addChild(state, 3);
            } else if (pending) {
                auto* state = makeTextPlate(tr("banning", "BANNING..."), {cardWidth - 18.0f, 24.0f}, {x, actionY}, kGold, 0.16f, kGold);
                m_root->addChild(state, 3);
            } else if (m_selectedBan.empty() && m_pendingBan.empty()) {
                auto* button = addButton(tr("ban", "BAN"), {x, actionY}, menu_selector(CorumRankedLayer::onBan), true, 0.46f);
                button->setTag(static_cast<int>(i));
            }
        }

        auto* privacyPlate = makeNeonPanel({248.0f, 36.0f}, {size.width / 2.0f, 24.0f}, kBlue, {8, 18, 36}, 242);
        m_root->addChild(privacyPlate, 1);
        auto* privacy = makeLabel(tr("private-ban-notice", "YOUR BAN IS PRIVATE  |  REVEALED WHEN THE PHASE ENDS"), 0.25f, {size.width / 2.0f, 24.0f}, ccc3(215, 225, 242), "semibold");
        corum::ranked::ui::fitMenuLabelWidth(privacy, 236.0f, 0.25f, 0.16f);
        m_root->addChild(privacy, 4);
    }

    void renderPrepare(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto const deathmatch = match.deathmatchSequence > 0;

        // A/B cards keep the screen symmetric while the centre column is reserved
        // for match state, map information and resource readiness.
        addPlayerCard({96.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerAProfile, ownA, match.playerAAccountId);
        addPlayerCard({size.width - 96.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBProfile, !ownA, match.playerBAccountId);

        auto* title = makeLabel(
            deathmatch ? tr("death-match", "DEATH MATCH") : fmt::format("ROUND {}", match.roundNumber),
            deathmatch ? 0.62f : 0.56f,
            {size.width / 2.0f, size.height - 27.0f},
            ccc3(245, 248, 255),
            "black"
        );
        corum::ranked::ui::fitMenuLabelWidth(title, size.width - 180.0f, deathmatch ? 0.62f : 0.56f, 0.34f);
        m_root->addChild(title, 6);

        auto displayDeathmatchUsedA = match.deathmatchAttemptsUsedA;
        auto displayDeathmatchUsedB = match.deathmatchAttemptsUsedB;
        if (deathmatch) {
            auto const localOwnUsed = RankedRuntime::get().localDeathmatchVisualAttemptsUsed();
            if (ownA) displayDeathmatchUsedA = std::max(displayDeathmatchUsedA, localOwnUsed);
            else displayDeathmatchUsedB = std::max(displayDeathmatchUsedB, localOwnUsed);

            auto* sub = makeLabel(tr("three-attempts", "3 ATTEMPTS"), 0.34f, {size.width / 2.0f, size.height - 52.0f}, kCyan, "black");
            m_root->addChild(sub, 6);
            auto* attemptsPlate = makeNeonPanel({132.0f, 24.0f}, {size.width / 2.0f, size.height - 72.0f}, kBlue, {8, 18, 38}, 245);
            m_root->addChild(attemptsPlate, 2);
            auto* attempts = makeLabel(
                fmt::format("A  {}/3    |    B  {}/3", displayDeathmatchUsedA, displayDeathmatchUsedB),
                0.26f,
                {size.width / 2.0f, size.height - 72.0f},
                ccc3(226, 232, 245),
                "semibold"
            );
            m_root->addChild(attempts, 6);
        } else if (!match.banner.empty() && match.banner != "NONE") {
            auto* sub = makeTextPlate(
                displayBanner(match.banner),
                {132.0f, 24.0f},
                {size.width / 2.0f, size.height - 57.0f},
                match.banner == "TIEBREAKER" ? kGold : kSideRed,
                0.20f,
                match.banner == "TIEBREAKER" ? kGold : kSideRed,
                "goldFont.fnt"
            );
            m_root->addChild(sub, 3);
        }

        // Series score is deliberately compact. It represents round wins in the
        // Bo3, never the score accumulated inside the current map.
        auto const seriesY = deathmatch ? size.height - 94.0f : size.height - 84.0f;
        auto* seriesPlate = makeNeonPanel({116.0f, 27.0f}, {size.width / 2.0f, seriesY}, kBlue, {7, 17, 35}, 245);
        m_root->addChild(seriesPlate, 2);
        auto* seriesCaption = makeLabel(tr("series", "SERIES"), 0.23f, {size.width / 2.0f, seriesY + 7.0f}, kCyan, "semibold");
        m_root->addChild(seriesCaption, 6);
        auto* seriesScore = makeLabel(
            fmt::format("{}   :   {}", match.roundWinsA, match.roundWinsB),
            0.33f,
            {size.width / 2.0f, seriesY - 5.0f},
            ccc3(245, 248, 255),
            "black"
        );
        m_root->addChild(seriesScore, 6);

        auto const mapY = size.height / 2.0f + 41.0f;
        if (match.currentMap) {
            auto* crown = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
            if (crown) {
                crown->setScale(0.24f);
                crown->setColor(kCyan);
                crown->setPosition({size.width / 2.0f, mapY + 31.0f});
                m_root->addChild(crown, 4);
            }
            auto* mapName = makeLabel(match.currentMap->title, 0.40f, {size.width / 2.0f, mapY + 7.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitFullMenuLabel(mapName, 154.0f, 32.0f, 0.40f);
            m_root->addChild(mapName, 6);

            auto const diffColor = difficultyColor(match.currentMap->difficulty);
            auto* diffPlate = makeNeonPanel({146.0f, 36.0f}, {size.width / 2.0f, mapY - 27.0f}, diffColor, {12, 17, 27}, 246);
            m_root->addChild(diffPlate, 2);
            addMapDifficulty(*match.currentMap, {size.width / 2.0f, mapY - 19}, 125, 17);
            auto const lengthMetadata=corum::ranked::ui::displayedMapMetadata(match.currentMap->levelId,match.currentMap->metadata);
            auto* difficulty = makeLabel(corum::ranked::mapLengthText(lengthMetadata), 0.30f, {size.width / 2.0f, mapY - 35.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitMenuLabelWidth(difficulty, 124.0f, 0.30f, 0.20f);
            m_root->addChild(difficulty, 6);
        }

        auto& runtime = RankedRuntime::get();
        auto const clearSyncPending =
            !deathmatch &&
            runtime.localDisplayClears() >= 3 &&
            runtime.hasLocalAttemptInFlight();
        auto* infoPlate = makeNeonPanel({154.0f, 118.0f}, {size.width / 2.0f, size.height / 2.0f - 36.0f}, kBlue, kDeepPanel2, 224);
        m_root->addChild(infoPlate, 1);

        auto const mapPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 14.0f};
        auto const songPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 42.0f};
        addPreparationStatusRows(mapPos, songPos, 116.0f, clearSyncPending);

        addRoundOptions(size.height / 2.0f - 106.0f);
        auto const loadStageY = size.height / 2.0f - 66.0f;
        m_prepareLoadStageLabel = makeLabel(
            preparationStageText(),
            0.18f,
            {size.width / 2.0f, loadStageY},
            m_levelLookupError.empty() ? kCyan : kGold,
            "semibold"
        );
        corum::ranked::ui::fitMenuLabelWidth(m_prepareLoadStageLabel, 138.0f, 0.18f, 0.12f);
        m_root->addChild(m_prepareLoadStageLabel, 6);

        constexpr float prepareBarWidth = 112.0f;
        auto* prepareBarBack = CCLayerColor::create({5, 12, 25, 245});
        prepareBarBack->setContentSize({prepareBarWidth, 8.0f});
        prepareBarBack->setPosition({size.width / 2.0f - 68.0f, size.height / 2.0f - 85.0f});
        m_root->addChild(prepareBarBack, 4);
        m_prepareLoadFill = CCLayerColor::create({kCyan.r, kCyan.g, kCyan.b, 245});
        m_prepareLoadFill->setContentSize({prepareBarWidth * m_prepareLoadVisual, 8.0f});
        m_prepareLoadFill->setPosition(prepareBarBack->getPosition());
        m_prepareLoadWidth = prepareBarWidth;
        m_root->addChild(m_prepareLoadFill, 5);
        m_prepareLoadPercentLabel = makeLabel(
            fmt::format("{}%", static_cast<int>(std::round(m_prepareLoadVisual * 100.0f))),
            0.20f,
            {size.width / 2.0f + 61.0f, size.height / 2.0f - 81.0f},
            ccc3(235, 242, 255),
            "black"
        );
        m_root->addChild(m_prepareLoadPercentLabel, 6);

        auto const seconds = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
        std::string footer = fmt::format("{}  {:02d}", tr("starts-in", "STARTS IN"), seconds);
        ccColor3B footerAccent = seconds <= 3 ? kSideRed : kBlue;
        if (clearSyncPending) {
            footer = tr("syncing-clear", "SYNCING CLEAR...");
            footerAccent = kCyan;
        } else if (match.state == "DEATHMATCH_PLAYING") {
            auto const ownUsed = ownA ? displayDeathmatchUsedA : displayDeathmatchUsedB;
            auto const opponentUsed = ownA ? displayDeathmatchUsedB : displayDeathmatchUsedA;
            footer = ownUsed >= 3
                ? fmt::format("{}   {}/3", tr("waiting-opponent", "WAITING FOR OPPONENT"), opponentUsed)
                : corum::ranked::i18n::attemptOfThree(std::min(3, ownUsed + 1));
            footerAccent = ownUsed >= 3 ? kBlue : kGold;
        } else if (phaseSeconds() >= 10.0) {
            auto const ownReady = ownA ? match.readyA : match.readyB;
            auto const opponentReady = ownA ? match.readyB : match.readyA;
            if (!ownReady) footer = corum::ranked::RankedRoundPreparation::get().ready()
                ? tr("sending-ready", "SENDING READY...") : tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
            else if (!opponentReady) footer = corum::ranked::i18n::waitingForPlayer(upper(shorten(match.opponentName, 13)));
            else footer = tr("starting", "STARTING...");
            footerAccent = kGold;
        }
        auto* footerPlate = makeTextPlate(
            footer,
            {170.0f, 28.0f},
            {size.width / 2.0f, 27.0f},
            footerAccent,
            0.31f,
            ccc3(245, 248, 255),
            "black",
            &m_prepareFooterLabel
        );
        m_root->addChild(footerPlate, 3);
    }

    void renderSpectatorWait(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto const ownScore = ownA ? match.scoreA : match.scoreB;
        auto const opponentScore = ownA ? match.scoreB : match.scoreA;
        auto const opponentAttemptActive = match.spectatorCurrentProgress.has_value();
        auto const progress = match.spectatorCurrentProgress.value_or(0);
        auto const opponent = match.spectatorOpponentName.empty()
            ? match.opponentName
            : match.spectatorOpponentName;

        auto* title = makeLabel(tr("attempt-finished", "YOUR ATTEMPT IS FINISHED"), 0.48f, {size.width / 2.0f, size.height - 34.0f}, kGold, "goldFont.fnt");
        corum::ranked::ui::fitMenuLabelWidth(title, size.width - 70.0f, 0.48f, 0.30f);
        m_root->addChild(title, 5);
        auto* subtitle = makeLabel(tr("waiting-opponent", "WAITING FOR OPPONENT"), 0.24f, {size.width / 2.0f, size.height - 59.0f}, kCyan);
        m_root->addChild(subtitle, 5);

        auto* panel = makeNeonPanel({246.0f, 142.0f}, {size.width / 2.0f, size.height / 2.0f - 4.0f}, kBlue, kDeepPanel, 238);
        m_root->addChild(panel, 1);

        auto* name = makeLabel(upper(shorten(opponent, 18)), 0.32f, {size.width / 2.0f, size.height / 2.0f + 38.0f});
        corum::ranked::ui::fitMenuLabelWidth(name, 205.0f, 0.32f, 0.21f);
        m_root->addChild(name, 5);
        auto* live = makeLabel(
            opponentAttemptActive
                ? tr("live-progress", "LIVE PROGRESS")
                : tr("waiting-start", "WAITING TO START"),
            0.20f,
            {size.width / 2.0f, size.height / 2.0f + 15.0f},
            kCyan,
            "semibold"
        );
        m_spectatorStateLabel = live;
        m_root->addChild(live, 5);
        auto* pct = makeLabel(
            opponentAttemptActive ? fmt::format("{}%", progress) : "--",
            0.74f,
            {size.width / 2.0f, size.height / 2.0f - 15.0f},
            kGold,
            "goldFont.fnt"
        );
        m_spectatorProgressLabel = pct;
        m_root->addChild(pct, 5);
        auto* score = makeLabel(
            scoreComparison(ownScore, opponentScore,match.deathmatchSequence>0),
            0.22f,
            {size.width / 2.0f, size.height / 2.0f - 53.0f},
            ccc3(218, 228, 244),
            "semibold"
        );
        m_spectatorScoreLabel = score;
        corum::ranked::ui::fitMenuLabelWidth(score, 225.0f, 0.22f, 0.16f);
        m_root->addChild(score, 5);

        std::string hintText = tr("round-resolve-hint", "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS");
        if (!opponentAttemptActive && match.state == "LAST_ATTEMPT_WINDOW") {
            auto const seconds = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
            hintText = fmt::format(fmt::runtime(tr("last-attempt-seconds","OPPONENT HAS {}s TO START THE LAST ATTEMPT")),seconds);
        }
        auto* hint = makeLabel(hintText, 0.24f, {size.width / 2.0f, 28.0f}, ccc3(195, 205, 224), "semibold");
        m_spectatorHintLabel = hint;
        corum::ranked::ui::fitMenuLabelWidth(hint, size.width - 80.0f, 0.24f, 0.16f);
        m_root->addChild(hint, 5);
    }

    void renderInterRound(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* title = makeLabel(
            match.state == "DEATHMATCH_RESULT" ? "DEATH MATCH RESULT" : "ROUND RESULT",
            0.62f,
            {size.width / 2.0f, size.height / 2.0f + 58.0f},
            ccc3(245, 248, 255),
            "goldFont.fnt"
        );
        corum::ranked::ui::fitMenuLabelWidth(title, size.width - 90.0f, 0.62f, 0.34f);
        m_root->addChild(title, 5);

        auto* series = makeNeonPanel({150.0f, 58.0f}, {size.width / 2.0f, size.height / 2.0f - 1.0f}, kBlue, {7, 17, 36}, 248);
        m_root->addChild(series, 2);
        auto* caption = makeLabel(tr("series-score", "SERIES SCORE"), 0.20f, {size.width / 2.0f, size.height / 2.0f + 14.0f}, kCyan, "semibold");
        m_root->addChild(caption, 5);
        auto* score = makeLabel(fmt::format("{}   :   {}", match.roundWinsA, match.roundWinsB), 0.45f, {size.width / 2.0f, size.height / 2.0f - 10.0f}, ccc3(245, 248, 255), "goldFont.fnt");
        m_root->addChild(score, 5);

        auto* next = makeTextPlate(tr("next-map-preparing", "NEXT MAP PREPARING..."), {166.0f, 30.0f}, {size.width / 2.0f, size.height / 2.0f - 58.0f}, kCyan, 0.21f, ccc3(225, 235, 248), "semibold");
        m_root->addChild(next, 3);
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
        auto* marksCover = CCLayerColor::create({7, 18, 40, 255});
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
                    if (owner->isRunning()) { owner->render(); owner->restoreResultInput(); }
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
        group->setPosition({size.width / 2.0f, size.height + 44.0f});
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
            CCEaseSineOut::create(CCMoveTo::create(0.32f, {size.width / 2.0f, size.height - 43.0f})),
            CCDelayTime::create(2.15f),
            CCEaseSineIn::create(CCMoveTo::create(0.28f, {size.width / 2.0f, size.height + 44.0f})),
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
        auto* close = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        close->setScale(0.60f);
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
        PlayerCardRefs cardA;
        PlayerCardRefs cardB;
        auto const cardY = size.height / 2.0f - 5.0f;
        auto const leftPerfect = match.perfectWinSide == "A";
        auto const rightPerfect = match.perfectWinSide == "B";
        addPlayerCard({100.0f, cardY}, match.playerAName, match.profileAfterA, ownA, match.playerAAccountId, &cardA, true);
        addPlayerCard({size.width - 100.0f, cardY}, match.playerBName, match.profileAfterB, !ownA, match.playerBAccountId, &cardB, true);
        if (leftPerfect) addPromotionAura({100.0f, cardY}, {150.0f, 184.0f}, true);
        if (rightPerfect) addPromotionAura({size.width - 100.0f, cardY}, {150.0f, 184.0f}, true);

        auto const perfect = leftPerfect || rightPerfect;
        auto const summaryRows = std::max<std::size_t>(1, match.rounds.size() + match.deathmatches.size());
        auto const summaryTop = size.height - (perfect ? 66.0f : 52.0f);
        auto const summaryHeight = std::clamp(34.0f + static_cast<float>(summaryRows) * 27.0f, 72.0f, 170.0f);
        auto const summaryBottom = std::max(52.0f, summaryTop - summaryHeight);
        auto* summaryPanel = makeNeonPanel(
            {164.0f, summaryTop - summaryBottom},
            {size.width / 2.0f, (summaryTop + summaryBottom) / 2.0f},
            perfect ? kGold : kBlue,
            kDeepPanel,
            214
        );
        m_root->addChild(summaryPanel, 1);

        auto* title = makeLabel(
            perfect ? "PERFECT WIN" : tr("match-complete", "MATCH COMPLETE"),
            perfect ? 0.60f : 0.52f,
            {size.width / 2.0f, size.height - 24.0f},
            perfect ? kGold : corum::ranked::ui::Text,
            "black"
        );
        m_root->addChild(title, 4);
        if (perfect) {
            auto* perfectHint = makeLabel(
                tr("maximum-rating-reward", "MAXIMUM RATING REWARD"),
                0.25f,
                {size.width / 2.0f, size.height - 49.0f},
                kGold,
                "black"
            );
            m_root->addChild(perfectHint, 5);
        }
        auto* leftFinal = makeLabel(fmt::format("{}", match.roundWinsA), 0.82f, {size.width / 2.0f - 105.0f, size.height / 2.0f + 18.0f}, match.winnerSide == "A" ? kGreen : kRed);
        auto* rightFinal = makeLabel(fmt::format("{}", match.roundWinsB), 0.82f, {size.width / 2.0f + 105.0f, size.height / 2.0f + 18.0f}, match.winnerSide == "B" ? kGreen : kRed);
        m_root->addChild(leftFinal, 4);
        m_root->addChild(rightFinal, 4);
        auto* leftResult = makeLabel(
            leftPerfect ? std::string("PERFECT WIN") : match.winnerSide == "A" ? tr("win", "WIN") : tr("lose", "LOSE"),
            leftPerfect ? 0.28f : 0.35f,
            {100.0f, 46.0f},
            leftPerfect ? kGold : match.winnerSide == "A" ? kGreen : kRed,
            "black"
        );
        auto* rightResult = makeLabel(
            rightPerfect ? std::string("PERFECT WIN") : match.winnerSide == "B" ? tr("win", "WIN") : tr("lose", "LOSE"),
            rightPerfect ? 0.28f : 0.35f,
            {size.width - 100.0f, 46.0f},
            rightPerfect ? kGold : match.winnerSide == "B" ? kGreen : kRed,
            "black"
        );
        m_root->addChild(leftResult, 4);
        m_root->addChild(rightResult, 4);

        float y = summaryTop - 17.0f;
        for (auto const& round : match.rounds) {
            auto* row = makeLabel(fmt::format("ROUND {}   {} : {}", round.roundNumber, formatScore(round.scoreA), formatScore(round.scoreB)), 0.31f, {size.width / 2.0f, y}, corum::ranked::ui::Text, "semibold");
            m_root->addChild(row, 4);
            addClearChecks(round.clearsA, {size.width / 2.0f - 50.0f, y - 10.0f});
            addClearChecks(round.clearsB, {size.width / 2.0f + 50.0f, y - 10.0f}, true);
            y -= 27.0f;
        }
        for (auto const& deathmatch : match.deathmatches) {
            auto* row = makeLabel(fmt::format("{} {}   {} : {}", tr("death-match", "DEATH MATCH"), deathmatch.sequence, deathmatch.scoreA, deathmatch.scoreB), 0.29f, {size.width / 2.0f, y}, kRed, "semibold");
            m_root->addChild(row, 4);
            y -= 25.0f;
        }

        auto const eventA = ratingEventText(match.ratingEventA);
        auto const eventB = ratingEventText(match.ratingEventB);
        CCLabelBMFont* eventLabelA = nullptr;
        CCLabelBMFont* eventLabelB = nullptr;
        if (!eventA.first.empty()) {
            eventLabelA = makeLabel(eventA.first, 0.29f, {100.0f, 20.0f}, eventA.second, "black");
            m_root->addChild(eventLabelA, 8);
        }
        if (!eventB.first.empty()) {
            eventLabelB = makeLabel(eventB.first, 0.29f, {size.width - 100.0f, 20.0f}, eventB.second, "black");
            m_root->addChild(eventLabelB, 8);
        }

        if (!scoreInput) {
            animateRankedScoreChange(
                100.0f,
                cardY - 43.0f,
                match.rankedScoreDeltaA,
                cardA.scoreLabel,
                match.profileBeforeA
            );
            animateRankedScoreChange(
                size.width - 100.0f,
                cardY - 43.0f,
                match.rankedScoreDeltaB,
                cardB.scoreLabel,
                match.profileBeforeB
            );
            animatePromotionTrigger(
                100.0f,
                cardY - 43.0f,
                match.ratingEventA,
                match.profileBeforeA,
                match.rankedScoreDeltaA
            );
            animatePromotionTrigger(
                size.width - 100.0f,
                cardY - 43.0f,
                match.ratingEventB,
                match.profileBeforeB,
                match.rankedScoreDeltaB
            );
        }

        addButton(tr("queue-again", "QUEUE AGAIN"), {size.width / 2.0f, 24.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.52f);

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
        addScreenHeader(
            tr("achievements", "ACHIEVEMENTS"),
            tr("achievement-subtitle", "PROGRESS · AP · TITLES"),
            kGold,
            kCyan
        );
        auto* ap = makeTextPlate(
            fmt::format("{} AP", view.achievements.totalAp),
            {size.width*.19f, 27.0f},
            {size.width*.16f, size.height - 75.0f},
            kGold,
            0.38f,
            kGold,
            "black"
        );
        m_root->addChild(ap, 5);
        auto* pending = makeTextPlate(
            fmt::format("{} {}", view.achievements.unclaimedRewardCount, tr("claimable", "CLAIMABLE")),
            {size.width*.23f, 27.0f},
            {size.width*.40f, size.height - 75.0f},
            view.achievements.unclaimedRewardCount > 0 ? kRed : kCyan,
            0.34f,
            view.achievements.unclaimedRewardCount > 0 ? ccc3(255, 225, 230) : corum::ranked::ui::Text,
            "black"
        );
        m_root->addChild(pending, 5);
        if(!titleSelectorOpen){
            auto const pendingRewards=corum::ranked::achievementPending(view.achievements.cards,"ALL");
            auto const tier=view.achievements.loading?0:pendingRewards.tier;
            auto const width=std::min(210.f,size.width*.38f);
            auto* visual=makeNeonPanel({width,27},{},corum::ranked::ui::achievementColor(tier),kDeepPanel,255);
            auto* fill=CCLayerColor::create({255,255,255,255},width-3,24);fill->setPosition({1.5f,1.5f});visual->addChild(fill,2);
            corum::ranked::ui::styleAchievementFill(fill,tier);
            auto* caption=makeLabel(fmt::format("{} (+{} AP)",tr("claim-all","CLAIM ALL"),pendingRewards.amount),.29f,{width/2,13.5f},tier?ccc3(17,25,40):corum::ranked::ui::Text,"black");
            corum::ranked::ui::fitFullMenuLabel(caption,width-12,19,.29f);visual->addChild(caption,5);
            auto* button=CCMenuItemSpriteExtra::create(visual,this,menu_selector(CorumRankedLayer::onAchievementClaimAll));
            button->setID("ranked-achievement-claim-all"_spr);button->setPosition({size.width*.755f,size.height-75});
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
            for (int tab = 0; tab < 3; ++tab) {
                auto const scope = tab == 0 ? "SOLO_RANKED" : tab == 1 ? "SURVIVAL" : "SHARED";
                auto name = corum::ranked::achievementScopeName(scope,corum::ranked::i18n::isKorean());
                if (m_achievementScope == scope) name = "[ " + name + " ]";
                auto* button = addButton(name,{size.width/2 + (tab-1)*114.0f,size.height-100},menu_selector(CorumRankedLayer::onAchievementScope),false,0.45f);
                button->setTag(tab);
            }
        }
        constexpr std::size_t perPage = 4;
        auto const order = corum::ranked::achievementDisplayOrderForScope(view.achievements.cards,m_achievementScope);
        auto const pageCount = std::max<std::size_t>(
            1,
            (order.size() + perPage - 1) / perPage
        );
        if (order.empty() && view.achievements.loaded) {
            auto* empty = makeLabel(tr("no-achievements","NO ACHIEVEMENTS IN THIS SECTION"),0.28f,{size.width/2,size.height/2-25},kCyan,"semibold");
            corum::ranked::ui::fitMenuLabelWidth(empty,size.width-65,0.28f,0.18f); m_root->addChild(empty,5);
        }
        m_achievementPage = std::min(m_achievementPage, pageCount - 1);
        auto const start = m_achievementPage * perPage;
        auto const layout=corum::ranked::achievementScreenLayout(size.width,size.height);
        auto const cardWidth=layout.cardWidth,cardHeight=layout.cardHeight,cardScale=layout.cardScale;
        for (std::size_t slot = 0; slot < perPage && start + slot < order.size(); ++slot) {
            auto const index = order[start + slot];
            auto const& card = view.achievements.cards[index];
            auto const column = slot % 2;
            auto const row = slot / 2;
            auto const center = CCPoint{
                column == 0 ? size.width / 4.0f : size.width * 3.0f / 4.0f,
                layout.firstY - static_cast<float>(row) * layout.rowStep,
            };
            auto const accent = achievementCardAccent(card);
            auto* panel = makeNeonPanel({cardWidth, cardHeight}, center, accent, kDeepPanel, 246);
            m_root->addChild(panel, 1);
            auto const baseName = corum::ranked::achievementCardName(card,corum::ranked::i18n::isKorean());
            auto const title = card.type == "PROGRESSIVE" && card.currentLevel > 0
                ? fmt::format("{} - {}", baseName, card.displayLabel)
                : baseName;
            auto const titleColor = accent;
            auto* titleLabel = makeLabel(
                title,
                0.40f*cardScale,
                {center.x - cardWidth / 2.0f + 10.0f, center.y + 27.0f*cardScale},
                titleColor,
                "semibold",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitFullMenuLabel(titleLabel,cardWidth-82,20*cardScale,.40f*cardScale);
            corum::ranked::ui::styleAchievementText(titleLabel,corum::ranked::achievementCardTier(card,view.achievements.equippedTitle),card.unlocked || card.currentLevel>0);
            m_root->addChild(titleLabel, 4);
            auto const description = corum::ranked::achievementCardDescription(card,corum::ranked::i18n::isKorean());
            auto* descriptionLabel = makeLabel(
                description,
                0.25f*cardScale,
                {center.x - cardWidth / 2.0f + 10.0f, center.y + 5.0f*cardScale},
                accent,
                "medium",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitFullMenuLabel(descriptionLabel, cardWidth - 90.0f, 26.0f*cardScale, 0.25f*cardScale);
            descriptionLabel->setAlignment(kCCTextAlignmentLeft);
            m_root->addChild(descriptionLabel, 4);

            double progress = card.type == "PROGRESSIVE"
                ? std::clamp(card.gauge, 0.0, 1.0)
                : card.unlocked ? 1.0 : 0.0;
            auto const progressText = card.type == "PROGRESSIVE"
                ? card.maxed
                    ? fmt::format("{}", card.metricValue)
                    : fmt::format("{} / {}", card.metricValue, card.nextThreshold.value_or(0))
                : card.unlocked
                    ? fmt::format(
                        "{} · {} · {} AP",
                        tr("completed", "COMPLETED"),
                        upper(card.rarity),
                        card.rewardAp
                    )
                    : fmt::format("{} · {} AP", upper(card.rarity), card.rewardAp);
            auto* progressLabel = makeLabel(
                progressText,
                0.30f*cardScale,
                {center.x - cardWidth / 2.0f + 10.0f, center.y - 18.0f*cardScale},
                accent,
                "black",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitMenuLabelWidth(progressLabel, cardWidth - 92.0f, 0.30f*cardScale, 0.21f*cardScale);
            m_root->addChild(progressLabel, 4);
            auto* barBack = CCLayerColor::create({40, 50, 68, 220});
            barBack->setContentSize({cardWidth - 92.0f, 7.0f*cardScale});
            barBack->setPosition({center.x - cardWidth / 2.0f + 10.0f, center.y - 34.0f*cardScale});
            m_root->addChild(barBack, 3);
            auto* barFill = CCLayerColor::create({accent.r, accent.g, accent.b, 245});
            barFill->setContentSize({static_cast<float>((cardWidth - 92.0f) * progress), 7.0f*cardScale});
            barFill->setPosition(barBack->getPosition());
            m_root->addChild(barFill, 4);
            corum::ranked::ui::styleAchievementFill(barFill,corum::ranked::achievementCardTier(card,view.achievements.equippedTitle),true,card.type=="PROGRESSIVE"?card.currentLevel>=4:card.unlocked);

            if (!titleSelectorOpen) {
                auto const claimState = corum::ranked::achievementClaimState(
                    card.unlocked,
                    card.currentLevel,
                    card.claimableRewards
                );
                auto* claim = addButton(
                    claimState == corum::ranked::AchievementClaimState::Unclaimed
                        ? fmt::format("CLAIM +{}", card.claimableAp)
                        : claimState == corum::ranked::AchievementClaimState::Claimed
                            ? tr("claimed", "CLAIMED")
                            : tr("locked", "LOCKED"),
                    {center.x + cardWidth / 2.0f - 39.0f, center.y + 14.0f*cardScale},
                    menu_selector(CorumRankedLayer::onAchievementClaim),
                    claimState == corum::ranked::AchievementClaimState::Unclaimed,
                    0.50f*cardScale
                );
                claim->setTag(static_cast<int>(index));
                claim->setEnabled(
                    claimState == corum::ranked::AchievementClaimState::Unclaimed &&
                    !view.achievements.loading
                );

                auto const titleUnlocked = card.type == "PROGRESSIVE"
                    ? card.currentLevel > 0
                    : card.unlocked;
                auto const equipped = view.achievements.equippedTitle.active &&
                    view.achievements.equippedTitle.achievementId == card.id;
                auto* titleButton = addButton(
                    equipped ? (card.type=="PROGRESSIVE"?tr("current", "CURRENT"):tr("unequip-title", "UNEQUIP")) : tr("set-title", "TITLE"),
                    {center.x + cardWidth / 2.0f - 39.0f, center.y - 18.0f*cardScale},
                    menu_selector(CorumRankedLayer::onAchievementTitle),
                    false,
                    0.50f*cardScale
                );
                titleButton->setTag(static_cast<int>(index));
                titleButton->setEnabled(titleUnlocked && !view.achievements.loading);
                if (!titleUnlocked) corum::ranked::ui::grayButtonVisual(titleButton);
            }
        }

        if (pageCount > 1 && !titleSelectorOpen) {
            auto* previous = addButton(
                "<",
                {size.width / 2.0f - 66.0f, 20.0f},
                menu_selector(CorumRankedLayer::onAchievementPrevious),
                false,
                0.52f
            );
            previous->setEnabled(m_achievementPage > 0);
            auto* next = addButton(
                ">",
                {size.width / 2.0f + 66.0f, 20.0f},
                menu_selector(CorumRankedLayer::onAchievementNext),
                false,
                0.52f
            );
            next->setEnabled(m_achievementPage + 1 < pageCount);
            auto* page = makeLabel(
                fmt::format("{} / {}", m_achievementPage + 1, pageCount),
                0.32f,
                {size.width / 2.0f, 20.0f},
                corum::ranked::ui::Text,
                "black"
            );
            m_root->addChild(page, 5);
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
        auto const tint=accent.value_or(selected?kCyan:kBlue);
        auto* face=makeNeonPanel({width,26},{},tint,kDeepPanel2,245);
        auto const labelColor=selected?kCyan:corum::ranked::ui::Text;
        historyText(face,text,.25f,{width/2,13},width-10,labelColor);
        auto* menu=CCMenu::create();menu->setAnchorPoint(CCPointZero);menu->setContentSize({width,26});menu->setPosition({position.x-width/2,position.y-13});parent->addChild(menu,20);
        auto* button=CCMenuItemSpriteExtra::create(face,this,handler);button->setPosition({width/2,13});button->setTag(tag);menu->addChild(button);return button;
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
        if(item.inProgress)return kCyan;
        if(item.mode==corum::ranked::RankedMode::SoloRanked)return item.side==item.winnerSide?kGreen:kRed;
        return item.survivalRank==1?kGold:item.survivalRank>0&&item.survivalRank<=3?kCyan:item.record["reachedFinal"].asBool().unwrapOr(false)?kBlue:item.survivalRank>0?kRed:corum::ranked::ui::TextMuted;
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
    void renderHistoryTabs() {
        auto const size=CCDirector::sharedDirector()->getWinSize();auto const step=std::min(112.f,(size.width-48)/3);
        for(int tab=0;tab<3;++tab){
            auto scope=tab==0?corum::ranked::HistoryScope::Survival:tab==1?corum::ranked::HistoryScope::SoloRanked:corum::ranked::HistoryScope::All;
            historyButton(m_root,tab==0?tr("survival","SURVIVAL"):tab==1?tr("solo-ranked","SOLO RANKED"):tr("all-history","ALL"),
                {size.width/2+(tab-1)*step,size.height-80},step-6,menu_selector(CorumRankedLayer::onHistoryScope),tab,m_historyScope==scope);
        }
        auto const filterWidth=std::min(155.f,(size.width-98)/2);
        historyButton(m_root,m_historyOrder=="OLDEST"?tr("history-oldest","OLDEST FIRST"):tr("history-newest","NEWEST FIRST"),{size.width/2-filterWidth/2-3,size.height-111},filterWidth,menu_selector(CorumRankedLayer::onHistoryOrder));
        historyButton(m_root,historyFilterText(m_historyResult),{size.width/2+filterWidth/2+3,size.height-111},filterWidth,menu_selector(CorumRankedLayer::onHistoryFilter));
        historyButton(m_root,tr("refresh","REFRESH"),{size.width/2,22},90,menu_selector(CorumRankedLayer::onHistoryRefresh));
    }
    void renderHistoryList(RuntimeView const& view) {
        auto const size=CCDirector::sharedDirector()->getWinSize();addTopBack(false);
        addScreenHeader(tr("match-history","MATCH HISTORY"),tr("history-latest-30","UP TO 30 MATCHES"),kGold,kCyan);renderHistoryTabs();
        if(view.historyLoading){auto* spinner=LoadingSpinner::create(40);spinner->setPosition({size.width/2,size.height/2});m_root->addChild(spinner,3);return;}
        if(!view.historyError.empty()){
            historyText(m_root,view.historyError,.27f,{size.width/2,size.height/2},size.width-60,kRed);return;
        }
        auto const placements=m_historyScope==corum::ranked::HistoryScope::Survival||m_historyResult!="ALL"?0:std::min<std::size_t>(5,view.placement.stageResults.size());
        auto const matches=std::min<std::size_t>(30,view.history.size());
        if(!placements&&!matches){historyText(m_root,tr("history-empty","NO MATCHES FOR THIS FILTER"),.30f,{size.width/2,size.height/2-15},size.width-60,kCyan);return;}
        auto const viewport=CCSize{std::min(760.f,size.width-48),std::max(55.f,size.height-176)};
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
        auto* content=scroll->m_contentLayer;auto const height=std::max(viewport.height,static_cast<float>(matches*122+placements*68+8));content->setContentSize({viewport.width,height});
        float y=height-4;
        for(std::size_t i=0;i<matches;++i,y-=122){
            auto const& item=view.history[i];bool const survival=item.mode==corum::ranked::RankedMode::Survival;
            auto const width=viewport.width-4;auto const color=historyColor(item);
            auto* face=makeNeonPanel({width,112},{},color,kDeepPanel2,250);
            historyText(face,historyOutcome(item),.43f,{15,91},width*.60f,color,true,{0,.5f});
            historyText(face,historyDelta(item),.39f,{width-15,91},width*.30f,!item.ownRankedScoreDelta?corum::ranked::ui::TextMuted:*item.ownRankedScoreDelta<0?kRed:kGreen,true,{1,.5f});
            std::string summary;
            if(survival){
                auto const total=item.record["participantCount"].asInt().unwrapOr(0);
                auto const rank=item.survivalRank>0?(corum::ranked::i18n::isKorean()?fmt::format(fmt::runtime(tr("player-rank","{}")),item.survivalRank):corum::ranked::survivalOrdinal(item.survivalRank)):"-";
                summary=item.inProgress?tr("history-final-rank-pending","FINAL RANK PENDING"):item.survivalRank>0?fmt::format("{} / {}  {}",rank,total,tr("history-players","PLAYERS")):tr("history-unsettled","RESULT PENDING");
            }else{
                auto const own=item.side=="B"?item.roundWinsB:item.roundWinsA,rival=item.side=="B"?item.roundWinsA:item.roundWinsB;
                summary=fmt::format("{}  {} : {}",tr("you","YOU"),own,rival);
            }
            historyText(face,summary,.39f,{15,64},survival?width-30:width*.46f,corum::ranked::ui::Text,true,{0,.5f});
            if(!survival){
                auto const other=item.side=="B"?"A":"B";auto const info=item.record["players"][other];
                corum::ranked::SurvivalStanding player;player.id=other;player.name=item.opponentName;player.bot=info["isBot"].asBool().unwrapOr(item.debug);
                player.accountId=player.bot?0:numFromString<int>(info["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
                player.tier=item.record["profileBefore"][other]["displayedTier"].asString().unwrapOr("");
                historyPlayerName(face,player,{width*.74f,64},width*.44f,kGold,item.matchId,false);
            }
            auto meta=(item.finishedAt.empty()?item.createdAt:item.finishedAt).substr(0,10);
            meta+="  ·  "+(survival?tr("survival","SURVIVAL"):tr("solo-ranked","SOLO RANKED"));
            if(!survival)if(auto bo=item.record["bestOf"].asInt();bo.isOk())meta+=fmt::format("  BO{}",bo.unwrap());
            if(item.debug)meta+="  DEBUG";
            historyText(face,meta,.26f,{15,39},width-30,corum::ranked::ui::TextMuted,false,{0,.5f});
            if(survival){
                auto const rounds=corum::ranked::historyRounds(item.record,true);
                auto const count=std::min<std::size_t>(rounds.size(),static_cast<std::size_t>(std::max(1.f,(width-132)/31.f)));
                for(std::size_t r=0;r<count;++r){auto const& round=rounds[r];auto const tint=historyRoundColor(round);
                    auto* chip=makeNeonPanel({28,17},{29.f+r*31,15},tint,kDeepPanel,245);face->addChild(chip,3);
                    historyText(chip,round.label,.23f,{14,8.5f},25,corum::ranked::ui::Text,true);
                }
            }
            historyText(face,tr("history-details","DETAILS >"),.27f,{width-15,15},100,kCyan,true,{1,.5f});
            // The ScrollLayer owns the entire gesture, including a drag begun
            // on a card. Only an unmoved, still-current tap selects its match.
            face->setAnchorPoint(CCPointZero);face->setPosition({2,y-112});face->setTag(static_cast<int>(i));content->addChild(face,3);
        }
        for(std::size_t i=0;i<placements;++i,y-=68){
            auto const& stage=view.placement.stageResults[i];auto const first=stage.mapGrades.empty()?"?":stage.mapGrades[0],second=stage.mapGrades.size()<2?"?":stage.mapGrades[1];
            auto* panel=makeNeonPanel({viewport.width-4,60},{viewport.width/2,y-30},kGold,kDeepPanel2,240);content->addChild(panel,2);panel->setTag(-1);
            historyText(panel,fmt::format("{}  {}  ·  {}",tr("placement","PLACEMENT"),stage.stageIndex,stage.stageGrade),.30f,{viewport.width/2,42},viewport.width-24,kGold);
            historyText(panel,fmt::format("MAP 1 {} + MAP 2 {}   {:.1f}/500",first,second,stage.stageScore),.25f,{viewport.width/2,19},viewport.width-24);
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
        auto* track=CCLayerColor::create({35,47,64,255});track->setContentSize({width,5});track->setPosition({center.x-width/2,center.y-22});parent->addChild(track,2);
        auto* fill=CCLayerColor::create({color.r,color.g,color.b,255});fill->setContentSize({width*static_cast<float>(std::clamp(progress.value_or(0.)/100.,0.,1.)),5});track->addChild(fill);
        historyText(parent,!progress?tr("history-no-attempts","NO RECORDED ATTEMPTS"):cleared?tr("history-cleared","CLEARED"):tr("history-not-cleared","NOT CLEARED"),.22f,{center.x,center.y-36},width,cleared?kGreen:corum::ranked::ui::TextMuted);
    }
    bool historyHasThumbnail(corum::ranked::HistoryRound const& round)const {
        return round.levelId>0 && corum::ranked::SurvivalThumbnailProvider::supported();
    }
    void historyMapPanel(CCNode* content,corum::ranked::HistoryRound const& round,CCPoint position,float width,float height) {
        using namespace corum::ranked;
        auto* panel=makeNeonPanel({width,height},position,kBlue,kDeepPanel2,250);content->addChild(panel,2);
        panel->setID("ranked-history-level-panel"_spr);
        historyText(panel,round.title.empty()?tr("history-level-name-unavailable","LEVEL NAME UNAVAILABLE"):round.title,.43f,{width/2,height-21},width-24,ui::Text,true);
        historyText(panel,round.creator.empty()?tr("history-unavailable","UNAVAILABLE"):"by "+round.creator,.27f,{width/2,height-45},width-24,ui::TextMuted);
        auto const thumbnail=historyHasThumbnail(round),horizontal=thumbnail&&width>=300;
        if(thumbnail){
            auto const thumbWidth=horizontal?150.f:std::min(width-24,150.f),thumbHeight=thumbWidth*9/16;
            auto* frame=makeNeonPanel({thumbWidth,thumbHeight},{horizontal?87.f:width/2,height-(horizontal?111.f:98.f)},kBlue,kDeepPanel,240);
            frame->setID("ranked-history-thumbnail"_spr);panel->addChild(frame,3);
            auto* placeholder=historyText(frame,tr("loading","Loading..."),.24f,{thumbWidth/2,thumbHeight/2},thumbWidth-12,ui::TextMuted);m_historyThumbnailPlaceholder=placeholder;
            auto* sprite=CCSprite::create();sprite->setPosition({thumbWidth/2,thumbHeight/2});frame->addChild(sprite,5);m_historyThumbnail=sprite;
            m_historyThumbnailSize=CCSize{thumbWidth-4,thumbHeight-4};m_historyTexture=nullptr;
            m_historyThumbnailContext={m_historySelectedId,round.key,0,static_cast<std::uint64_t>(round.levelId),++m_historyThumbnailGeneration};
        }
        float const infoLeft=horizontal?174.f:12.f,infoWidth=width-infoLeft-12;
        float const infoTop=thumbnail&&!horizontal?height-156:height-62;
        MapMetadata metadata;metadata.kind=round.difficulty.find("DEMON")!=std::string::npos?"DEMON":round.difficulty.empty()?"UNKNOWN":"NORMAL";
        if(round.stars>0)metadata.stars=round.stars;
        metadata.featureScore=round.featureScore;metadata.epic=round.epic;
        auto* difficulty=ui::mapDifficultyBadge(round.levelId,metadata,60,25,false,recordedMapDifficulty(round.difficulty));difficulty->setPosition({infoLeft,infoTop-26});panel->addChild(difficulty,5);
        historyText(panel,round.difficulty.empty()?tr("history-unavailable","UNAVAILABLE"):round.difficulty,.30f,{infoLeft+64,infoTop-13},std::max(12.f,infoWidth-64),ui::Text,true,{0,.5f});
        historyText(panel,round.levelId>0?fmt::format("GD #{}",round.levelId):tr("history-level-unavailable","Map information could not be found."),.28f,{infoLeft+infoWidth/2,infoTop-41},infoWidth,kCyan,true);
        historyText(panel,(round.length.empty()?tr("history-unavailable","UNAVAILABLE"):round.length)+(round.stars>0?fmt::format("  ·  {}*",round.stars):""),.27f,{infoLeft+infoWidth/2,infoTop-63},infoWidth,ui::Text);
        historyText(panel,round.grade.empty()?round.label:round.grade,.25f,{infoLeft+infoWidth/2,infoTop-83},infoWidth,round.grade.empty()?ui::TextMuted:kGold,true);
        auto* button=historyButton(panel,tr("history-view-level","VIEW LEVEL"),{width/2,21},std::min(width-24,170.f),menu_selector(CorumRankedLayer::onHistoryLevel),round.levelId);
        if(round.levelId<=0)ui::grayButtonVisual(button);
    }
    void historyOwnPanel(CCNode* parent,corum::ranked::HistoryRound const& round,CCPoint position,float width,float height) {
        auto* panel=makeNeonPanel({width,height},position,historyRoundColor(round),kDeepPanel2,240);parent->addChild(panel,2);
        historyText(panel,tr("history-your-record","YOUR RECORD"),.30f,{width/2,height-18},width-14,kCyan);
        auto const own=std::find_if(round.standings.begin(),round.standings.end(),[](auto const& p){return p.own;});
        if(own==round.standings.end()){historyText(panel,tr("history-not-participant","DID NOT PLAY THIS ROUND"),.23f,{width/2,height/2},width-14,corum::ranked::ui::TextMuted);return;}
        auto const rank=own->rank>0?fmt::format("{}#{}",own->tiedPlayers>1?"= ":"",own->rank):"-";
        historyText(panel,rank,.48f,{width/2,height-46},width-14,kCyan);
        historyProgress(panel,own->progress,own->cleared,round.deathmatch,{width/2,height-81},width-30,own->cleared?kGold:kCyan);
        historyText(panel,fmt::format("{}  {}",own->attempts,tr("history-attempts","ATTEMPTS")),.24f,{width/2,69},width-14);
        historyText(panel,own->qualified?tr("history-qualified","QUALIFIED"):tr("history-eliminated","ELIMINATED"),.25f,{width/2,48},width-14,own->qualified?kGreen:kRed);
        historyText(panel,round.cutoff?fmt::format("{} {}%",tr("history-cutoff-gap","CUTOFF GAP"),corum::ranked::rankedScoreText((round.deathmatch?own->progress:std::trunc(own->progress))-*round.cutoff,round.deathmatch,true)):"",.22f,{width/2,28},width-14,corum::ranked::ui::TextMuted);
    }
    void renderHistoryDetail(RuntimeView const& view) {
        auto const size=CCDirector::sharedDirector()->getWinSize();addTopBack(false);
        if(m_historyIndex>=view.history.size()){m_page=Page::HistoryList;renderHistoryList(view);return;}
        auto const& item=view.history[m_historyIndex];auto const survival=item.mode==corum::ranked::RankedMode::Survival;
        addScreenHeader(survival?tr("history-survival-title","SURVIVAL MATCH HISTORY"):tr("history-solo-title","SOLO RANKED / MATCH HISTORY"),historyOutcome(item)+"  "+historyDelta(item),historyColor(item),kCyan);
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
        for(int i=m_historyTabPage*tabs;i<std::min(static_cast<int>(rounds.size()),(m_historyTabPage+1)*tabs);++i)
            historyButton(m_root,rounds[i].label,{(size.width-layout.width)/2+50+(i%tabs)*54.f,size.height-84},49,menu_selector(CorumRankedLayer::onHistoryRound),i,rounds[i].key==m_historyRound,survival?std::optional<ccColor3B>{historyRoundColor(rounds[i])}:std::nullopt);
        if(pages>1){historyButton(m_root,"<",{(size.width-layout.width)/2+12,size.height-84},24,menu_selector(CorumRankedLayer::onHistoryRoundPage),-1);historyButton(m_root,">",{(size.width+layout.width)/2-12,size.height-84},24,menu_selector(CorumRankedLayer::onHistoryRoundPage),1);}
        auto const viewport=CCSize{layout.width,layout.height};auto* scroll=corum::ranked::ui::createRankedTapScroll(viewport,[weak=WeakRef(this)](CCPoint start,CCPoint end){if(auto owner=weak.lock())owner->onHistoryPlayerTap(start,end);});m_historyBoardScroll=scroll;scroll->setID("ranked-history-board-scroll"_spr);scroll->setPosition({(size.width-layout.width)/2,layout.bottom});m_root->addChild(scroll,3);
        auto* content=scroll->m_contentLayer;
        if(!round.completed){
            auto const mapWidth=std::min(layout.width,390.f),mapHeight=corum::ranked::historyMapPanelHeight(mapWidth,historyHasThumbnail(round));
            auto const h=std::max(viewport.height,mapHeight+75);content->setContentSize({layout.width,h});
            historyText(content,item.inProgress?tr("history-round-live","This round is still in progress."):tr("history-round-unfinished","This round has no confirmed result."),.32f,{layout.width/2,h-25},layout.width-20,kCyan);
            historyText(content,item.inProgress?tr("history-round-live-help","Final records and rankings appear after the round ends."):tr("history-unavailable","UNAVAILABLE"),.25f,{layout.width/2,h-51},layout.width-20,corum::ranked::ui::TextMuted);
            historyMapPanel(content,round,{layout.width/2,h-75-mapHeight/2},mapWidth,mapHeight);scroll->scrollToTop();return;
        }
        if(!survival){
            auto const mapWidth=layout.split?layout.mapWidth:std::min(layout.width,390.f);auto const boardWidth=layout.split?layout.width-mapWidth-10:layout.width;
            auto const mapHeight=corum::ranked::historyMapPanelHeight(mapWidth,historyHasThumbnail(round));
            auto const h=std::max(viewport.height,layout.split?std::max(mapHeight,205.f):mapHeight+205);content->setContentSize({layout.width,h});
            historyMapPanel(content,round,{layout.split?mapWidth/2:layout.width/2,h-mapHeight/2},mapWidth,mapHeight);
            auto const boardX=layout.split?mapWidth+10:0.f,boardTop=layout.split?h:h-mapHeight-12;
            auto* panel=makeNeonPanel({boardWidth,190},{boardX+boardWidth/2,boardTop-95},historyColor(item),kDeepPanel2,240);content->addChild(panel,2);
            auto const ownB=item.side=="B";auto const ownScore=ownB?item.roundWinsB:item.roundWinsA,otherScore=ownB?item.roundWinsA:item.roundWinsB;
            historyText(panel,fmt::format("{} : {}",ownScore,otherScore),.42f,{boardWidth/2,169},boardWidth*.22f,kCyan,true);
            for(auto const* side:{"A","B"}){
                auto const person=item.record["players"][side];corum::ranked::SurvivalStanding player;
                player.id=side;player.name=person["name"].asString().unwrapOr("");player.bot=person["isBot"].asBool().unwrapOr(false);
                player.accountId=player.bot?0:numFromString<int>(person["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);
                player.tier=item.record["profileBefore"][side]["displayedTier"].asString().unwrapOr("");
                auto const own=std::string(side)==item.side;if(player.name.empty())player.name=own?tr("you","YOU"):item.opponentName;
                historyPlayerName(panel,player,{boardWidth*(own?.22f:.78f),171},boardWidth*.31f,own?kCyan:kGold);
            }
            auto const otherSide=ownB?"A":"B";auto const ownTier=item.record["profileBefore"][item.side]["displayedTier"].asString().unwrapOr(""),otherTier=item.record["profileBefore"][otherSide]["displayedTier"].asString().unwrapOr("");
            historyText(panel,ownTier,.22f,{boardWidth*.25f,137},boardWidth*.44f,kGold);historyText(panel,otherTier,.22f,{boardWidth*.75f,137},boardWidth*.44f,kGold);
            historyProgress(panel,ownB?round.bestB:round.bestA,ownB?round.clearedB:round.clearedA,round.deathmatch,{boardWidth*.25f,111},boardWidth*.4f,kCyan);
            historyProgress(panel,ownB?round.bestA:round.bestB,ownB?round.clearedA:round.clearedB,round.deathmatch,{boardWidth*.75f,111},boardWidth*.4f,kGold);
            auto const outcome=round.deathmatch?round.winnerSide:round.result;
            auto const tied=outcome.empty()||outcome=="DRAW";
            historyText(panel,tied?tr("tie","TIE"):outcome==item.side?tr("history-your-round-win","YOU WIN THIS ROUND"):tr("history-rival-round-win","RIVAL WINS THIS ROUND"),.28f,{boardWidth/2,45},boardWidth-14,tied?kGold:outcome==item.side?kGreen:kRed);
            if(round.nextDeathmatch>0)historyText(panel,fmt::format("{} DM {}",tr("history-continued","CONTINUED IN"),round.nextDeathmatch),.24f,{boardWidth/2,20},boardWidth-16,corum::ranked::ui::TextMuted);
        }else{
            auto const mapWidth=layout.split?layout.mapWidth:std::min(layout.width,390.f),mapHeight=corum::ranked::historyMapPanelHeight(mapWidth,historyHasThumbnail(round));
            auto const boardHeight=58.f+round.standings.size()*70;auto const h=std::max(viewport.height,layout.split?std::max({220.f,mapHeight,boardHeight}):mapHeight+250.f+boardHeight);content->setContentSize({layout.width,h});
            historyMapPanel(content,round,{layout.split?mapWidth/2:layout.width/2,h-mapHeight/2},mapWidth,mapHeight);
            auto const boardX=layout.split?layout.mapWidth+8:0.f,boardTop=layout.split?h:h-mapHeight-244;
            auto const width=layout.boardWidth;
            auto* board=makeNeonPanel({width,boardHeight},{boardX+width/2,boardTop-boardHeight/2},kBlue,kDeepPanel2,240);content->addChild(board,2);
            auto const cutoff=round.cutoff?corum::ranked::rankedScoreText(*round.cutoff,round.deathmatch)+"%":"-";
            historyText(board,fmt::format("{} {} · {} {}/{}",tr("history-cutoff","CUTOFF"),cutoff,tr("history-qualified","QUALIFIED"),round.qualifiedCount,round.target),.23f,{width/2,boardHeight-16},width-14,kCyan);
            float y=boardHeight-62;
            auto const columns=corum::ranked::historyRosterColumns(width);
            for(auto const& p:round.standings){
                auto const color=p.own?kCyan:p.cleared?kGold:p.qualified?kGreen:kRed;
                if(p.own){auto* emphasis=makeNeonPanel({width-8,60},{width/2,y-4},kCyan,kDeepPanel,240);board->addChild(emphasis,2);}
                historyText(board,p.rank>0?fmt::format("{}{}",p.tiedPlayers>1?"=":"",p.rank):"-",.27f,{columns.rank,y},30,color);
                auto const suffix=p.forfeited?tr("history-forfeit","FORFEIT"):p.qualified?tr("history-qualified","QUALIFIED"):tr("history-eliminated","ELIMINATED");
                corum::ranked::SurvivalStanding player;player.id=p.id;player.name=p.name;player.rank=p.rank;player.bot=p.bot;player.accountId=p.accountId;player.tier=p.tier;
                auto* iconHolder=CCNode::create();iconHolder->setPosition({columns.icon,y});board->addChild(iconHolder,5);m_historyIcons.push_back({WeakRef<CCNode>(iconHolder),player});
                historyPlayerName(board,player,{columns.name+columns.nameWidth/2,y+7},columns.nameWidth,color);
                historyText(board,fmt::format("{} {} · {}",p.attempts,tr("history-attempts","ATTEMPTS"),suffix),.21f,{columns.name,y-23},width-columns.name-12,color,false,{0,.5f});
                historyText(board,corum::ranked::rankedScoreText(p.progress,round.deathmatch)+"%",.34f,{columns.best,y+5},columns.bestWidth,color,true);
                if(p.cutoffBoundary){auto* line=CCLayerColor::create({kGold.r,kGold.g,kGold.b,220});line->setContentSize({width-16,1});line->setPosition({8,y-35});board->addChild(line,3);}
                y-=70;
            }
            historyOwnPanel(content,round,{layout.split?layout.width-layout.ownWidth/2:layout.width/2,layout.split?h-110:h-mapHeight-122},layout.split?layout.ownWidth:std::min(layout.width,390.f),220);
        }
        scroll->scrollToTop();
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
            clearSyncPending || mapReady ? kGreen : (failed ? kRed : kCyan), "ranked-map-download-status"_spr);
        addStatus("02  " + audioText, audioPosition,
            audioReady ? kGreen : (failed && mapReady ? kRed : kCyan), "ranked-audio-download-status"_spr);
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
        closeJukeboxNotice();
        if (!corum::ranked::RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene())) return;
        m_enteringLevel = true;
        m_rankedChildScenePushed = true;
    }

    void onLowDetail(CCObject*) {
        auto* level = findPlayableMap();
        if (!level || m_enteringLevel) return;
        corum::ranked::RankedRoundPreparation::get().setLowDetail(!level->m_lowDetailModeToggled);
        // The exact native Saved object persists user options; never overwrite
        // the map author's m_lowDetailMode capability or any score/metadata.
        render();
    }

    void onDisableShake(CCObject*) {
        auto* level = findPlayableMap();
        if (!level || m_enteringLevel) return;
        level->m_disableShakeToggled = !level->m_disableShakeToggled;
        render();
    }

    void addRoundOptions(float y) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* level = findPlayableMap();
        auto* ldm = addButton(fmt::format("{}: {}", tr("low-detail", "LDM"), level && level->m_lowDetailModeToggled ? "ON" : "OFF"),
            {size.width / 2.0f - 103.0f, y}, menu_selector(CorumRankedLayer::onLowDetail), false, 0.35f);
        auto* shake = addButton(fmt::format("{}: {}", tr("disable-shake", "DISABLE SHAKE"), level && level->m_disableShakeToggled ? "ON" : "OFF"),
            {size.width / 2.0f, y}, menu_selector(CorumRankedLayer::onDisableShake), false, 0.35f);
        ldm->setID("ranked-round-ldm"_spr);
        shake->setID("ranked-round-disable-shake"_spr);
        ldm->setEnabled(level != nullptr);
        shake->setEnabled(level != nullptr);
        addJukeboxButton({size.width / 2.0f + 116.0f, y}, 0.35f);
    }

    void onDownloadAllResources(CCObject*) {
        m_localMessage.clear();
        RankedAudioManager::get().downloadAll();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        if (assets.view().state == corum::ranked::ui::RankAssetPackState::Failed) assets.retry();
        else assets.prepare();
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

    void onBan(CCObject* sender) {
        if (!m_selectedBan.empty() || !m_pendingBan.empty()) return;
        auto const index = sender ? sender->getTag() : -1;
        auto const& maps = RankedRuntime::get().view().match.candidateMaps;
        if (index < 0 || static_cast<std::size_t>(index) >= maps.size()) return;
        m_pendingBan = maps[static_cast<std::size_t>(index)].canonicalLevelId;
        RankedRuntime::get().submitBan(m_pendingBan);
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

    void onAchievements(CCObject*) {
        if (m_modeTransition.moving()) return;
        auto const selected = m_modeTransition.selected();
        m_achievementScope = !selected ? "SHARED" : *selected == corum::ranked::RankedMode::Survival ? "SURVIVAL" : "SOLO_RANKED";
        m_page = Page::Achievements;
        m_achievementPage = 0;
        m_titleSelectorAchievement.clear();
        RankedRuntime::get().fetchAchievements();
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
        RankedRuntime::get().fetchHistoryDetail(rows[m_historyIndex].mode,m_historySelectedId,true);
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
        if(m_page==Page::HistoryDetail&&m_historyIndex<rows.size())RankedRuntime::get().fetchHistoryDetail(rows[m_historyIndex].mode,rows[m_historyIndex].matchId,true);
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

    void onTierGuide(CCObject*) {
        if(m_modeTransition.moving() || m_modeTransition.selected()!=corum::ranked::RankedMode::SoloRanked)return;
        if(auto* popup=corum::ranked::ui::RankedTierGuidePopup::create())popup->show();
    }

    void onCloseLayer(CCObject*) {
        if (m_modeNoticeOpen) { closeModeNotice(); syncLobbyInput(); return; }
        if (m_modeTransition.moving() || m_uiFadingOut) return;
        auto& runtime = RankedRuntime::get();
        if (m_page == Page::Live && m_modeTransition.selected() && m_lobbyPanel && runtime.view().stage != RuntimeStage::Matched) {
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
