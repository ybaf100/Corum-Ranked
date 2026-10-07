#include "RankedPopup.hpp"

#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "DebugBotPopup.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#endif
#include "RankedSongGate.hpp"
#include "RankedLevelPreparation.hpp"
#include "RankedRoundPreparation.hpp"
#include "domain/PlacementStageMerge.hpp"
#include "domain/AchievementPresentation.hpp"
#include "domain/TransitionTrace.hpp"
#include "ui/RankBadgeAssets.hpp"
#include "ui/RankTransitionLayer.hpp"
#include "ui/PlacementGradeBadge.hpp"
#include "ui/PlayerIcon.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"
#include "ui/RankedInput.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
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

std::string formatScore(double value) {
    if (std::abs(value - std::round(value)) < 0.0005) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
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

std::string scoreComparison(double ownScore, double opponentScore) {
    if (!corum::ranked::i18n::isKorean()) {
        return fmt::format(
            "YOUR SCORE  {}     OPPONENT  {}",
            formatScore(ownScore),
            formatScore(opponentScore)
        );
    }
    return fmt::format(
        "내 {}  {}     {}  {}",
        tr("score", "SCORE"),
        formatScore(ownScore),
        tr("opponent", "OPPONENT"),
        formatScore(opponentScore)
    );
}

std::string twoLineTitle(std::string value, std::size_t perLine = 11) {
    if (value.size() <= perLine) return value;
    auto splitAt = value.rfind(' ', perLine);
    if (splitAt == std::string::npos || splitAt < perLine / 2) splitAt = perLine;
    auto first = value.substr(0, splitAt);
    auto secondStart = splitAt;
    while (secondStart < value.size() && value[secondStart] == ' ') ++secondStart;
    auto second = value.substr(secondStart);
    auto const maxSecond = perLine + 2;
    if (second.size() > maxSecond) {
        second.resize(maxSecond > 3 ? maxSecond - 3 : maxSecond);
        second += "...";
    }
    return first + "\n" + second;
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
    if (event.promotionSucceeded && oldTier != "UNRANKED" && newTier != "UNRANKED") {
        return RankTransitionType::Promotion;
    }
    if (event.demoted && oldTier != "UNRANKED" && oldTier != "RED" && newTier != "UNRANKED") {
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
    if (rarity == "LEGENDARY") return ccc3(255, 176, 70);
    if (rarity == "EPIC") return ccc3(198, 121, 255);
    if (rarity == "RARE") return ccc3(87, 187, 255);
    return ccc3(178, 186, 201);
}

std::string achievementTitleText(corum::ranked::AchievementTitleView const& title) {
    if (!title.active) return {};
    auto const name = corum::ranked::i18n::isKorean() ? title.nameKo : title.nameEn;
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
    auto* result = corum::ranked::ui::label(text, scale, position, color, weight);
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
    corum::ranked::ui::fitLabelWidth(label, size.width - 12.0f, scale, std::max(0.12f, scale * 0.65f));
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

    auto* caption = corum::ranked::ui::label(
        text,
        0.62f,
        {width / 2.0f, height / 2.0f},
        selected ? kGold : (danger ? ccc3(255, 224, 229) : corum::ranked::ui::Text),
        true
    );
    corum::ranked::ui::fitLabelWidth(caption, width - 20.0f, 0.62f, 0.30f);
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
    Page m_page = Page::Live;
    std::size_t m_historyIndex = 0;
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
        m_concessionNoticeText = corum::ranked::ui::label("", 0.30f,
            {m_concessionNotice->getContentSize().width / 2, 13}, kGold, corum::ranked::ui::FontWeight::ExtraBold);
        m_concessionNotice->addChild(m_concessionNoticeText); m_concessionNotice->setVisible(false);
        addChild(m_concessionNotice, 2500);

        RankedRuntime::get().begin();
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
        setKeypadEnabled(!m_rankTransitionLayer);
        if (m_menu) {
            corum::ranked::ui::cancelMenuTouch(m_menu);
            m_menu->setTouchEnabled(!m_rankTransitionLayer);
        }
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
        runtime.tick();
        auto& audio = RankedAudioManager::get();
        audio.configure(runtime.view().client);
        audio.tick();
        syncResources();
        syncPhase();
        updateAutomation();

        auto const uiKey = presentationKey();
        if (prepareUiTransition(uiKey)) {
            render();
            applyFadeInIfNeeded();
        }
        updateDynamicUi();
        syncAudioMode();
        maybeEnterLevel();
    }

    bool showResourceGate() const {
        auto const& audioConfig = RankedRuntime::get().view().client.audio;
        auto const audioEnabled = audioConfig.enabled && !audioConfig.resources.empty();
        auto const& audio = RankedAudioManager::get();
        auto const assetState = corum::ranked::ui::RankAssetPackManager::get().view().state;
        auto const assetsDownloading = assetState == corum::ranked::ui::RankAssetPackState::Downloading;
        if (!audioEnabled) return assetsDownloading;
        return audio.requiresResourceDownload() || assetsDownloading || !m_resourceGateAcknowledged;
    }

    void syncResources() {
        auto& audio = RankedAudioManager::get();
        auto& assets = corum::ranked::ui::RankAssetPackManager::get();
        if (assets.view().state == corum::ranked::ui::RankAssetPackState::Disabled) {
            assets.prepare();
        }
        auto const assetsDownloading =
            assets.view().state == corum::ranked::ui::RankAssetPackState::Downloading;
        auto const& audioConfig = RankedRuntime::get().view().client.audio;
        if (!audioConfig.enabled || audioConfig.resources.empty()) {
            if (assetsDownloading) {
                m_resourceGateInitialized = true;
                m_resourceGateAcknowledged = false;
                m_resourceGateWasMissing = true;
                return;
            }
            m_resourceGateInitialized = true;
            m_resourceGateAcknowledged = true;
            m_resourceGateWasMissing = false;
            m_resourceGateMenuRestored = false;
            return;
        }

        auto const missing = audio.requiresResourceDownload() || assetsDownloading;
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

    std::string preparationPresentationKey() const {
        auto const& p = corum::ranked::RankedRoundPreparation::get().view();
        auto* level = findPlayableMap();
        return fmt::format(":prepare:{}:{}:{}:{}:{}:{}:{}:{}:{}", p.mapReady, p.audioReady,
            p.songsDone, p.songsTotal, p.sfxDone, p.sfxTotal, p.error,
            level && level->m_lowDetailModeToggled, level && level->m_disableShakeToggled);
    }

    std::string presentationKey() const {
        auto const& view = RankedRuntime::get().view();
        if (view.environmentWarning.active) {
            return fmt::format("environment-warning:{}", view.revision);
        }
        if (showResourceGate()) {
            auto const resources = RankedAudioManager::get().downloadView();
            auto const assets = corum::ranked::ui::RankAssetPackManager::get().view();
            return fmt::format(
                "resources:{}:{}:{}",
                static_cast<int>(resources.state),
                static_cast<int>(assets.state),
                assets.activeVersion
            );
        }
        if (m_page == Page::HistoryList) {
            return fmt::format(
                "history:list:{}:{}:{}",
                view.historyLoading,
                view.historyError,
                view.history.size() + view.placement.stageResults.size()
            );
        }
        if (m_page == Page::HistoryDetail) return fmt::format("history:detail:{}", m_historyIndex);
        if (m_page == Page::Achievements) {
            return fmt::format(
                "achievements:{}:{}:{}:{}:{}",
                view.achievements.loaded,
                view.achievements.loading,
                view.achievements.unclaimedRewardCount,
                m_achievementPage,
                m_titleSelectorAchievement
            ) + fmt::format(":{}", view.revision);
        }
        if (m_page == Page::PlacementPoolSelection) {
            return fmt::format("placement:pool:{}:{}:{}:{}", RankedRuntime::get().view().revision, m_selectedPlacementStageCount, m_selectedPlacementPool, m_localMessage);
        }
        if (m_page == Page::PlacementSkipConfirm) return "placement:skip-confirm";
        if (view.stage != RuntimeStage::Matched) {
            return fmt::format(
                "stage:{}:placement:{}:{}",
                static_cast<int>(view.stage),
                view.placement.required,
                view.placement.active
            );
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
            ) + preparationPresentationKey();
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
        return key + preparationPresentationKey();
    }

    double uiFadeInSeconds() const {
        return std::clamp(RankedRuntime::get().view().client.ui.fadeInSeconds, 0.0, 3.0);
    }

    double uiFadeOutSeconds() const {
        return std::clamp(RankedRuntime::get().view().client.ui.fadeOutSeconds, 0.0, 3.0);
    }

    bool prepareUiTransition(std::string const& key) {
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
        if (showResourceGate()) return;

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
        m_resourceCountLabel = nullptr;
        m_resourceProgressFill = nullptr;
        m_resourceProgressLabel = nullptr;
        m_resourceProgressWidth = 0.0f;
        m_mainStatusLabel = nullptr;
        m_matchFoundCountdownLabel = nullptr;
        m_banTimerLabel = nullptr;
        m_prepareFooterLabel = nullptr;
        m_prepareLoadStageLabel = nullptr;
        m_prepareLoadPercentLabel = nullptr;
        m_prepareLoadFill = nullptr;
        m_prepareLoadWidth = 0.0f;
        m_spectatorStateLabel = nullptr;
        m_spectatorProgressLabel = nullptr;
        m_spectatorScoreLabel = nullptr;
        m_spectatorHintLabel = nullptr;
    }

    void clearUi() {
        resetDynamicUiPointers();
        corum::ranked::ui::cancelMenuTouch(m_menu);
        m_root->removeAllChildrenWithCleanup(true);
        m_menu->removeAllChildrenWithCleanup(true);
    }

    void updateDynamicUi() {
        auto const& runtime = RankedRuntime::get();
        auto const& noticeMatch = runtime.view().match;
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
                corum::ranked::ui::fitLabelWidth(m_concessionNoticeText,
                    m_concessionNotice->getContentSize().width - 16, 0.30f, 0.20f);
            }
        }
        if (RankedRuntime::get().view().environmentWarning.active) return;
        if (showResourceGate()) {
            auto const resourceView = RankedAudioManager::get().downloadView();
            if (m_resourceCountLabel) {
                m_resourceCountLabel->setString(
                    fmt::format(
                        "{} / {} {}",
                        resourceView.ready,
                        resourceView.total,
                        tr("ready", "READY")
                    ).c_str()
                );
                m_resourceCountLabel->setColor(
                    resourceView.ready == resourceView.total ? kGreen : ccc3(240, 244, 250)
                );
            }
            double progress = resourceView.total > 0
                ? static_cast<double>(resourceView.ready) / static_cast<double>(resourceView.total)
                : 1.0;
            if (resourceView.state == RankedResourceDownloadState::Downloading && resourceView.total > 0) {
                progress = std::min(1.0, (
                    static_cast<double>(resourceView.ready) +
                    static_cast<double>(resourceView.activeProgress) / 100.0
                ) / static_cast<double>(resourceView.total));
            }
            progress = std::clamp(progress, 0.0, 1.0);
            if (m_resourceProgressFill) {
                m_resourceProgressFill->setContentSize({
                    static_cast<float>(m_resourceProgressWidth * progress),
                    10.0f
                });
            }
            if (m_resourceProgressLabel) {
                m_resourceProgressLabel->setString(
                    fmt::format("{}%", static_cast<int>(std::round(progress * 100.0))).c_str()
                );
            }
            return;
        }

        if (m_page != Page::Live) return;
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) {
            if (m_mainStatusLabel) {
                auto detail = view.error.empty() ? view.status : view.error;
                if (!m_localMessage.empty()) detail = m_localMessage;
                m_mainStatusLabel->setString(shorten(detail, 90).c_str());
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
                    scoreComparison(ownScore, opponentScore).c_str()
                );
            }
            if (m_spectatorHintLabel) {
                std::string hint = tr("round-resolve-hint", "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS");
                if (!opponentAttemptActive && match.state == "LAST_ATTEMPT_WINDOW") {
                    auto const remaining = std::max<std::int64_t>(
                        0,
                        RankedRuntime::get().deadlineSeconds().value_or(0)
                    );
                    hint = corum::ranked::i18n::isKorean()
                        ? fmt::format("{} {}초", tr("last-attempt-hint", "OPPONENT HAS TO START THE LAST ATTEMPT"), remaining)
                        : fmt::format("OPPONENT HAS {}s TO START THE LAST ATTEMPT", remaining);
                }
                m_spectatorHintLabel->setString(hint.c_str());
            }
        }
    }

    void render() {
        clearUi();
        auto const& view = RankedRuntime::get().view();
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
        corum::ranked::ui::fitLabelWidth(title, size.width - 170.0f, 0.70f, 0.40f);
        m_root->addChild(title, 6);

        auto* eyebrow = makeLabel(
            eyebrowText,
            0.26f,
            {size.width / 2.0f, size.height - 53.0f},
            accent,
            "semibold"
        );
        corum::ranked::ui::fitLabelWidth(eyebrow, 238.0f, 0.26f, 0.18f);
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
            "한국어",
            false,
            selected == corum::ranked::i18n::Language::Korean
        );
        koreanVisual->setScale(0.48f);
        auto* korean = CCMenuItemSpriteExtra::create(
            koreanVisual,
            this,
            menu_selector(CorumRankedLayer::onKorean)
        );
        korean->setPosition({128.0f, size.height - 28.0f});
        korean->setID("ranked-language-korean"_spr);
        m_menu->addChild(korean);
    }

    void addStatusPill(std::string const& text, CCPoint position, ccColor3B color) {
        auto* panel = makePanel({106.0f, 28.0f}, position, color, 190);
        m_root->addChild(panel, 1);
        auto* label = makeLabel(text, 0.32f, position, corum::ranked::ui::Text, "semibold");
        corum::ranked::ui::fitLabelWidth(label, 96.0f, 0.32f, 0.20f);
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
        corum::ranked::ui::fitLabelWidth(body, panelWidth - 34.0f, 0.29f, 0.19f);
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
        corum::ranked::ui::fitLabelWidth(action, panelWidth - 34.0f, 0.22f, 0.15f);
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
            corum::ranked::ui::fitLabelWidth(headingLabel, panelWidth - 36.0f, 0.23f, 0.16f);
            m_root->addChild(headingLabel, 4);

            auto* detailLabel = makeLabel(
                detail,
                0.23f,
                {size.width / 2.0f, rowY - 14.0f},
                corum::ranked::ui::Text,
                "semibold"
            );
            corum::ranked::ui::fitLabelWidth(detailLabel, panelWidth - 36.0f, 0.23f, 0.13f);
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

    void addStreakLabel(
        int streak,
        CCPoint position,
        float scale = 0.27f,
        float maximumWidth = 150.0f,
        bool showDivider = true
    ) {
        if (std::abs(streak) < 2) return;
        auto const winning = streak > 0;
        auto const color = winning ? kGold : kRed;

        if (showDivider) {
            auto* divider = CCLayerColor::create({color.r, color.g, color.b, 100});
            divider->setContentSize({std::min(112.0f, maximumWidth), 0.75f});
            divider->setPosition({position.x - divider->getContentSize().width / 2.0f, position.y + 8.0f});
            m_root->addChild(divider, 6);
        }

        auto* label = makeLabel(
            corum::ranked::i18n::streak(std::abs(streak), winning),
            scale,
            position,
            color,
            "black"
        );
        corum::ranked::ui::fitLabelWidth(label, maximumWidth, scale, std::max(0.16f, scale * 0.72f));
        m_root->addChild(label, 7);
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

        auto* iconPlate = makeNeonPanel({66.0f, 66.0f}, {center.x, center.y + 29.0f}, accent, {5, 11, 23}, 248);
        m_root->addChild(iconPlate, 2);
        if (!self && corum::ranked::ui::hasPersistentRankAmbient(tier)) {
            auto* ambient = corum::ranked::ui::createRankAmbientFx(
                tier,
                {54.0f, 54.0f},
                corum::ranked::ui::RankBadgeDisplay::Compact
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
        addStreakLabel(profile.resultStreak, {center.x, center.y + 66.0f}, 0.22f, 126.0f, false);

        auto* nameLabel = makeLabel(shorten(upper(name), 15), 0.38f, {center.x, center.y - 11.0f}, corum::ranked::ui::Text, "semibold");
        corum::ranked::ui::fitLabelWidth(nameLabel, 126.0f, 0.38f, 0.25f);
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
            corum::ranked::ui::fitLabelWidth(titleLabel, 134.0f, 0.20f, 0.14f);
            m_root->addChild(titleLabel, 5);
        }

        auto const placement = profile.placementGames < profile.placementGamesRequired;
        auto const captionText = placement
            ? tr("placement", "PLACEMENT")
            : (profile.promotion.active ? tr("promotion", "PROMOTION")
                : (profile.demotionWarning ? tr("demotion-warning", "DEMOTION WARNING") : tr("rating", "RATING")));
        auto const captionY = center.y - (hasTitle ? 40.0f : 32.0f);
        auto const valueY = center.y - (hasTitle ? 54.0f : 47.0f);
        // Put the warning in the status row rather than over the tier plate.
        auto const showDemotion = !placement && !profile.promotion.active && profile.demotionWarning;
        auto* lpCaption = makeLabel(captionText, showDemotion ? 0.20f : 0.23f,
            {center.x, captionY}, showDemotion ? kRed : kCyan, showDemotion ? "black" : "semibold");
        corum::ranked::ui::fitLabelWidth(lpCaption, 134.0f, showDemotion ? 0.20f : 0.23f, 0.14f);
        m_root->addChild(lpCaption, 4);
        if (placement) {
            addPlacementProgress(profile.placementGames, std::max(1, profile.placementGamesRequired), {center.x, valueY});
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {center.x, valueY});
            auto* detail = makeLabel(
                corum::ranked::i18n::promotionMatch(profile.promotion.winsRequired),
                0.20f,
                {center.x, center.y - (hasTitle ? 66.0f : 61.0f)},
                ccc3(145, 255, 125),
                "semibold"
            );
            corum::ranked::ui::fitLabelWidth(detail, 134.0f, 0.20f, 0.14f);
            m_root->addChild(detail, 6);
        } else {
            auto* lpLabel = makeLabel(
                combineTierAndScore
                    ? fmt::format("{} - {} LP", upper(tier), profile.visibleRankedScore.value_or(0))
                    : fmt::format("{} LP", profile.visibleRankedScore.value_or(0)),
                0.35f,
                {center.x, valueY},
                ccc3(240, 244, 250),
                "black"
            );
            corum::ranked::ui::fitLabelWidth(lpLabel, 124.0f, 0.35f, 0.22f);
            m_root->addChild(lpLabel, 4);
            if (outRefs) outRefs->scoreLabel = lpLabel;
        }

        auto* tierPlate = makeTextPlate(upper(tier), {96.0f, 24.0f}, {center.x, center.y - 74.0f}, accent, 0.28f, accent, "black");
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

    void renderResources() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        auto const resourceView = RankedAudioManager::get().downloadView();
        auto const assetView = corum::ranked::ui::RankAssetPackManager::get().view();
        auto const assetsDownloading = assetView.state == corum::ranked::ui::RankAssetPackState::Downloading;
        auto const assetsFailed = assetView.state == corum::ranked::ui::RankAssetPackState::Failed;
        auto const assetsExternalReady = assetView.state == corum::ranked::ui::RankAssetPackState::Ready;

        auto* panel = makeNeonPanel(
            {std::min(390.0f, size.width - 90.0f), 220.0f},
            {size.width / 2.0f, size.height / 2.0f - 2.0f},
            kCyan,
            kDeepPanel,
            248
        );
        m_root->addChild(panel, 1);

        std::string title = tr("ranked-resources", "RANKED RESOURCES");
        std::string subtitle = tr("resources-required", "Additional resources are required.");
        ccColor3B accent = kCyan;
        if (resourceView.state == RankedResourceDownloadState::Downloading || assetsDownloading) {
            title = tr("downloading-resources", "DOWNLOADING RESOURCES");
            subtitle = tr("keep-open", "Keep this screen open until the download is complete.");
            accent = kGold;
        } else if (resourceView.state == RankedResourceDownloadState::Ready) {
            title = tr("all-resources-ready", "ALL RESOURCES READY");
            subtitle = tr("everything-ready", "Everything required for Ranked is ready.");
            accent = kGreen;
        } else if (resourceView.state == RankedResourceDownloadState::Failed) {
            title = tr("resources-failed", "SOME RESOURCES FAILED");
            subtitle = tr("retry-before-continuing", "Retry the failed download before continuing.");
            accent = kRed;
        }

        auto* titleLabel = makeLabel(title, 0.52f, {size.width / 2.0f, size.height / 2.0f + 76.0f}, accent, "goldFont.fnt");
        corum::ranked::ui::fitLabelWidth(titleLabel, size.width - 150.0f, 0.52f, 0.30f);
        m_root->addChild(titleLabel, 4);
        auto* subLabel = makeLabel(subtitle, 0.27f, {size.width / 2.0f, size.height / 2.0f + 47.0f}, ccc3(210, 220, 238), "semibold");
        corum::ranked::ui::fitLabelWidth(subLabel, size.width - 150.0f, 0.27f, 0.18f);
        m_root->addChild(subLabel, 4);

        auto* count = makeLabel(
            fmt::format("{} / {} {}", resourceView.ready, resourceView.total, tr("ready", "READY")),
            0.38f,
            {size.width / 2.0f, size.height / 2.0f + 10.0f},
            resourceView.ready == resourceView.total ? kGreen : ccc3(240, 244, 250),
            "black"
        );
        m_resourceCountLabel = count;
        m_root->addChild(count, 4);

        auto const songState = resourceView.state == RankedResourceDownloadState::Ready
            ? "READY"
            : resourceView.state == RankedResourceDownloadState::Downloading
                ? "DOWNLOADING"
                : resourceView.state == RankedResourceDownloadState::Failed
                    ? "FAILED"
                    : "NOT READY";
        auto* songRow = makeLabel(
            fmt::format("Song — {}", songState),
            0.25f,
            {size.width / 2.0f, size.height / 2.0f - 5.0f},
            resourceView.state == RankedResourceDownloadState::Ready ? kGreen :
                resourceView.state == RankedResourceDownloadState::Failed ? kRed : kGold,
            "semibold"
        );
        m_root->addChild(songRow, 4);

        std::string assetStateText = "READY (BUNDLED)";
        ccColor3B assetStateColor = kGreen;
        if (assetsExternalReady) assetStateText = "READY (EXTERNAL)";
        else if (assetsDownloading) { assetStateText = "DOWNLOADING"; assetStateColor = kGold; }
        else if (assetsFailed) { assetStateText = "FAILED — BUNDLED FALLBACK"; assetStateColor = kGold; }
        auto* assetRow = makeLabel(
            fmt::format("Rank Visuals — {}", assetStateText),
            0.25f,
            {size.width / 2.0f, size.height / 2.0f - 21.0f},
            assetStateColor,
            "semibold"
        );
        corum::ranked::ui::fitLabelWidth(assetRow, size.width - 150.0f, 0.25f, 0.16f);
        m_root->addChild(assetRow, 4);

        auto const barWidth = std::min(270.0f, size.width - 180.0f);
        auto* barBack = CCLayerColor::create({9, 17, 32, 235});
        barBack->setContentSize({barWidth, 10.0f});
        barBack->setPosition({size.width / 2.0f - barWidth / 2.0f, size.height / 2.0f - 38.0f});
        m_root->addChild(barBack, 3);
        double progress = resourceView.total > 0
            ? static_cast<double>(resourceView.ready) / static_cast<double>(resourceView.total)
            : 1.0;
        if (resourceView.state == RankedResourceDownloadState::Downloading && resourceView.total > 0) {
            progress = std::min(1.0, (
                static_cast<double>(resourceView.ready) + static_cast<double>(resourceView.activeProgress) / 100.0
            ) / static_cast<double>(resourceView.total));
        }
        auto* barFill = CCLayerColor::create({accent.r, accent.g, accent.b, 245});
        barFill->setContentSize({static_cast<float>(barWidth * std::clamp(progress, 0.0, 1.0)), 10.0f});
        barFill->setPosition(barBack->getPosition());
        m_resourceProgressFill = barFill;
        m_resourceProgressWidth = barWidth;
        m_root->addChild(barFill, 4);

        if (resourceView.state == RankedResourceDownloadState::Downloading) {
            auto* progressLabel = makeLabel(
                fmt::format("{}%", static_cast<int>(std::round(progress * 100.0))),
                0.22f,
                {size.width / 2.0f, size.height / 2.0f - 58.0f},
                kGold
            );
            m_resourceProgressLabel = progressLabel;
            m_root->addChild(progressLabel, 4);
        } else if (resourceView.state == RankedResourceDownloadState::Ready) {
            addButton(tr("continue", "CONTINUE"), {size.width / 2.0f, size.height / 2.0f - 72.0f}, menu_selector(CorumRankedLayer::onContinueResources), false, 0.72f);
        } else if (resourceView.state == RankedResourceDownloadState::Failed) {
            addButton(tr("retry-failed", "RETRY FAILED"), {size.width / 2.0f, size.height / 2.0f - 72.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), true, 0.68f);
        } else {
            addButton(tr("download-all", "DOWNLOAD ALL"), {size.width / 2.0f, size.height / 2.0f - 72.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), false, 0.72f);
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

        auto const threeCountText = corum::ranked::i18n::isKorean()
            ? std::string("3회")
            : std::string("3 STAGES");
        auto const fiveCountText = corum::ranked::i18n::isKorean()
            ? std::string("5회")
            : std::string("5 STAGES");
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
        corum::ranked::ui::fitLabelWidth(countHintLabel, size.width - 74.0f, 0.20f, 0.14f);
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
                : fmt::format("{} · {}", reference->title, reference->difficulty);
            auto* referenceLabel = makeLabel(
                referenceText,
                0.14f,
                {x, y - 16.0f},
                corum::ranked::ui::TextMuted,
                "semibold"
            );
            corum::ranked::ui::fitLabelWidth(referenceLabel, 112.0f, 0.14f, 0.10f);
            m_root->addChild(referenceLabel, 5);
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
            corum::ranked::ui::fitLabelWidth(status, size.width - 90.0f, 0.20f, 0.14f);
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
        corum::ranked::ui::fitLabelWidth(warning, 330.0f, 0.29f, 0.18f);
        m_root->addChild(warning, 5);
        auto* result = makeLabel(
            tr("skip-placement-result", "YOU WILL BE PLACED AT RED - 150 LP."),
            0.38f,
            {size.width / 2.0f, size.height / 2.0f + 4.0f},
            kRed,
            "black"
        );
        corum::ranked::ui::fitLabelWidth(result, 320.0f, 0.38f, 0.22f);
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

    void renderMain(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);
        addLanguageSelector();
        addScreenHeader("CORUM RANKED", tr("competitive-ladder", "COMPETITIVE 1V1 LADDER"), kGold, kCyan);

        auto* historyVisual = makeActionButtonVisual(tr("history", "HISTORY"));
        historyVisual->setScale(0.66f);
        auto* history = CCMenuItemSpriteExtra::create(historyVisual, this, menu_selector(CorumRankedLayer::onHistory));
        history->setPosition({size.width - 53.0f, size.height - 28.0f});
        m_menu->addChild(history);

        auto* trophyVisual = makeActionButtonVisual(tr("achievements", "TROPHY"));
        trophyVisual->setScale(0.66f);
        auto* trophy = CCMenuItemSpriteExtra::create(
            trophyVisual,
            this,
            menu_selector(CorumRankedLayer::onAchievements)
        );
        trophy->setPosition({size.width - 133.0f, size.height - 28.0f});
        trophy->setID("ranked-achievement-trophy"_spr);
        m_menu->addChild(trophy);
        auto const badgeText = corum::ranked::achievementBadgeText(
            view.achievements.unclaimedRewardCount
        );
        if (!badgeText.empty()) {
            // Do not use a heavily downscaled vanilla GD button sprite here.
            // Some native renderers expose its internal sprite pieces as detached
            // squares at this size. A Corum pill is deterministic on every target.
            auto const trophySize = trophyVisual->getContentSize();
            auto const badgeWidth = badgeText == "99+" ? 38.0f : 26.0f;
            auto* badge = corum::ranked::ui::pill(
                badgeText,
                {badgeWidth, 19.0f},
                {trophySize.width - 5.0f, trophySize.height - 4.0f},
                kRed,
                badgeText == "99+" ? 0.27f : 0.35f,
                corum::ranked::ui::Text,
                true
            );
            badge->setID("ranked-achievement-notification-badge"_spr);
            trophyVisual->addChild(badge, 10);
        }

        auto const leftCenter = CCPoint{size.width / 2.0f - 118.0f, size.height / 2.0f - 2.0f};
        auto const rightCenter = CCPoint{size.width / 2.0f + 118.0f, size.height / 2.0f - 2.0f};

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

        auto* profilePanel = makeNeonPanel({188.0f, 196.0f}, leftCenter, tierColor(upper(view.profileTier)), kDeepPanel, 246);
        m_root->addChild(profilePanel, 1);
        if (profile.promotion.active) addPromotionAura(leftCenter, {188.0f, 196.0f});
        auto* profileTitle = makeLabel(tr("your-rank", "YOUR RANK"), 0.34f, {leftCenter.x, leftCenter.y + 77.0f}, kCyan, "semibold");
        m_root->addChild(profileTitle, 4);
        auto* emblem = corum::ranked::ui::createAnimatedRankBadge(
            view.profileTier,
            {68.0f, 68.0f},
            corum::ranked::ui::RankBadgeDisplay::Large
        );
        emblem->setPosition({leftCenter.x, leftCenter.y + 20.0f});
        m_root->addChild(emblem, 4);
        addStreakLabel(profile.resultStreak, {leftCenter.x, leftCenter.y + 60.0f}, 0.28f, 154.0f);
        auto* tier = makeLabel(upper(view.profileTier), 0.54f, {leftCenter.x, leftCenter.y - 25.0f}, tierColor(upper(view.profileTier)), "black");
        corum::ranked::ui::fitLabelWidth(tier, 150.0f, 0.54f, 0.32f);
        m_root->addChild(tier, 4);
        auto const placement = profile.placementGames < profile.placementGamesRequired;
        if (placement) {
            auto const completedStages = std::max(
                profile.placementGames,
                static_cast<int>(view.placement.stageResults.size())
            );
            auto const placementStageTotal = view.placement.active ? view.placement.totalStages : profile.placementGamesRequired;
            addPlacementProgress(completedStages, std::max(1, placementStageTotal), {leftCenter.x, leftCenter.y - 51.0f}, 16.0f, 0.30f);
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {leftCenter.x, leftCenter.y - 49.0f}, 16.0f, 0.30f);
        } else {
            auto* rating = makeLabel(fmt::format("{} LP", view.profileScore), 0.46f, {leftCenter.x, leftCenter.y - 53.0f}, ccc3(240, 244, 250), "black");
            corum::ranked::ui::fitLabelWidth(rating, 150.0f, 0.46f, 0.28f);
            m_root->addChild(rating, 4);
        }
        std::string profileHintText = tr("seasonal-progress", "SEASONAL LADDER PROGRESS");
        ccColor3B profileHintColor = ccc3(195, 205, 224);
        if (placement) profileHintText = tr("placement-matches", "PLACEMENT MATCHES");
        else if (profile.promotion.active) {
            profileHintText = corum::ranked::i18n::promotionMatch(profile.promotion.winsRequired);
            profileHintColor = ccc3(145, 255, 125);
        } else if (profile.demotionWarning) {
            profileHintText = tr("demotion-warning", "DEMOTION WARNING");
            profileHintColor = kRed;
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
            corum::ranked::ui::fitLabelWidth(equippedTitle, 168.0f, 0.20f, 0.14f);
            equippedTitle->setID("ranked-profile-equipped-title"_spr);
            m_root->addChild(equippedTitle, 4);
        }
        // The progression warning always has its own line, even with a title.
        auto* profileHint = makeLabel(profileHintText, hasTitle ? 0.22f : 0.25f,
            {leftCenter.x, leftCenter.y - (hasTitle ? 85.0f : 79.0f)}, profileHintColor,
            profile.demotionWarning && !placement && !profile.promotion.active ? "black" : "semibold");
        corum::ranked::ui::fitLabelWidth(profileHint, 168.0f, hasTitle ? 0.22f : 0.25f, 0.15f);
        profileHint->setID("ranked-profile-progress-status"_spr);
        m_root->addChild(profileHint, 4);

        auto* actionPanel = makeNeonPanel({188.0f, 196.0f}, rightCenter, kBlue, kDeepPanel, 246);
        m_root->addChild(actionPanel, 1);
        auto* actionTitle = makeLabel(tr("matchmaking", "MATCHMAKING"), 0.34f, {rightCenter.x, rightCenter.y + 77.0f}, kCyan, "semibold");
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

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        if (!view.placement.required) {
            addButton("DEBUG BOT MATCH", {96.0f, 30.0f}, menu_selector(CorumRankedLayer::onDebugBot), true, 0.54f);
        }
#endif
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (view.stage == RuntimeStage::Ready) {
            addButton("DEBUG SPECTATE", {96.0f, 62.0f}, menu_selector(CorumRankedLayer::onDebugSpectate), true, 0.50f);
        }
#endif

        auto detail = view.error.empty() ? view.status : view.error;
        if (!m_localMessage.empty()) detail = m_localMessage;
        if (!detail.empty()) {
            auto* statusPanel = makeNeonPanel({std::min(380.0f, size.width - 84.0f), 34.0f}, {size.width / 2.0f, 27.0f}, view.error.empty() ? kBlue : kRed, kDeepPanel2, 238);
            m_root->addChild(statusPanel, 1);
            auto* status = makeLabel(shorten(detail, 90), 0.28f, {size.width / 2.0f, 27.0f}, view.error.empty() ? ccc3(220, 228, 244) : kRed, "semibold");
            corum::ranked::ui::fitLabelWidth(status, size.width - 120.0f, 0.28f, 0.18f);
            m_mainStatusLabel = status;
            m_root->addChild(status, 4);
        }
    }

    void renderPlacementMap(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& placement = view.placement;
        auto const& match = view.match;
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
            auto* map = makeLabel(
                upper(match.currentMap->title),
                0.42f,
                {size.width / 2.0f, size.height / 2.0f + 58.0f},
                corum::ranked::ui::Text,
                "black"
            );
            corum::ranked::ui::fitLabelWidth(map, 278.0f, 0.42f, 0.25f);
            m_root->addChild(map, 5);
            auto* difficulty = makeLabel(
                match.currentMap->difficulty,
                0.29f,
                {size.width / 2.0f, size.height / 2.0f + 35.0f},
                difficultyColor(match.currentMap->difficulty),
                "semibold"
            );
            m_root->addChild(difficulty, 5);
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
        corum::ranked::ui::fitLabelWidth(state, size.width - 90.0f, 0.25f, 0.16f);
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
            corum::ranked::ui::fitLabelWidth(title, 76.0f, 0.20f, 0.14f);
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
            corum::ranked::ui::fitLabelWidth(score, 76.0f, 0.18f, 0.13f);
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
            corum::ranked::ui::fitLabelWidth(title, 76.0f, 0.20f, 0.14f);
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
            corum::ranked::ui::fitLabelWidth(score, 76.0f, 0.18f, 0.13f);
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
                ? (corum::ranked::i18n::isKorean()
                    ? fmt::format("전체 {}개 맵 완료", view.placement.totalStages * view.placement.mapsPerStage)
                    : fmt::format("ALL {} MAPS COMPLETE", view.placement.totalStages * view.placement.mapsPerStage))
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
            fmt::format("{} - {} LP", upper(finalResult.displayedTier), finalResult.visibleRankedScore),
            0.55f,
            {size.width / 2.0f, size.height / 2.0f - 49.0f},
            tierColor(finalResult.displayedTier),
            "black"
        );
        corum::ranked::ui::fitLabelWidth(tier, size.width - 100.0f, 0.55f, 0.32f);
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
            corum::ranked::ui::fitLabelWidth(error, size.width - 100.0f, 0.22f, 0.15f);
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
        corum::ranked::ui::fitLabelWidth(hint, 176.0f, 0.24f, 0.16f);
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

            auto* map = makeLabel(twoLineTitle(upper(candidate.title), 10), 0.31f, {x, centerY + 15.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitLabelWidth(map, cardWidth - 12.0f, 0.31f, 0.20f);
            m_root->addChild(map, 4);

            auto* difficultyPlate = makeNeonPanel({cardWidth - 22.0f, 28.0f}, {x, centerY - 20.0f}, diffColor, {11, 20, 36}, 245);
            m_root->addChild(difficultyPlate, 2);
            auto* diffCaption = makeLabel(tr("difficulty", "DIFFICULTY"), 0.23f, {x, centerY - 14.0f}, kCyan, "semibold");
            m_root->addChild(diffCaption, 4);
            auto* diff = makeLabel(candidate.difficulty, 0.35f, {x, centerY - 27.0f}, diffColor, "black");
            corum::ranked::ui::fitLabelWidth(diff, cardWidth - 28.0f, 0.35f, 0.22f);
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
        corum::ranked::ui::fitLabelWidth(privacy, 236.0f, 0.25f, 0.16f);
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
        corum::ranked::ui::fitLabelWidth(title, size.width - 180.0f, deathmatch ? 0.62f : 0.56f, 0.34f);
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
            auto* mapName = makeLabel(twoLineTitle(upper(match.currentMap->title), 15), 0.40f, {size.width / 2.0f, mapY + 7.0f}, corum::ranked::ui::Text, "semibold");
            corum::ranked::ui::fitLabelWidth(mapName, 154.0f, 0.40f, 0.24f);
            m_root->addChild(mapName, 6);

            auto const diffColor = difficultyColor(match.currentMap->difficulty);
            auto* diffPlate = makeNeonPanel({74.0f, 32.0f}, {size.width / 2.0f, mapY - 27.0f}, diffColor, {12, 17, 27}, 246);
            m_root->addChild(diffPlate, 2);
            auto* difficulty = makeLabel(match.currentMap->difficulty, 0.42f, {size.width / 2.0f, mapY - 27.0f}, diffColor, "black");
            corum::ranked::ui::fitLabelWidth(difficulty, 62.0f, 0.42f, 0.25f);
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
        corum::ranked::ui::fitLabelWidth(m_prepareLoadStageLabel, 138.0f, 0.18f, 0.12f);
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
        corum::ranked::ui::fitLabelWidth(title, size.width - 70.0f, 0.48f, 0.30f);
        m_root->addChild(title, 5);
        auto* subtitle = makeLabel(tr("waiting-opponent", "WAITING FOR OPPONENT"), 0.24f, {size.width / 2.0f, size.height - 59.0f}, kCyan);
        m_root->addChild(subtitle, 5);

        auto* panel = makeNeonPanel({246.0f, 142.0f}, {size.width / 2.0f, size.height / 2.0f - 4.0f}, kBlue, kDeepPanel, 238);
        m_root->addChild(panel, 1);

        auto* name = makeLabel(upper(shorten(opponent, 18)), 0.32f, {size.width / 2.0f, size.height / 2.0f + 38.0f});
        corum::ranked::ui::fitLabelWidth(name, 205.0f, 0.32f, 0.21f);
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
            scoreComparison(ownScore, opponentScore),
            0.22f,
            {size.width / 2.0f, size.height / 2.0f - 53.0f},
            ccc3(218, 228, 244),
            "semibold"
        );
        m_spectatorScoreLabel = score;
        corum::ranked::ui::fitLabelWidth(score, 225.0f, 0.22f, 0.16f);
        m_root->addChild(score, 5);

        std::string hintText = tr("round-resolve-hint", "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS");
        if (!opponentAttemptActive && match.state == "LAST_ATTEMPT_WINDOW") {
            auto const seconds = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
            hintText = corum::ranked::i18n::isKorean()
                ? fmt::format("{} {}초", tr("last-attempt-hint", "OPPONENT HAS TO START THE LAST ATTEMPT"), seconds)
                : fmt::format("OPPONENT HAS {}s TO START THE LAST ATTEMPT", seconds);
        }
        auto* hint = makeLabel(hintText, 0.24f, {size.width / 2.0f, 28.0f}, ccc3(195, 205, 224), "semibold");
        m_spectatorHintLabel = hint;
        corum::ranked::ui::fitLabelWidth(hint, size.width - 80.0f, 0.24f, 0.16f);
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
        corum::ranked::ui::fitLabelWidth(title, size.width - 90.0f, 0.62f, 0.34f);
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

    void addClearChecks(int clears, CCPoint base, bool rightAligned = false) {
        auto const count = std::clamp(clears, 0, 3);
        for (int i = 0; i < count; ++i) {
            auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            check->setScale(0.26f);
            check->setColor(kGreen);
            check->setPosition({base.x + (rightAligned ? -1.0f : 1.0f) * i * 13.0f, base.y});
            m_root->addChild(check, 5);
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
            fmt::format("{} - {} LP", upper(before.displayedTier), *before.visibleRankedScore),
            0.27f,
            {centerX, finalY},
            ccc3(240, 244, 250),
            "goldFont.fnt"
        );
        corum::ranked::ui::fitLabelWidth(previousScore, 118.0f, 0.27f, 0.16f);
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
            fmt::format("{} - {} LP", upper(before.displayedTier), *before.visibleRankedScore),
            0.27f,
            {centerX, finalY},
            ccc3(240, 244, 250),
            "goldFont.fnt"
        );
        corum::ranked::ui::fitLabelWidth(prior, 118.0f, 0.27f, 0.16f);
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
        RankedProfileView const& after
    ) {
        auto const transition = authoritativeTransitionType(event, before, after);
        if (!transition || m_rankTransitionLayer) return false;

        auto* transitionLayer = RankTransitionLayer::create(
            before.displayedTier,
            after.displayedTier,
            *transition,
            ratingEventText(event).first,
            tr("tap-to-skip", "TAP TO SKIP"),
            tr("continue", "CONTINUE"),
            [this] {
                m_rankTransitionLayer = nullptr;
                setKeypadEnabled(true);
                if (m_menu) {
                    corum::ranked::ui::cancelMenuTouch(m_menu);
                    m_menu->setTouchEnabled(true);
                }
                playNextAchievementToast();
            }
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

    void playNextAchievementToast() {
        // Result rank reveals own the foreground first. Keep notices queued until
        // the settled Match/Placement Result UI is actually visible.
        if (m_achievementToastPlaying || !m_achievementToastRoot || m_rankTransitionLayer) return;
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

        auto const accent = notice.type == "PROGRESSIVE" && notice.level
            ? [&notice] {
                switch (*notice.level) {
                    case 1: return ccc3(166, 172, 181);
                    case 2: return ccc3(184, 115, 51);
                    case 3: return ccc3(200, 208, 218);
                    case 4: return ccc3(244, 197, 66);
                    case 5: return ccc3(141, 235, 255);
                    default: return kCyan;
                }
            }()
            : rarityColor(notice.rarity);
        auto* panel = makeNeonPanel({330.0f, 67.0f}, CCPointZero, accent, kDeepPanel, 255);
        group->addChild(panel, 0);
        auto const heading = notice.type == "ONE_SHOT"
            ? tr("achievement-unlocked", "ACHIEVEMENT UNLOCKED")
            : notice.maxed
                ? tr("achievement-maxed", "ACHIEVEMENT MAXED")
                : tr("achievement-upgraded", "ACHIEVEMENT UPGRADED");
        auto* headingLabel = makeLabel(heading, 0.22f, {0.0f, 19.0f}, accent, "semibold");
        group->addChild(headingLabel, 2);
        auto const baseName = corum::ranked::i18n::isKorean() ? notice.nameKo : notice.nameEn;
        auto const displayName = notice.displayLabel.empty()
            ? baseName
            : fmt::format("{} - {}", baseName, notice.displayLabel);
        auto* name = makeLabel(displayName, 0.34f, {0.0f, 0.0f}, corum::ranked::ui::Text, "black");
        corum::ranked::ui::fitLabelWidth(name, 290.0f, 0.34f, 0.22f);
        group->addChild(name, 2);
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
            corum::ranked::ui::fitLabelWidth(reason, size.width - 100.0f, 0.28f, 0.18f);
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
            auto* row = makeLabel(fmt::format("ROUND {}   {} : {}", round.roundNumber, round.scoreA, round.scoreB), 0.31f, {size.width / 2.0f, y}, corum::ranked::ui::Text, "semibold");
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

        addButton(tr("queue-again", "QUEUE AGAIN"), {size.width / 2.0f, 24.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.52f);

        // A result can be rendered again after visiting History or after a
        // presentation refresh. Play a tier transition only once per match;
        // subsequent renders immediately show the authoritative settled UI.
        auto const transitionKey = match.matchId.empty()
            ? fmt::format("{}:{}:{}", match.playerAName, match.playerBName, match.stateVersion)
            : match.matchId;
        if (m_rankTransitionPlayedKey != transitionKey) {
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
        if (m_rankTransitionLayer && m_menu) m_menu->setTouchEnabled(false);
        enqueueAchievementToasts(
            "match:" + transitionKey,
            match.achievementUnlocks
        );
    }

    ccColor3B achievementCardAccent(corum::ranked::AchievementCardView const& card) const {
        if (card.type == "ONE_SHOT") return rarityColor(card.rarity);
        if (card.currentLevel > 0 && static_cast<std::size_t>(card.currentLevel) <= card.levels.size()) {
            return colorFromCssHex(
                card.levels[static_cast<std::size_t>(card.currentLevel - 1)].displayColor,
                kCyan
            );
        }
        return ccc3(112, 124, 146);
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
        auto const name = corum::ranked::i18n::isKorean() ? card.nameKo : card.nameEn;
        auto* heading = makeLabel(
            name,
            0.50f,
            {size.width / 2.0f, size.height / 2.0f + 62.0f},
            achievementCardAccent(card),
            "black"
        );
        corum::ranked::ui::fitLabelWidth(heading, 320.0f, 0.50f, 0.32f);
        m_root->addChild(heading, 74);
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
            button->setEnabled(available && !current);
        }
        auto* clear = addButton(
            tr("clear-title", "CLEAR TITLE"),
            {size.width / 2.0f - 70.0f, size.height / 2.0f - 52.0f},
            menu_selector(CorumRankedLayer::onAchievementTitleClear),
            true,
            0.56f
        );
        clear->setEnabled(view.achievements.equippedTitle.active);
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
            {118.0f, 27.0f},
            {size.width / 2.0f - 68.0f, size.height - 75.0f},
            kGold,
            0.38f,
            kGold,
            "black"
        );
        m_root->addChild(ap, 5);
        auto* pending = makeTextPlate(
            fmt::format("{} {}", view.achievements.unclaimedRewardCount, tr("claimable", "CLAIMABLE")),
            {138.0f, 27.0f},
            {size.width / 2.0f + 68.0f, size.height - 75.0f},
            view.achievements.unclaimedRewardCount > 0 ? kRed : kCyan,
            0.34f,
            view.achievements.unclaimedRewardCount > 0 ? ccc3(255, 225, 230) : corum::ranked::ui::Text,
            "black"
        );
        m_root->addChild(pending, 5);

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
            corum::ranked::ui::fitLabelWidth(error, size.width - 90.0f, 0.23f, 0.16f);
            m_root->addChild(error, 8);
        }

        constexpr std::size_t perPage = 4;
        auto const pageCount = std::max<std::size_t>(
            1,
            (view.achievements.cards.size() + perPage - 1) / perPage
        );
        m_achievementPage = std::min(m_achievementPage, pageCount - 1);
        auto const start = m_achievementPage * perPage;
        auto const cardWidth = std::min(250.0f, size.width / 2.0f - 22.0f);
        constexpr float cardHeight = 88.0f;
        for (std::size_t slot = 0; slot < perPage && start + slot < view.achievements.cards.size(); ++slot) {
            auto const index = start + slot;
            auto const& card = view.achievements.cards[index];
            auto const column = slot % 2;
            auto const row = slot / 2;
            auto const center = CCPoint{
                column == 0 ? size.width / 4.0f : size.width * 3.0f / 4.0f,
                size.height - 134.0f - static_cast<float>(row) * 94.0f,
            };
            auto const accent = achievementCardAccent(card);
            auto* panel = makeNeonPanel({cardWidth, cardHeight}, center, accent, kDeepPanel, 246);
            m_root->addChild(panel, 1);
            auto const baseName = corum::ranked::i18n::isKorean() ? card.nameKo : card.nameEn;
            auto const title = card.type == "PROGRESSIVE" && card.currentLevel > 0
                ? fmt::format("{} - {}", baseName, card.displayLabel)
                : baseName;
            auto const titleColor =
                card.unlocked || card.currentLevel > 0
                    ? accent
                    : corum::ranked::ui::Text;
            auto* titleLabel = makeLabel(
                title,
                0.40f,
                {center.x - cardWidth / 2.0f + 10.0f, center.y + 27.0f},
                titleColor,
                "semibold",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitLabelWidth(titleLabel, cardWidth - 82.0f, 0.40f, 0.26f);
            m_root->addChild(titleLabel, 4);
            auto const description = corum::ranked::i18n::isKorean()
                ? card.descriptionKo
                : card.descriptionEn;
            auto* descriptionLabel = makeLabel(
                shorten(description, corum::ranked::i18n::isKorean() ? 40 : 52),
                0.25f,
                {center.x - cardWidth / 2.0f + 10.0f, center.y + 7.0f},
                ccc3(192, 205, 224),
                "medium",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitLabelWidth(descriptionLabel, cardWidth - 90.0f, 0.25f, 0.19f);
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
                0.30f,
                {center.x - cardWidth / 2.0f + 10.0f, center.y - 13.0f},
                card.maxed ? kGold : corum::ranked::ui::Text,
                "black",
                {0.0f, 0.5f}
            );
            corum::ranked::ui::fitLabelWidth(progressLabel, cardWidth - 92.0f, 0.30f, 0.21f);
            m_root->addChild(progressLabel, 4);
            auto* barBack = CCLayerColor::create({40, 50, 68, 220});
            barBack->setContentSize({cardWidth - 92.0f, 7.0f});
            barBack->setPosition({center.x - cardWidth / 2.0f + 10.0f, center.y - 34.0f});
            m_root->addChild(barBack, 3);
            auto* barFill = CCLayerColor::create({accent.r, accent.g, accent.b, 245});
            barFill->setContentSize({static_cast<float>((cardWidth - 92.0f) * progress), 7.0f});
            barFill->setPosition(barBack->getPosition());
            m_root->addChild(barFill, 4);

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
                    {center.x + cardWidth / 2.0f - 39.0f, center.y + 14.0f},
                    menu_selector(CorumRankedLayer::onAchievementClaim),
                    false,
                    0.50f
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
                    equipped ? tr("current", "CURRENT") : tr("set-title", "TITLE"),
                    {center.x + cardWidth / 2.0f - 39.0f, center.y - 18.0f},
                    menu_selector(CorumRankedLayer::onAchievementTitle),
                    false,
                    0.50f
                );
                titleButton->setTag(static_cast<int>(index));
                titleButton->setEnabled(titleUnlocked && !view.achievements.loading);
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

    void renderHistoryList(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        addScreenHeader(tr("match-history", "MATCH HISTORY"), tr("recent-matches", "PLACEMENT STAGES + COMPETITIVE MATCHES"), kGold, kCyan);
        auto const placementCount = std::min<std::size_t>(5, view.placement.stageResults.size());
        if (view.historyLoading && placementCount == 0) {
            auto* spinner = LoadingSpinner::create(55.0f);
            spinner->setPosition({size.width / 2.0f, size.height / 2.0f});
            m_root->addChild(spinner, 3);
            return;
        }
        if (!view.historyError.empty() && placementCount == 0) {
            m_root->addChild(makeLabel(view.historyError, 0.27f, {size.width / 2.0f, size.height / 2.0f}, kRed), 3);
            addButton(tr("retry", "RETRY"), {size.width / 2.0f, size.height / 2.0f - 40.0f}, menu_selector(CorumRankedLayer::onHistory), false, 0.55f);
            return;
        }
        if (view.history.empty() && placementCount == 0) {
            m_root->addChild(makeLabel(tr("no-completed-matches", "NO COMPLETED MATCHES"), 0.32f, {size.width / 2.0f, size.height / 2.0f}, ccc3(190, 195, 210)), 3);
            return;
        }

        std::size_t visualRow = 0;
        for (std::size_t i = 0; i < placementCount && visualRow < 7; ++i, ++visualRow) {
            auto const& stage = view.placement.stageResults[i];
            auto const map1 = stage.mapGrades.size() > 0 ? stage.mapGrades[0] : "?";
            auto const map2 = stage.mapGrades.size() > 1 ? stage.mapGrades[1] : "?";
            auto const y = size.height - 67.0f - static_cast<float>(visualRow) * 31.0f;
            auto* panel = makeNeonPanel({size.width - 110.0f, 23.0f}, {size.width / 2.0f, y}, kGold, kDeepPanel2, 232);
            m_root->addChild(panel, 1);
            auto* row = makeLabel(
                fmt::format("STAGE {}   MAP 1 {}  +  MAP 2 {}  >  {}   {:.1f}/500", stage.stageIndex, map1, map2, stage.stageGrade, stage.stageScore),
                0.27f,
                {size.width / 2.0f, y},
                kGold,
                "semibold"
            );
            corum::ranked::ui::fitLabelWidth(row, size.width - 130.0f, 0.27f, 0.17f);
            m_root->addChild(row, 3);
        }

        auto const matchSlots = visualRow >= 7 ? 0 : 7 - visualRow;
        auto const matchCount = std::min<std::size_t>(matchSlots, view.history.size());
        for (std::size_t i = 0; i < matchCount; ++i, ++visualRow) {
            auto const& item = view.history[i];
            auto const y = size.height - 67.0f - static_cast<float>(visualRow) * 31.0f;
            auto const won = item.side == item.winnerSide;
            auto* panel = makeNeonPanel({size.width - 110.0f, 23.0f}, {size.width / 2.0f, y}, won ? kGreen : kRed, kDeepPanel2, 232);
            m_root->addChild(panel, 1);
            auto const ratingText = item.ownRankedScoreDelta
                ? fmt::format("{:+d} LP", *item.ownRankedScoreDelta)
                : tr("placement", "PLACEMENT");
            auto* row = makeLabel(
                fmt::format("{}   VS {}   {}", won ? tr("win", "WIN") : tr("loss", "LOSS"), shorten(item.opponentName, 14), ratingText),
                0.30f,
                {size.width / 2.0f - 18.0f, y},
                won ? kGreen : kRed,
                "semibold"
            );
            corum::ranked::ui::fitLabelWidth(row, size.width - 190.0f, 0.30f, 0.20f);
            m_root->addChild(row, 3);
            auto* detail = addButton(tr("detail", "DETAIL"), {size.width - 72.0f, y}, menu_selector(CorumRankedLayer::onHistoryDetail), false, 0.50f);
            detail->setTag(static_cast<int>(i));
        }
    }

    void renderHistoryDetail(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        if (m_historyIndex >= view.history.size()) {
            m_page = Page::HistoryList;
            renderHistoryList(view);
            return;
        }
        auto const& item = view.history[m_historyIndex];
        auto const won = item.side == item.winnerSide;
        addScreenHeader(
            won ? tr("victory", "VICTORY") : tr("defeat", "DEFEAT"),
            fmt::format("VS {}   //   {} : {}", upper(shorten(item.opponentName, 16)), item.roundWinsA, item.roundWinsB),
            won ? kGreen : kRed,
            won ? kGreen : kRed
        );
        auto* detailPanel = makeNeonPanel(
            {std::min(344.0f, size.width - 96.0f), size.height - 98.0f},
            {size.width / 2.0f, size.height / 2.0f - 25.0f},
            won ? kGreen : kRed,
            kDeepPanel,
            224
        );
        m_root->addChild(detailPanel, 1);
        float y = size.height - 88.0f;
        for (auto const& round : item.rounds) {
            auto* row = makeLabel(fmt::format("ROUND {}  {}  {} : {}", round.roundNumber, shorten(round.mapTitle, 12), round.scoreA, round.scoreB), 0.31f, {size.width / 2.0f, y}, corum::ranked::ui::Text, "semibold");
            m_root->addChild(row, 3);
            addClearChecks(round.clearsA, {size.width / 2.0f - 48.0f, y - 13.0f});
            addClearChecks(round.clearsB, {size.width / 2.0f + 48.0f, y - 13.0f}, true);
            y -= 40.0f;
        }
        for (auto const& deathmatch : item.deathmatches) {
            auto* row = makeLabel(fmt::format("{} {}  {} : {}", tr("death-match", "DEATH MATCH"), deathmatch.sequence, deathmatch.scoreA, deathmatch.scoreB), 0.30f, {size.width / 2.0f, y}, kRed, "semibold");
            m_root->addChild(row, 3);
            y -= 32.0f;
        }
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
        if (!corum::ranked::RankedRoundPreparation::get().launch(CCDirector::sharedDirector()->getRunningScene())) return;
        m_enteringLevel = true;
        m_rankedChildScenePushed = true;
    }

    void onLowDetail(CCObject*) {
        auto* level = findPlayableMap();
        if (!level || m_enteringLevel) return;
        level->m_lowDetailModeToggled = !level->m_lowDetailModeToggled;
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
            {size.width / 2.0f - 48.0f, y}, menu_selector(CorumRankedLayer::onLowDetail), false, 0.35f);
        auto* shake = addButton(fmt::format("{}: {}", tr("disable-shake", "DISABLE SHAKE"), level && level->m_disableShakeToggled ? "ON" : "OFF"),
            {size.width / 2.0f + 47.0f, y}, menu_selector(CorumRankedLayer::onDisableShake), false, 0.35f);
        ldm->setID("ranked-round-ldm"_spr);
        shake->setID("ranked-round-disable-shake"_spr);
        ldm->setEnabled(level != nullptr);
        shake->setEnabled(level != nullptr);
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
        if (corum::ranked::ui::RankAssetPackManager::get().view().state ==
            corum::ranked::ui::RankAssetPackState::Downloading) return;
        m_resourceGateAcknowledged = true;
        m_resourceGateWasMissing = false;
        m_resourceGateMenuRestored = false;
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

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void onDebugBot(CCObject*) {
        corum::ranked::showDebugBotPasswordPopup();
    }
#endif
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    void onDebugSpectate(CCObject*) {
        corum::ranked::debug::showDebugSpectatePopup();
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
        m_page = Page::HistoryList;
        RankedRuntime::get().fetchHistory();
    }

    void onAchievements(CCObject*) {
        m_page = Page::Achievements;
        m_achievementPage = 0;
        m_titleSelectorAchievement.clear();
        RankedRuntime::get().fetchAchievements();
    }

    void onAchievementPrevious(CCObject*) {
        if (m_achievementPage > 0) --m_achievementPage;
    }

    void onAchievementNext(CCObject*) {
        auto const count = RankedRuntime::get().view().achievements.cards.size();
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
        RankedRuntime::get().equipAchievementTitle(card.id, std::nullopt);
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
        auto const index = sender ? sender->getTag() : -1;
        if (index < 0) return;
        m_historyIndex = static_cast<std::size_t>(index);
        m_page = Page::HistoryDetail;
    }

    void onHistoryBack(CCObject*) {
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

    void onCloseLayer(CCObject*) {
        if (RankedRuntime::get().view().stage == RuntimeStage::Queued) RankedRuntime::get().leaveQueue();
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
        if (m_rankTransitionLayer) return;
        if (m_page != Page::Live) {
            onHistoryBack(nullptr);
            return;
        }
        auto const& view = RankedRuntime::get().view();
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
