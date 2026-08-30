#include "RankedPopup.hpp"

#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "DebugBotPopup.hpp"
#include "RankedSongGate.hpp"
#include "ui/RankBadgeAssets.hpp"
#include "ui/RankTransitionLayer.hpp"
#include "ui/PlacementGradeBadge.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
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

std::string shorten(std::string value, std::size_t maximum) {
    if (value.size() <= maximum) return value;
    if (maximum <= 3) return value.substr(0, maximum);
    value.resize(maximum - 3);
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
    char const* font = nullptr
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
    return corum::ranked::ui::label(text, scale, position, color, weight);
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
    label->limitLabelWidth(size.width - 12.0f, scale, std::max(0.12f, scale * 0.65f));
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
        0.38f,
        {width / 2.0f, height / 2.0f},
        selected ? kGold : (danger ? ccc3(255, 224, 229) : corum::ranked::ui::Text),
        true
    );
    caption->limitLabelWidth(width - 20.0f, 0.38f, 0.24f);
    root->addChild(caption, 5);
    return root;
}

std::string resourceKey(MatchView const& match) {
    return fmt::format(
        "{}:{}:{}:{}",
        match.matchId,
        match.roundNumber,
        match.deathmatchSequence,
        match.currentMap ? match.currentMap->levelId : 0
    );
}

class CorumRankedLayer final : public CCLayerColor, public LevelManagerDelegate {
    enum class Page {
        Live,
        PlacementPoolSelection,
        PlacementSkipConfirm,
        HistoryList,
        HistoryDetail,
    };

    CCLayerColor* m_fadeRoot = nullptr;
    CCNode* m_root = nullptr;
    CCMenu* m_menu = nullptr;
    Page m_page = Page::Live;
    std::size_t m_historyIndex = 0;
    std::string m_phaseKey;
    std::string m_resourceKey;
    std::string m_lastMatchId;
    std::string m_selectedBan;
    std::string m_pendingBan;
    std::string m_localMessage;
    SteadyClock::time_point m_phaseStartedAt {};
    std::vector<int> m_songIds;
    bool m_readySent = false;
    bool m_matchFoundReadySent = false;
    bool m_enteringSongGate = false;
    bool m_enteringLevel = false;
    LevelManagerDelegate* m_previousLevelManagerDelegate = nullptr;
    GJGameLevel* m_resolvedLevel = nullptr;
    std::string m_resolvedLevelContext;
    std::string m_levelLookupContext;
    std::string m_levelLookupKey;
    std::string m_levelLookupError;
    int m_levelLookupTargetId = 0;
    int m_levelLookupAttempts = 0;
    bool m_levelLookupBusy = false;
    SteadyClock::time_point m_levelLookupStartedAt {};
    SteadyClock::time_point m_levelLookupLastRequestAt {};
    SteadyClock::time_point m_levelLookupNextAttemptAt {};
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
    bool m_uiFadingOut = false;
    bool m_fadeInNextRender = true;
    SteadyClock::time_point m_uiTransitionDeadline {};
    CCLabelBMFont* m_resourceCountLabel = nullptr;
    CCLayerColor* m_resourceProgressFill = nullptr;
    CCLabelBMFont* m_resourceProgressLabel = nullptr;
    float m_resourceProgressWidth = 0.0f;
    CCLabelBMFont* m_mainStatusLabel = nullptr;
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

    ~CorumRankedLayer() override {
        cancelExactLevelLookup();
        releaseResolvedLevel();
    }

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
        CCLayerColor::onEnter();

        auto const resumedFromRankedChild = m_rankedChildScenePushed;
        if (resumedFromRankedChild) {
            // The scene pushed by this layer has finished. Release both launch
            // guards before refresh() so maybeEnterLevel() can open the next
            // legal attempt on the same map. canEnterCurrentLevel() remains the
            // authority that blocks a fourth Death Match attempt, spectator
            // entry, results, and closed attempt windows.
            m_rankedChildScenePushed = false;
            m_enteringSongGate = false;
            m_enteringLevel = false;
            log::info("Ranked child scene returned; automatic entry guard released");
        }

        // LevelInfoLayer and PlayLayer are pushed over this Ranked scene. The
        // corresponding onExit() intentionally stops polling and input while a
        // player is inside those scenes, so every return through popScene() must
        // restore them. Without this lifecycle pair, the retained Ranked layer
        // comes back frozen on its pre-launch "OPENING LEVEL / STARTS IN 05"
        // frame and can never observe ROUND_RESULT or the following Round.
        setKeypadEnabled(true);
        if (m_menu) m_menu->setTouchEnabled(true);
        schedule(schedule_selector(CorumRankedLayer::refresh), 0.20f);

        // Reconcile any Attempt ACK or match poll that completed while the
        // Ranked scene was covered instead of waiting for another visual frame.
        refresh(0.0f);
    }

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
        if (!audioConfig.enabled || audioConfig.resources.empty()) return false;
        auto const& audio = RankedAudioManager::get();
        return audio.requiresResourceDownload() || !m_resourceGateAcknowledged;
    }

    void syncResources() {
        auto& audio = RankedAudioManager::get();
        auto const& audioConfig = RankedRuntime::get().view().client.audio;
        if (!audioConfig.enabled || audioConfig.resources.empty()) {
            m_resourceGateInitialized = true;
            m_resourceGateAcknowledged = true;
            m_resourceGateWasMissing = false;
            m_resourceGateMenuRestored = false;
            return;
        }

        auto const missing = audio.requiresResourceDownload();
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

    std::string presentationKey() const {
        if (showResourceGate()) {
            auto const resources = RankedAudioManager::get().downloadView();
            return fmt::format("resources:{}", static_cast<int>(resources.state));
        }
        if (m_page == Page::HistoryList) {
            auto const& view = RankedRuntime::get().view();
            return fmt::format(
                "history:list:{}:{}:{}",
                view.historyLoading,
                view.historyError,
                view.history.size() + view.placement.stageResults.size()
            );
        }
        if (m_page == Page::HistoryDetail) return fmt::format("history:detail:{}", m_historyIndex);
        if (m_page == Page::PlacementPoolSelection) {
            return fmt::format("placement:pool:{}:{}", RankedRuntime::get().view().revision, m_localMessage);
        }
        if (m_page == Page::PlacementSkipConfirm) return "placement:skip-confirm";
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) {
            return fmt::format(
                "stage:{}:placement:{}:{}",
                static_cast<int>(view.stage),
                view.placement.required,
                view.placement.active
            );
        }
        if (view.match.placement) {
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
            );
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
        return key;
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
            state == "DEATHMATCH_PLAYING" ||
            state == "DEATHMATCH_RESULT";
        auto const isRoundThree =
            view.match.roundNumber >= 3 &&
            (
                state == "ROUND_PREPARE" ||
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
                m_enteringSongGate = false;
                RankedRuntime::get().setSongBypassAllowed(false);
                m_enteringLevel = false;
                m_songIds.clear();
                releaseResolvedLevel();
                m_levelLookupError.clear();
                m_levelLookupAttempts = 0;
                m_levelLookupStartedAt = SteadyClock::now();
                m_levelLookupNextAttemptAt = {};
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
        if (m_menu && m_menu->m_pSelectedItem) {
            // Geode's CCMenu selected-item safety hook is not installed on iOS.
            // Clear it explicitly before destroying children so ccTouchEnded can
            // never activate a freed CCMenuItem.
            m_menu->m_pSelectedItem = nullptr;
        }
        m_root->removeAllChildrenWithCleanup(true);
        m_menu->removeAllChildrenWithCleanup(true);
    }

    void updateDynamicUi() {
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
                if (!ownReady) footer = tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
                else if (!opponentReady) footer = corum::ranked::i18n::waitingForPlayer(upper(shorten(match.opponentName, 13)));
                else footer = tr("starting", "STARTING...");
            }
            m_prepareFooterLabel->setString(footer.c_str());
        }

        if (
            match.state == "ROUND_PREPARE" ||
            match.state == "DEATHMATCH_PREPARE"
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
        title->limitLabelWidth(size.width - 170.0f, 0.70f, 0.40f);
        m_root->addChild(title, 6);

        auto* eyebrow = makeLabel(
            eyebrowText,
            0.26f,
            {size.width / 2.0f, size.height - 53.0f},
            accent,
            "semibold"
        );
        eyebrow->limitLabelWidth(238.0f, 0.26f, 0.18f);
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
        englishVisual->setScale(0.43f);
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
        koreanVisual->setScale(0.43f);
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
        label->limitLabelWidth(96.0f, 0.32f, 0.20f);
        m_root->addChild(label, 2);
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
        label->limitLabelWidth(maximumWidth, scale, std::max(0.16f, scale * 0.72f));
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

        int frame = 1;
        if (self) {
            if (auto* game = GameManager::sharedState()) frame = std::max(1, game->getPlayerFrame());
        }

        auto* iconPlate = makeNeonPanel({66.0f, 66.0f}, {center.x, center.y + 29.0f}, accent, {5, 11, 23}, 248);
        m_root->addChild(iconPlate, 2);
        auto* icon = SimplePlayer::create(frame);
        icon->setScale(1.12f);
        icon->setPosition({center.x, center.y + 29.0f});
        if (self) {
            if (auto* game = GameManager::sharedState()) {
                icon->setColors(game->colorForIdx(game->getPlayerColor()), game->colorForIdx(game->getPlayerColor2()));
            }
        }
        m_root->addChild(icon, 4);
        addStreakLabel(profile.resultStreak, {center.x, center.y + 66.0f}, 0.22f, 126.0f, false);

        auto* nameLabel = makeLabel(shorten(upper(name), 15), 0.38f, {center.x, center.y - 11.0f}, corum::ranked::ui::Text, "semibold");
        nameLabel->limitLabelWidth(126.0f, 0.38f, 0.25f);
        m_root->addChild(nameLabel, 4);

        auto const placement = profile.placementGames < profile.placementGamesRequired;
        auto const captionText = placement
            ? tr("placement", "PLACEMENT")
            : (profile.promotion.active ? tr("promotion", "PROMOTION") : tr("rating", "RATING"));
        auto* lpCaption = makeLabel(captionText, 0.23f, {center.x, center.y - 32.0f}, kCyan, "semibold");
        m_root->addChild(lpCaption, 4);
        if (placement) {
            addPlacementProgress(profile.placementGames, std::max(1, profile.placementGamesRequired), {center.x, center.y - 47.0f});
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {center.x, center.y - 47.0f});
            auto* detail = makeLabel(
                corum::ranked::i18n::promotionMatch(profile.promotion.winsRequired),
                0.20f,
                {center.x, center.y - 61.0f},
                ccc3(145, 255, 125),
                "semibold"
            );
            detail->limitLabelWidth(134.0f, 0.20f, 0.14f);
            m_root->addChild(detail, 6);
        } else {
            auto* lpLabel = makeLabel(
                combineTierAndScore
                    ? fmt::format("{} - {} LP", upper(tier), profile.visibleRankedScore.value_or(0))
                    : fmt::format("{} LP", profile.visibleRankedScore.value_or(0)),
                0.35f,
                {center.x, center.y - 47.0f},
                ccc3(240, 244, 250),
                "black"
            );
            lpLabel->limitLabelWidth(124.0f, 0.35f, 0.22f);
            m_root->addChild(lpLabel, 4);
            if (outRefs) outRefs->scoreLabel = lpLabel;
            if (profile.demotionWarning) {
                auto* warning = makeLabel(tr("demotion-warning", "DEMOTION WARNING"), 0.20f, {center.x, center.y - 62.0f}, kRed, "black");
                warning->limitLabelWidth(134.0f, 0.20f, 0.14f);
                m_root->addChild(warning, 6);
            }
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
        if (resourceView.state == RankedResourceDownloadState::Downloading) {
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
        titleLabel->limitLabelWidth(size.width - 150.0f, 0.52f, 0.30f);
        m_root->addChild(titleLabel, 4);
        auto* subLabel = makeLabel(subtitle, 0.27f, {size.width / 2.0f, size.height / 2.0f + 47.0f}, ccc3(210, 220, 238), "semibold");
        subLabel->limitLabelWidth(size.width - 150.0f, 0.27f, 0.18f);
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

        auto const barWidth = std::min(270.0f, size.width - 180.0f);
        auto* barBack = CCLayerColor::create({9, 17, 32, 235});
        barBack->setContentSize({barWidth, 10.0f});
        barBack->setPosition({size.width / 2.0f - barWidth / 2.0f, size.height / 2.0f - 16.0f});
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
                {size.width / 2.0f, size.height / 2.0f - 40.0f},
                kGold
            );
            m_resourceProgressLabel = progressLabel;
            m_root->addChild(progressLabel, 4);
        } else if (resourceView.state == RankedResourceDownloadState::Ready) {
            addButton(tr("continue", "CONTINUE"), {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onContinueResources), false, 0.72f);
        } else if (resourceView.state == RankedResourceDownloadState::Failed) {
            addButton(tr("retry-failed", "RETRY FAILED"), {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), true, 0.68f);
        } else {
            addButton(tr("download-all", "DOWNLOAD ALL"), {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), false, 0.72f);
        }
    }

    void renderPlacementPoolSelection(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        addLanguageSelector();
        addScreenHeader(
            tr("placement-matches", "PLACEMENT"),
            tr("select-starting-pool", "SELECT YOUR STARTING POOL"),
            kGold,
            kCyan
        );

        auto* explanation = makeLabel(
            tr("pool-choice-explanation", "THIS CHOICE ONLY SETS STAGE 1. YOUR RESULTS DETERMINE THE FINAL RANK."),
            0.23f,
            {size.width / 2.0f, size.height - 78.0f},
            corum::ranked::ui::TextMuted,
            "semibold"
        );
        explanation->limitLabelWidth(size.width - 74.0f, 0.25f, 0.17f);
        m_root->addChild(explanation, 5);

        auto* panel = makeNeonPanel(
            {std::min(430.0f, size.width - 60.0f), 174.0f},
            {size.width / 2.0f, size.height / 2.0f + 1.0f},
            kBlue,
            kDeepPanel,
            244
        );
        m_root->addChild(panel, 1);

        constexpr float spacing = 124.0f;
        for (int pool = 1; pool <= 6; ++pool) {
            auto const row = (pool - 1) / 3;
            auto const column = (pool - 1) % 3;
            auto const x = size.width / 2.0f + (static_cast<float>(column) - 1.0f) * spacing;
            auto const y = size.height / 2.0f + 45.0f - static_cast<float>(row) * 76.0f;
            auto* button = addButton(
                fmt::format("POOL {}", pool),
                {x, y},
                menu_selector(CorumRankedLayer::onStartPlacement),
                false,
                0.57f
            );
            button->setTag(pool);

            auto const reference = std::find_if(
                view.placement.referenceMaps.begin(),
                view.placement.referenceMaps.end(),
                [pool](RankedMapView const& map) { return map.pool == pool; }
            );
            auto const referenceText = reference == view.placement.referenceMaps.end()
                ? tr("reference-loading", "REFERENCE MAP LOADING...")
                : fmt::format("{} · {} · ID {}", reference->title, reference->difficulty, reference->levelId);
            auto* referenceLabel = makeLabel(
                referenceText,
                0.18f,
                {x, y - 21.0f},
                corum::ranked::ui::TextMuted,
                "semibold"
            );
            referenceLabel->limitLabelWidth(112.0f, 0.18f, 0.12f);
            m_root->addChild(referenceLabel, 5);
        }

        addButton(
            tr("skip-placement", "SKIP PLACEMENT"),
            {size.width / 2.0f, 22.0f},
            menu_selector(CorumRankedLayer::onOpenPlacementSkip),
            true,
            0.55f
        );

        auto detail = view.error.empty() ? view.status : view.error;
        if (!detail.empty() && !view.error.empty()) {
            auto* status = makeLabel(
                shorten(detail, 96),
                0.25f,
                {size.width / 2.0f, 7.0f},
                view.error.empty() ? corum::ranked::ui::TextMuted : kRed,
                "semibold"
            );
            status->limitLabelWidth(size.width - 90.0f, 0.25f, 0.16f);
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
        warning->limitLabelWidth(330.0f, 0.29f, 0.18f);
        m_root->addChild(warning, 5);
        auto* result = makeLabel(
            tr("skip-placement-result", "YOU WILL BE PLACED AT RED - 150 LP."),
            0.38f,
            {size.width / 2.0f, size.height / 2.0f + 4.0f},
            kRed,
            "black"
        );
        result->limitLabelWidth(320.0f, 0.38f, 0.22f);
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
        historyVisual->setScale(0.78f);
        auto* history = CCMenuItemSpriteExtra::create(historyVisual, this, menu_selector(CorumRankedLayer::onHistory));
        history->setPosition({size.width - 60.0f, size.height - 28.0f});
        m_menu->addChild(history);

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
        tier->limitLabelWidth(150.0f, 0.54f, 0.32f);
        m_root->addChild(tier, 4);
        auto const placement = profile.placementGames < profile.placementGamesRequired;
        if (placement) {
            auto const completedStages = std::max(
                profile.placementGames,
                static_cast<int>(view.placement.stageResults.size())
            );
            addPlacementProgress(completedStages, std::max(1, profile.placementGamesRequired), {leftCenter.x, leftCenter.y - 51.0f}, 16.0f, 0.30f);
        } else if (profile.promotion.active) {
            addRatingMarks(profile.promotion.results, profile.promotion.gamesRequired, {leftCenter.x, leftCenter.y - 49.0f}, 16.0f, 0.30f);
        } else {
            auto* rating = makeLabel(fmt::format("{} LP", view.profileScore), 0.46f, {leftCenter.x, leftCenter.y - 53.0f}, ccc3(240, 244, 250), "black");
            rating->limitLabelWidth(150.0f, 0.46f, 0.28f);
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
        auto* profileHint = makeLabel(profileHintText, 0.25f, {leftCenter.x, leftCenter.y - 79.0f}, profileHintColor, "semibold");
        profileHint->limitLabelWidth(168.0f, 0.25f, 0.17f);
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

        auto detail = view.error.empty() ? view.status : view.error;
        if (!m_localMessage.empty()) detail = m_localMessage;
        if (!detail.empty()) {
            auto* statusPanel = makeNeonPanel({std::min(380.0f, size.width - 84.0f), 34.0f}, {size.width / 2.0f, 27.0f}, view.error.empty() ? kBlue : kRed, kDeepPanel2, 238);
            m_root->addChild(statusPanel, 1);
            auto* status = makeLabel(shorten(detail, 90), 0.28f, {size.width / 2.0f, 27.0f}, view.error.empty() ? ccc3(220, 228, 244) : kRed, "semibold");
            status->limitLabelWidth(size.width - 120.0f, 0.28f, 0.18f);
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
            map->limitLabelWidth(278.0f, 0.42f, 0.25f);
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

        std::string stateText = tr("opening-exact-level", "OPENING EXACT GD LEVEL...");
        if (match.state == "MAP_PLAYING") stateText = tr("placement-map-live", "MAP LIVE · SCORE + GRADE UPDATE IN REAL TIME");
        if (match.state == "MAP_SETTLING") stateText = tr("final-attempt-progress", "FINAL ATTEMPT IN PROGRESS");
        auto* state = makeLabel(
            stateText,
            0.25f,
            {size.width / 2.0f, 34.0f},
            match.state == "MAP_SETTLING" ? kGold : kCyan,
            "semibold"
        );
        state->limitLabelWidth(size.width - 90.0f, 0.25f, 0.16f);
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
            title->limitLabelWidth(76.0f, 0.20f, 0.14f);
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
            score->limitLabelWidth(76.0f, 0.18f, 0.13f);
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
            auto* group = CCNode::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kBlue, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kCyan, "semibold");
            title->limitLabelWidth(76.0f, 0.20f, 0.14f);
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
            score->limitLabelWidth(76.0f, 0.18f, 0.13f);
            group->addChild(score, 4);
            m_root->addChild(group, 5);
            return group;
        };

        auto const center = size.width / 2.0f;
        auto* left = makeGradeCard("MAP 1", map1Grade, map1Score, center - 112.0f, false);
        auto* right = makeGradeCard("MAP 2", map2Grade, map2Score, center, false);
        auto* finalGroup = makeGradeCard(tr("stage-grade", "STAGE GRADE"), result.stageGrade, result.stageScore, center + 112.0f, true);
        m_root->addChild(makeLabel("+", 0.48f, {center - 56.0f, size.height / 2.0f + 4.0f}, kGold, "black"), 6);
        m_root->addChild(makeLabel(">", 0.46f, {center + 56.0f, size.height / 2.0f + 4.0f}, kCyan, "black"), 6);

        left->runAction(CCSequence::create(
            CCDelayTime::create(0.16f),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {2.0f, 0.0f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {-2.0f, 0.0f})),
            nullptr
        ));
        right->runAction(CCSequence::create(
            CCDelayTime::create(0.16f),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {-2.0f, 0.0f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {2.0f, 0.0f})),
            nullptr
        ));
        finalGroup->setScale(0.84f);
        finalGroup->runAction(CCSequence::create(
            CCDelayTime::create(0.44f),
            CCEaseSineOut::create(CCScaleTo::create(0.14f, 1.08f)),
            CCEaseSineInOut::create(CCScaleTo::create(0.12f, 1.0f)),
            nullptr
        ));
        if (m_stageResultAudioPlayedKey != audioKey) {
            finalGroup->runAction(CCSequence::create(
                CCDelayTime::create(0.44f),
                CallFuncExt::create([this, audioKey, grade = result.stageGrade] {
                    m_stageResultAudioPlayedKey = audioKey;
                    RankedAudioManager::get().setMode(
                        grade == "S" || grade == "A" || grade == "B"
                            ? RankedAudioMode::ResultWin
                            : RankedAudioMode::ResultLose
                    );
                }),
                nullptr
            ));
        }

        auto const finalStage = result.stageIndex >= view.placement.totalStages;
        addPlacementProgress(result.stageIndex, view.placement.totalStages, {center, 67.0f}, 18.0f, 0.27f);
        auto* movement = makeLabel(
            finalStage
                ? tr("all-placement-maps-complete", "ALL 10 MAPS COMPLETE")
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
            CCDelayTime::create(0.76f),
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
                CCDelayTime::create(0.76f),
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
            finalResult.skipped ? tr("placement-skipped", "PLACEMENT SKIPPED") : tr("ten-maps-five-stages", "10 MAPS · 5 STAGES"),
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
        tier->limitLabelWidth(size.width - 100.0f, 0.55f, 0.32f);
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
            error->limitLabelWidth(size.width - 100.0f, 0.22f, 0.15f);
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
        if (match.state == "ROUND_PREPARE" || match.state == "DEATHMATCH_PREPARE") {
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
                match.state == "ROUND_SETTLING"
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
        addPlayerCard({100.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerAProfile, ownA);
        addPlayerCard({size.width - 100.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBProfile, !ownA);

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
        hint->limitLabelWidth(176.0f, 0.24f, 0.16f);
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
            map->limitLabelWidth(cardWidth - 12.0f, 0.31f, 0.20f);
            m_root->addChild(map, 4);

            auto* difficultyPlate = makeNeonPanel({cardWidth - 22.0f, 28.0f}, {x, centerY - 20.0f}, diffColor, {11, 20, 36}, 245);
            m_root->addChild(difficultyPlate, 2);
            auto* diffCaption = makeLabel(tr("difficulty", "DIFFICULTY"), 0.23f, {x, centerY - 14.0f}, kCyan, "semibold");
            m_root->addChild(diffCaption, 4);
            auto* diff = makeLabel(candidate.difficulty, 0.35f, {x, centerY - 27.0f}, diffColor, "black");
            diff->limitLabelWidth(cardWidth - 28.0f, 0.35f, 0.22f);
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
        privacy->limitLabelWidth(236.0f, 0.25f, 0.16f);
        m_root->addChild(privacy, 4);
    }

    void renderPrepare(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto const deathmatch = match.deathmatchSequence > 0;

        // A/B cards keep the screen symmetric while the centre column is reserved
        // for match state, map information and resource readiness.
        addPlayerCard({96.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerAProfile, ownA);
        addPlayerCard({size.width - 96.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBProfile, !ownA);

        auto* title = makeLabel(
            deathmatch ? tr("death-match", "DEATH MATCH") : fmt::format("ROUND {}", match.roundNumber),
            deathmatch ? 0.62f : 0.56f,
            {size.width / 2.0f, size.height - 27.0f},
            ccc3(245, 248, 255),
            "black"
        );
        title->limitLabelWidth(size.width - 180.0f, deathmatch ? 0.62f : 0.56f, 0.34f);
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
            mapName->limitLabelWidth(154.0f, 0.40f, 0.24f);
            m_root->addChild(mapName, 6);

            auto const diffColor = difficultyColor(match.currentMap->difficulty);
            auto* diffPlate = makeNeonPanel({74.0f, 32.0f}, {size.width / 2.0f, mapY - 27.0f}, diffColor, {12, 17, 27}, 246);
            m_root->addChild(diffPlate, 2);
            auto* difficulty = makeLabel(match.currentMap->difficulty, 0.42f, {size.width / 2.0f, mapY - 27.0f}, diffColor, "black");
            difficulty->limitLabelWidth(62.0f, 0.42f, 0.25f);
            m_root->addChild(difficulty, 6);
        }

        auto* local = findPlayableMap();
        auto const mapReady = local != nullptr;
        auto const songReady = mapReady && isSongReady(local);
        auto& runtime = RankedRuntime::get();
        auto const clearSyncPending =
            !deathmatch &&
            runtime.localDisplayClears() >= 3 &&
            runtime.hasLocalAttemptInFlight();
        auto* infoPlate = makeNeonPanel({154.0f, 118.0f}, {size.width / 2.0f, size.height / 2.0f - 36.0f}, kBlue, kDeepPanel2, 224);
        m_root->addChild(infoPlate, 1);

        auto const mapPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 14.0f};
        auto const songPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 42.0f};
        if (clearSyncPending) {
            auto* plate = makeTextPlate("01  " + tr("clear-recorded", "CLEAR RECORDED"), {116.0f, 23.0f}, mapPos, kGreen, 0.24f, kGreen, "semibold");
            m_root->addChild(plate, 3);
        } else if (mapReady) {
            auto* plate = makeTextPlate("01  " + tr("map-ready", "MAP READY"), {116.0f, 23.0f}, mapPos, kGreen, 0.25f, kGreen, "semibold");
            m_root->addChild(plate, 3);
        } else {
            auto* plate = makeTextPlate("01  " + tr("map-loading", "MAP LOADING"), {116.0f, 23.0f}, mapPos, kCyan, 0.25f, ccc3(235, 242, 255), "semibold");
            m_root->addChild(plate, 3);
        }

        if (clearSyncPending) {
            auto* plate = makeTextPlate("02  " + tr("result-sync", "RESULT SYNC"), {116.0f, 23.0f}, songPos, kCyan, 0.24f, ccc3(218, 228, 244), "semibold");
            m_root->addChild(plate, 3);
        } else if (!mapReady) {
            auto* plate = makeTextPlate("02  " + tr("waiting", "WAITING"), {116.0f, 23.0f}, songPos, kBlue, 0.25f, ccc3(218, 228, 244), "semibold");
            m_root->addChild(plate, 3);
        } else if (songReady) {
            auto* plate = makeTextPlate("02  " + tr("audio-ready", "AUDIO READY"), {116.0f, 23.0f}, songPos, kGreen, 0.25f, kGreen, "semibold");
            m_root->addChild(plate, 3);
        } else if (RankedRuntime::get().songBypassAllowed()) {
            auto* plate = makeTextPlate("02  " + tr("start-muted", "START MUTED"), {116.0f, 23.0f}, songPos, kGold, 0.24f, kGold, "semibold");
            m_root->addChild(plate, 3);
        } else {
            // The real click target remains the vanilla song button on the level
            // info page. This button only opens that gated page.
            addButton(tr("download-song", "DOWNLOAD SONG"), songPos, menu_selector(CorumRankedLayer::onDownloadSong), false, 0.48f);
        }

        auto const loadStageY = size.height / 2.0f - 66.0f;
        m_prepareLoadStageLabel = makeLabel(
            preparationStageText(),
            0.18f,
            {size.width / 2.0f, loadStageY},
            m_levelLookupError.empty() ? kCyan : kGold,
            "semibold"
        );
        m_prepareLoadStageLabel->limitLabelWidth(138.0f, 0.18f, 0.12f);
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
            if (!ownReady) footer = tr("waiting-download", "WAITING FOR YOUR DOWNLOAD");
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
        title->limitLabelWidth(size.width - 70.0f, 0.48f, 0.30f);
        m_root->addChild(title, 5);
        auto* subtitle = makeLabel(tr("waiting-opponent", "WAITING FOR OPPONENT"), 0.24f, {size.width / 2.0f, size.height - 59.0f}, kCyan);
        m_root->addChild(subtitle, 5);

        auto* panel = makeNeonPanel({246.0f, 142.0f}, {size.width / 2.0f, size.height / 2.0f - 4.0f}, kBlue, kDeepPanel, 238);
        m_root->addChild(panel, 1);

        auto* name = makeLabel(upper(shorten(opponent, 18)), 0.32f, {size.width / 2.0f, size.height / 2.0f + 38.0f});
        name->limitLabelWidth(205.0f, 0.32f, 0.21f);
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
        score->limitLabelWidth(225.0f, 0.22f, 0.16f);
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
        hint->limitLabelWidth(size.width - 80.0f, 0.24f, 0.16f);
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
        title->limitLabelWidth(size.width - 90.0f, 0.62f, 0.34f);
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
        previousScore->limitLabelWidth(118.0f, 0.27f, 0.16f);
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
        prior->limitLabelWidth(118.0f, 0.27f, 0.16f);
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
                if (m_menu) m_menu->setTouchEnabled(true);
            }
        );
        if (!transitionLayer) return false;

        m_rankTransitionLayer = transitionLayer;
        if (m_menu) {
            if (m_menu->m_pSelectedItem) m_menu->m_pSelectedItem = nullptr;
            m_menu->setTouchEnabled(false);
        }
        setKeypadEnabled(false);
        addChild(transitionLayer, 1000);
        return true;
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
            reason->limitLabelWidth(size.width - 100.0f, 0.28f, 0.18f);
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
        addPlayerCard({100.0f, cardY}, match.playerAName, match.profileAfterA, ownA, &cardA, true);
        addPlayerCard({size.width - 100.0f, cardY}, match.playerBName, match.profileAfterB, !ownA, &cardB, true);
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
            row->limitLabelWidth(size.width - 130.0f, 0.27f, 0.17f);
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
            row->limitLabelWidth(size.width - 190.0f, 0.30f, 0.20f);
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

    GJGameLevel* findLocalMap() const {
        auto const id = RankedRuntime::get().currentLevelId();
        if (id <= 0) return nullptr;
        auto* manager = GameLevelManager::sharedState();
        return manager ? manager->getSavedLevel(id) : nullptr;
    }

    bool isPlayableLevel(GJGameLevel* level) const {
        if (!level) return false;
        if (static_cast<int>(level->m_levelID) != RankedRuntime::get().currentLevelId()) return false;
        // getSavedLevel() may return only list metadata. PlayLayer needs the actual
        // downloaded level string; entering on metadata alone is what caused the
        // repeated "Load Failed" screen in alpha.11.
        return !level->m_levelString.empty() && !level->m_levelNotDownloaded;
    }

    GJGameLevel* findPlayableMap() const {
        auto* level = findLocalMap();
        return isPlayableLevel(level) ? level : nullptr;
    }

    void releaseResolvedLevel() {
        if (m_resolvedLevel) {
            m_resolvedLevel->release();
            m_resolvedLevel = nullptr;
        }
        m_resolvedLevelContext.clear();
    }

    bool levelLookupKeyMatches(char const* key) const {
        return
            m_levelLookupBusy &&
            key &&
            !m_levelLookupKey.empty() &&
            m_levelLookupKey == key;
    }

    void finishExactLevelLookup() {
        if (m_levelLookupBusy || m_previousLevelManagerDelegate) {
            auto* manager = GameLevelManager::sharedState();
            if (manager && manager->m_levelManagerDelegate == this) {
                manager->m_levelManagerDelegate = m_previousLevelManagerDelegate;
            }
        }
        m_previousLevelManagerDelegate = nullptr;
        m_levelLookupBusy = false;
        m_levelLookupKey.clear();
        m_levelLookupContext.clear();
        m_levelLookupTargetId = 0;
    }

    void cancelExactLevelLookup() {
        finishExactLevelLookup();
    }

    bool lookupStillTargetsCurrentMap(std::string const& context, int levelId) const {
        auto const& view = RankedRuntime::get().view();
        return
            view.stage == RuntimeStage::Matched &&
            levelId > 0 &&
            RankedRuntime::get().currentLevelId() == levelId &&
            resourceKey(view.match) == context;
    }

    void acceptExactLevelResults(CCArray* levels, char const* key) {
        if (!levelLookupKeyMatches(key)) return;

        auto const context = m_levelLookupContext;
        auto const targetId = m_levelLookupTargetId;
        GJGameLevel* exactLevel = nullptr;
        if (levels) {
            for (unsigned int index = 0; index < levels->count(); ++index) {
                auto* candidate = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(index));
                if (candidate && static_cast<int>(candidate->m_levelID) == targetId) {
                    exactLevel = candidate;
                    break;
                }
            }
        }

        auto const stillCurrent = lookupStillTargetsCurrentMap(context, targetId);
        finishExactLevelLookup();
        if (!stillCurrent) return;

        if (!exactLevel) {
            m_levelLookupError = tr("exact-level-not-found", "EXACT GD LEVEL NOT FOUND - RETRYING");
            m_levelLookupNextAttemptAt = SteadyClock::now() + std::chrono::milliseconds(1400);
            return;
        }

        exactLevel->retain();
        releaseResolvedLevel();
        m_resolvedLevel = exactLevel;
        m_resolvedLevelContext = context;
        m_levelLookupError.clear();
        m_prepareLoadVisual = std::max(m_prepareLoadVisual, 0.30f);
        log::info(
            "Resolved exact Ranked GD level: match={} round={} level={} name={}",
            RankedRuntime::get().view().match.matchId,
            RankedRuntime::get().view().match.roundNumber,
            targetId,
            std::string(exactLevel->m_levelName.c_str())
        );
    }

    void rejectExactLevelResults(char const* key) {
        if (!levelLookupKeyMatches(key)) return;
        auto const context = m_levelLookupContext;
        auto const targetId = m_levelLookupTargetId;
        auto const stillCurrent = lookupStillTargetsCurrentMap(context, targetId);
        finishExactLevelLookup();
        if (!stillCurrent) return;
        m_levelLookupError = tr("level-lookup-failed", "LEVEL LOOKUP FAILED - RETRYING");
        auto const backoffMilliseconds = std::min(4000, 900 + m_levelLookupAttempts * 450);
        m_levelLookupNextAttemptAt = SteadyClock::now() + std::chrono::milliseconds(backoffMilliseconds);
    }

    void loadLevelsFinished(CCArray* levels, char const* key) override {
        if (levelLookupKeyMatches(key)) {
            acceptExactLevelResults(levels, key);
            return;
        }
        if (m_previousLevelManagerDelegate && m_previousLevelManagerDelegate != this) {
            m_previousLevelManagerDelegate->loadLevelsFinished(levels, key);
        }
    }

    void loadLevelsFailed(char const* key) override {
        if (levelLookupKeyMatches(key)) {
            rejectExactLevelResults(key);
            return;
        }
        if (m_previousLevelManagerDelegate && m_previousLevelManagerDelegate != this) {
            m_previousLevelManagerDelegate->loadLevelsFailed(key);
        }
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int type) override {
        if (levelLookupKeyMatches(key)) {
            acceptExactLevelResults(levels, key);
            return;
        }
        if (m_previousLevelManagerDelegate && m_previousLevelManagerDelegate != this) {
            m_previousLevelManagerDelegate->loadLevelsFinished(levels, key, type);
        }
    }

    void loadLevelsFailed(char const* key, int type) override {
        if (levelLookupKeyMatches(key)) {
            rejectExactLevelResults(key);
            return;
        }
        if (m_previousLevelManagerDelegate && m_previousLevelManagerDelegate != this) {
            m_previousLevelManagerDelegate->loadLevelsFailed(key, type);
        }
    }

    void setupPageInfo(gd::string info, char const* key) override {
        if (levelLookupKeyMatches(key)) return;
        if (m_previousLevelManagerDelegate && m_previousLevelManagerDelegate != this) {
            m_previousLevelManagerDelegate->setupPageInfo(info, key);
        }
    }

    bool exactLevelLookupStateAllowed() const {
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) return false;
        auto const& state = view.match.state;
        return
            state == "ROUND_PREPARE" ||
            state == "DEATHMATCH_PREPARE" ||
            state == "MAP_PREPARE" ||
            state == "ROUND_PLAYING" ||
            state == "DEATHMATCH_PLAYING" ||
            state == "MAP_PLAYING";
    }

    void beginExactLevelLookup() {
        if (!exactLevelLookupStateAllowed() || findLocalMap()) return;
        auto const& match = RankedRuntime::get().view().match;
        auto const targetId = RankedRuntime::get().currentLevelId();
        if (targetId <= 0 || !match.currentMap) return;
        auto const context = resourceKey(match);
        if (
            m_resolvedLevel &&
            m_resolvedLevelContext == context &&
            static_cast<int>(m_resolvedLevel->m_levelID) == targetId
        ) return;

        auto const now = SteadyClock::now();
        if (m_levelLookupBusy) {
            auto const stalled =
                m_levelLookupLastRequestAt != SteadyClock::time_point{} &&
                std::chrono::duration<double>(now - m_levelLookupLastRequestAt).count() >= 8.0;
            if (!stalled) return;

            auto* manager = GameLevelManager::sharedState();
            if (!manager || manager->m_levelManagerDelegate != this) {
                // Another vanilla screen took ownership of the singleton delegate.
                // Do not overwrite that live owner; retry after it releases control.
                m_previousLevelManagerDelegate = nullptr;
                m_levelLookupBusy = false;
                m_levelLookupKey.clear();
                m_levelLookupNextAttemptAt = now + std::chrono::seconds(1);
                return;
            }
            auto* retrySearch = GJSearchObject::create(
                SearchType::Type19,
                fmt::format("{}&gameVersion=22", m_levelLookupTargetId)
            );
            if (!retrySearch) return;
            m_levelLookupKey = retrySearch->getKey() ? retrySearch->getKey() : "";
            m_levelLookupLastRequestAt = now;
            ++m_levelLookupAttempts;
            manager->getOnlineLevels(retrySearch);
            return;
        }
        if (
            m_levelLookupNextAttemptAt != SteadyClock::time_point{} &&
            now < m_levelLookupNextAttemptAt
        ) return;

        auto* manager = GameLevelManager::sharedState();
        auto* search = GJSearchObject::create(
            SearchType::Type19,
            fmt::format("{}&gameVersion=22", targetId)
        );
        if (!manager || !search || !search->getKey()) {
            m_levelLookupError = tr("level-lookup-unavailable", "LEVEL LOOKUP UNAVAILABLE - RETRYING");
            m_levelLookupNextAttemptAt = now + std::chrono::seconds(1);
            return;
        }

        m_previousLevelManagerDelegate = manager->m_levelManagerDelegate == this
            ? nullptr
            : manager->m_levelManagerDelegate;
        manager->m_levelManagerDelegate = this;
        m_levelLookupBusy = true;
        m_levelLookupContext = context;
        m_levelLookupTargetId = targetId;
        m_levelLookupKey = search->getKey();
        m_levelLookupLastRequestAt = now;
        m_levelLookupNextAttemptAt = {};
        m_levelLookupError.clear();
        if (m_levelLookupStartedAt == SteadyClock::time_point{}) m_levelLookupStartedAt = now;
        ++m_levelLookupAttempts;
        log::info(
            "Resolving exact Ranked GD level: match={} round={} level={} attempt={}",
            match.matchId,
            match.roundNumber,
            targetId,
            m_levelLookupAttempts
        );
        manager->getOnlineLevels(search);
    }

    GJGameLevel* findLevelInfoMap() const {
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) return nullptr;
        auto const targetId = RankedRuntime::get().currentLevelId();
        if (
            m_resolvedLevel &&
            m_resolvedLevelContext == resourceKey(view.match) &&
            static_cast<int>(m_resolvedLevel->m_levelID) == targetId
        ) return m_resolvedLevel;
        return findLocalMap();
    }

    float preparationProgressTarget() {
        if (auto* level = findLevelInfoMap()) {
            if (!isPlayableLevel(level)) return 0.30f;
            return isSongReady(level) ? 0.92f : 0.75f;
        }
        if (m_levelLookupStartedAt == SteadyClock::time_point{}) return 0.0f;
        auto const elapsed = std::chrono::duration<double>(
            SteadyClock::now() - m_levelLookupStartedAt
        ).count();
        return std::min(0.29f, 0.035f + static_cast<float>(elapsed) * 0.04f);
    }

    std::string preparationStageText() {
        if (auto* level = findLevelInfoMap()) {
            if (!isPlayableLevel(level)) return tr("exact-level-found", "EXACT GD LEVEL FOUND");
            return isSongReady(level)
                ? tr("preparation-ready", "PREPARATION READY")
                : tr("waiting-song", "WAITING FOR SONG");
        }
        if (m_levelLookupBusy) return tr("finding-exact-level", "FINDING EXACT GD LEVEL...");
        if (!m_levelLookupError.empty()) return m_levelLookupError;
        return tr("waiting-level-lookup", "WAITING FOR LEVEL LOOKUP...");
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
        if (!manager || id <= 0) return true;
        // Resource songs are already part of the game installation. Custom songs
        // must have a downloaded file according to MusicDownloadManager.
        return manager->isResourceSong(id) || manager->isSongDownloaded(id);
    }

    bool isSongReady(GJGameLevel* level) {
        if (!level) return false;
        refreshSongIds(level);
        auto* manager = MusicDownloadManager::sharedState();
        if (!manager) return m_songIds.empty();
        for (auto const id : m_songIds) {
            if (!songIdReady(manager, id)) return false;
        }
        return true;
    }

    void openSongDownloadGate(double countdownOverride = -1.0) {
        if (m_enteringSongGate) return;
        auto* level = findLevelInfoMap();
        if (!level) {
            beginExactLevelLookup();
            return;
        }

        // The real Geometry Dash LevelInfo page owns map/song acquisition. The
        // explicit override is used as a recovery path when polling skipped a
        // very short PREPARE phase (notably Debug Trigger Death Match).
        auto const authoritativeRemaining = RankedRuntime::get().deadlineMillis();
        auto countdownRemaining = countdownOverride >= 0.0
            ? countdownOverride
            : authoritativeRemaining
                ? std::max(0.0, static_cast<double>(*authoritativeRemaining) / 1000.0)
                : std::max(0.0, 10.0 - phaseSeconds());
        if (!RankedRuntime::get().isPlacement()) {
            countdownRemaining = std::min(countdownRemaining, 10.0);
        }
        auto const usedResolvedLevel = level == m_resolvedLevel;
        m_enteringSongGate = corum::ranked::showRankedSongDownloadGate(level, countdownRemaining);
        if (m_enteringSongGate) {
            m_rankedChildScenePushed = true;
            if (usedResolvedLevel) releaseResolvedLevel();
        }
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
        if (
            match.state != "ROUND_PREPARE" &&
            match.state != "DEATHMATCH_PREPARE" &&
            match.state != "MAP_PREPARE"
        ) return;

        // Resolve the exact Geometry Dash level as soon as the authoritative
        // ten-second prepare phase begins. LevelInfo starts its vanilla map/update
        // work behind the Ranked cover immediately; the real page is revealed at
        // five seconds remaining instead of spending the first half idle.
        openSongDownloadGate();
    }

    void maybeEnterLevel() {
        auto const& view = RankedRuntime::get().view();
        if (m_enteringLevel || view.stage != RuntimeStage::Matched) return;
        auto const& state = view.match.state;
        if (
            state != "ROUND_PLAYING" &&
            state != "DEATHMATCH_PLAYING" &&
            state != "MAP_PLAYING"
        ) return;
        if (!RankedRuntime::get().canEnterCurrentLevel()) {
            if (state == "DEATHMATCH_PLAYING") m_localMessage = "All 3 attempts used. Waiting for the opponent...";
            return;
        }
        auto* level = findPlayableMap();
        if (!level) {
            // A Debug scenario (or a fast server transition) can move PREPARE ->
            // PLAYING between client polls. Recover by opening the same vanilla
            // LevelInfo download gate even though PLAYING is already authoritative.
            // LevelInfoLayer will download the map and auto-enter as soon as it is
            // playable; this prevents the old permanent "Loading map" deadlock.
            m_localMessage = "Opening level download recovery...";
            openSongDownloadGate(0.0);
            return;
        }
        // Do not construct PlayLayer directly. Enter through Geometry Dash's normal
        // LevelInfoLayer -> onPlay path; this lets the game own level validation,
        // loading transitions, audio setup, and the return/quit stack.
        auto* scene = LevelInfoLayer::scene(level, false);
        if (!scene) {
            m_localMessage = "Unable to open the selected level. Retrying...";
            return;
        }
        m_enteringLevel = true;
        m_rankedChildScenePushed = true;
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.25f, scene));
    }

    void onDownloadAllResources(CCObject*) {
        m_localMessage.clear();
        RankedAudioManager::get().downloadAll();
    }

    void onContinueResources(CCObject*) {
        if (!RankedAudioManager::get().resourcesReady()) return;
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
        m_page = Page::PlacementPoolSelection;
    }

    void onStartPlacement(CCObject* sender) {
        auto const pool = sender ? sender->getTag() : 0;
        if (pool < 1 || pool > 6) return;
        m_localMessage.clear();
        m_page = Page::Live;
        RankedRuntime::get().startPlacement(pool);
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

    void onBan(CCObject* sender) {
        if (!m_selectedBan.empty() || !m_pendingBan.empty()) return;
        auto const index = sender ? sender->getTag() : -1;
        auto const& maps = RankedRuntime::get().view().match.candidateMaps;
        if (index < 0 || static_cast<std::size_t>(index) >= maps.size()) return;
        m_pendingBan = maps[static_cast<std::size_t>(index)].canonicalLevelId;
        RankedRuntime::get().submitBan(m_pendingBan);
    }

    void onDownloadMap(CCObject*) {
        openSongDownloadGate();
    }

    void onDownloadSong(CCObject*) {
        openSongDownloadGate();
    }

    void onQueueAgain(CCObject*) {
        m_page = Page::Live;
        RankedRuntime::get().queueAgain();
    }

    void onResultClose(CCObject*) {
        RankedRuntime::get().dismissMatch();
        m_page = Page::Live;
    }

    void onHistory(CCObject*) {
        m_page = Page::HistoryList;
        RankedRuntime::get().fetchHistory();
    }

    void onHistoryDetail(CCObject* sender) {
        auto const index = sender ? sender->getTag() : -1;
        if (index < 0) return;
        m_historyIndex = static_cast<std::size_t>(index);
        m_page = Page::HistoryDetail;
    }

    void onHistoryBack(CCObject*) {
        if (m_page == Page::PlacementSkipConfirm) {
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
        setKeypadEnabled(false);
        if (m_menu) {
            if (m_menu->m_pSelectedItem) {
                m_menu->m_pSelectedItem = nullptr;
            }
            m_menu->setTouchEnabled(false);
        }
        // Ranked now lives in its own GD-style scene/tab. Return to the previous
        // Geometry Dash scene instead of merely deleting the fullscreen child.
        CCDirector::sharedDirector()->popScene();
    }

    void onExit() override {
        unschedule(schedule_selector(CorumRankedLayer::refresh));
        cancelExactLevelLookup();
        m_rankTransitionLayer = nullptr;
        setKeypadEnabled(false);
        if (m_menu) {
            if (m_menu->m_pSelectedItem) {
                m_menu->m_pSelectedItem = nullptr;
            }
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
