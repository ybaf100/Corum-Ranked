#include "RankedPopup.hpp"

#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "DebugBotPopup.hpp"
#include "RankedSongGate.hpp"

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
using corum::ranked::RoundSummaryView;
using corum::ranked::RuntimeStage;
using corum::ranked::RuntimeView;

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr ccColor3B kPanelColor = {18, 24, 40};
constexpr ccColor3B kPanelLight = {33, 43, 67};
constexpr ccColor3B kAccent = {100, 200, 255};
constexpr ccColor3B kGreen = {92, 236, 133};
constexpr ccColor3B kRed = {255, 104, 104};
constexpr ccColor3B kGold = {255, 214, 90};
constexpr ccColor3B kCyan = {74, 226, 255};
constexpr ccColor3B kBlue = {73, 147, 255};
constexpr ccColor3B kDeepPanel = {6, 13, 28};
constexpr ccColor3B kDeepPanel2 = {10, 20, 38};
constexpr ccColor3B kSideRed = {255, 92, 102};

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
    if (tier == "RED") return {240, 78, 78};
    if (tier == "AQUA") return {86, 224, 231};
    if (tier == "BRONZE") return {194, 125, 63};
    if (tier == "SILVER") return {205, 210, 225};
    if (tier == "GOLD") return {255, 208, 65};
    return {155, 160, 175};
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
    char const* font = "bigFont.fnt"
) {
    auto* label = CCLabelBMFont::create(text.c_str(), font);
    label->setScale(scale);
    label->setColor(color);
    label->setPosition(position);
    return label;
}

CCNode* makePanel(CCSize size, CCPoint position, ccColor3B color = kPanelColor, GLubyte opacity = 235) {
    auto* node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint({0.5f, 0.5f});
    node->setPosition(position);

    auto* fill = CCLayerColor::create({color.r, color.g, color.b, opacity});
    fill->setContentSize(size);
    fill->setPosition(CCPointZero);
    node->addChild(fill, 0);
    return node;
}

CCNode* makeNeonPanel(
    CCSize size,
    CCPoint position,
    ccColor3B accent = kCyan,
    ccColor3B fill = kDeepPanel,
    GLubyte fillOpacity = 245
) {
    auto* node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint({0.5f, 0.5f});
    node->setPosition(position);

    // Do not stretch Geometry Dash sprites as arbitrary Scale9 backgrounds.
    // Some base-game atlases render opaque black source rectangles when scaled,
    // which caused the alpha.24 cards/HUD to cover gameplay. Build the panel out
    // of plain CCLayerColor nodes instead so every platform gets the same bounds.
    auto* shadow = CCLayerColor::create({2, 6, 16, 92});
    shadow->setContentSize({size.width + 4.0f, size.height + 4.0f});
    shadow->setPosition({-2.0f, -4.0f});
    node->addChild(shadow, -1);

    auto* border = CCLayerColor::create({accent.r, accent.g, accent.b, 118});
    border->setContentSize(size);
    border->setPosition(CCPointZero);
    node->addChild(border, 0);

    auto* inner = CCLayerColor::create({fill.r, fill.g, fill.b, fillOpacity});
    inner->setContentSize({std::max(2.0f, size.width - 4.0f), std::max(2.0f, size.height - 4.0f)});
    inner->setPosition({2.0f, 2.0f});
    node->addChild(inner, 1);

    auto* top = CCLayerColor::create({accent.r, accent.g, accent.b, 220});
    top->setContentSize({std::max(4.0f, size.width - 14.0f), 2.0f});
    top->setPosition({7.0f, size.height - 4.0f});
    node->addChild(top, 2);

    auto* bottom = CCLayerColor::create({accent.r, accent.g, accent.b, 90});
    bottom->setContentSize({std::max(4.0f, size.width - 24.0f), 1.0f});
    bottom->setPosition({12.0f, 4.0f});
    node->addChild(bottom, 2);
    return node;
}

CCNode* makeTextPlate(
    std::string const& text,
    CCSize size,
    CCPoint position,
    ccColor3B accent,
    float scale = 0.24f,
    ccColor3B textColor = {255, 255, 255},
    char const* font = "bigFont.fnt"
) {
    auto* node = makeNeonPanel(size, position, accent, kDeepPanel2, 242);
    auto* label = makeLabel(text, scale, {size.width / 2.0f, size.height / 2.0f}, textColor, font);
    label->limitLabelWidth(size.width - 12.0f, scale, std::max(0.12f, scale * 0.65f));
    node->addChild(label, 4);
    return node;
}

char const* buttonBackground(bool danger = false) {
    return danger ? "GJ_button_06.png" : "GJ_button_01.png";
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

class CorumRankedLayer final : public CCLayerColor {
    enum class Page {
        Live,
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
    bool m_resourceGateInitialized = false;
    bool m_resourceGateAcknowledged = false;
    bool m_resourceGateWasMissing = false;
    bool m_resourceGateMenuRestored = false;
    std::string m_renderedUiKey;
    std::string m_pendingUiKey;
    bool m_uiFadingOut = false;
    bool m_fadeInNextRender = true;
    SteadyClock::time_point m_uiTransitionDeadline {};

    ~CorumRankedLayer() override = default;

    bool init() override {
        if (!CCLayerColor::initWithColor({0, 0, 0, 0})) return false;
        setID("corum-ranked-fullscreen"_spr);
        setKeypadEnabled(true);

        auto const winSize = CCDirector::sharedDirector()->getWinSize();
        if (auto* background = CCSprite::create("GJ_gradientBG.png")) {
            background->setAnchorPoint({0.5f, 0.5f});
            background->setPosition({winSize.width / 2.0f, winSize.height / 2.0f});
            auto const content = background->getContentSize();
            if (content.width > 0.0f) background->setScaleX(winSize.width / content.width);
            if (content.height > 0.0f) background->setScaleY(winSize.height / content.height);
            background->setColor(ccc3(5, 67, 180));
            addChild(background, 0);
        }
        if (auto* shade = CCLayerColor::create({2, 8, 24, 112})) {
            shade->setContentSize(winSize);
            addChild(shade, 0);
        }

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
        schedule(schedule_selector(CorumRankedLayer::refresh), 0.20f);
        return true;
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
        if (m_page == Page::HistoryList) return "history:list";
        if (m_page == Page::HistoryDetail) return "history:detail";
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched) {
            return fmt::format("stage:{}", static_cast<int>(view.stage));
        }
        return fmt::format(
            "match:{}:{}:{}",
            view.match.state,
            view.match.roundNumber,
            view.match.deathmatchSequence
        );
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

        if (key == m_renderedUiKey) return true;
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
        if (view.stage == RuntimeStage::Matched) {
            auto const& state = view.match.state;
            auto const gameplayTransition =
                state == "ROUND_PREPARE" ||
                state == "DEATHMATCH_PREPARE" ||
                state == "ROUND_PLAYING" ||
                state == "FINAL_ATTEMPT_WINDOW" ||
                state == "LAST_ATTEMPT_WINDOW" ||
                state == "DEATHMATCH_PLAYING";
            if ((m_enteringSongGate || m_enteringLevel) && gameplayTransition) return;
        }
        if (m_page != Page::Live || view.stage != RuntimeStage::Matched) {
            audio.setMode(RankedAudioMode::Menu);
            return;
        }
        if (view.match.state == "MATCH_RESULT") {
            audio.setMode(view.match.winnerSide == view.match.side
                ? RankedAudioMode::ResultWin
                : RankedAudioMode::ResultLose);
            return;
        }
        if (view.match.state == "CANCELLED") {
            audio.setMode(RankedAudioMode::Match);
            return;
        }
        audio.setMode(RankedAudioMode::Match);
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
            }

        }
    }

    double phaseSeconds() const {
        if (m_phaseStartedAt == SteadyClock::time_point{}) return 0.0;
        return std::chrono::duration<double>(SteadyClock::now() - m_phaseStartedAt).count();
    }

    void clearUi() {
        m_root->removeAllChildrenWithCleanup(true);
        m_menu->removeAllChildrenWithCleanup(true);
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

    CCMenuItemSpriteExtra* addButton(
        std::string const& text,
        CCPoint position,
        SEL_MenuHandler selector,
        bool danger = false,
        float scale = 0.72f
    ) {
        auto* sprite = ButtonSprite::create(text.c_str(), "bigFont.fnt", buttonBackground(danger), 0.75f);
        sprite->setScale(scale);
        auto* item = CCMenuItemSpriteExtra::create(sprite, this, selector);
        item->setPosition(position);
        m_menu->addChild(item);
        return item;
    }

    void addStatusPill(std::string const& text, CCPoint position, ccColor3B color) {
        auto* panel = makePanel({106.0f, 28.0f}, position, color, 190);
        m_root->addChild(panel, 1);
        auto* label = makeLabel(text, 0.28f, position);
        label->limitLabelWidth(96.0f, 0.28f, 0.18f);
        m_root->addChild(label, 2);
    }

    void addPlayerCard(
        CCPoint center,
        std::string const& name,
        std::string const& tier,
        int score,
        bool self
    ) {
        auto const accent = tierColor(upper(tier));
        auto* panel = makeNeonPanel({150.0f, 184.0f}, center, accent, kDeepPanel, 246);
        m_root->addChild(panel, 1);

        auto* ribbon = makeTextPlate(
            self ? "YOU" : "OPPONENT",
            {74.0f, 18.0f},
            {center.x, center.y + 78.0f},
            accent,
            0.17f,
            self ? kGold : ccc3(220, 228, 242)
        );
        m_root->addChild(ribbon, 2);

        int frame = 1;
        if (self) {
            if (auto* game = GameManager::sharedState()) frame = std::max(1, game->getPlayerFrame());
        }

        auto* iconPlate = makeNeonPanel({66.0f, 66.0f}, {center.x, center.y + 36.0f}, accent, {5, 11, 23}, 248);
        m_root->addChild(iconPlate, 2);
        auto* icon = SimplePlayer::create(frame);
        icon->setScale(1.12f);
        icon->setPosition({center.x, center.y + 36.0f});
        if (self) {
            if (auto* game = GameManager::sharedState()) {
                icon->setColors(game->colorForIdx(game->getPlayerColor()), game->colorForIdx(game->getPlayerColor2()));
            }
        }
        m_root->addChild(icon, 4);

        auto* nameLabel = makeLabel(shorten(upper(name), 15), 0.31f, {center.x, center.y - 6.0f});
        nameLabel->limitLabelWidth(120.0f, 0.31f, 0.22f);
        m_root->addChild(nameLabel, 4);

        auto* lpCaption = makeLabel("RATING", 0.14f, {center.x, center.y - 27.0f}, kCyan);
        m_root->addChild(lpCaption, 4);
        auto* lpLabel = makeLabel(fmt::format("{} LP", score), 0.27f, {center.x, center.y - 43.0f}, ccc3(240, 244, 250), "goldFont.fnt");
        lpLabel->limitLabelWidth(118.0f, 0.27f, 0.18f);
        m_root->addChild(lpLabel, 4);

        auto* tierPlate = makeTextPlate(upper(tier), {96.0f, 24.0f}, {center.x, center.y - 69.0f}, accent, 0.21f, accent, "goldFont.fnt");
        m_root->addChild(tierPlate, 3);
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

        std::string title = "RANKED RESOURCES";
        std::string subtitle = "Additional resources are required.";
        ccColor3B accent = kCyan;
        if (resourceView.state == RankedResourceDownloadState::Downloading) {
            title = "DOWNLOADING RESOURCES";
            subtitle = "Keep this screen open until the download is complete.";
            accent = kGold;
        } else if (resourceView.state == RankedResourceDownloadState::Ready) {
            title = "ALL RESOURCES READY";
            subtitle = "Everything required for Ranked is ready.";
            accent = kGreen;
        } else if (resourceView.state == RankedResourceDownloadState::Failed) {
            title = "SOME RESOURCES FAILED";
            subtitle = "Retry the failed download before continuing.";
            accent = kRed;
        }

        auto* titleLabel = makeLabel(title, 0.52f, {size.width / 2.0f, size.height / 2.0f + 76.0f}, accent, "goldFont.fnt");
        titleLabel->limitLabelWidth(size.width - 150.0f, 0.52f, 0.30f);
        m_root->addChild(titleLabel, 4);
        auto* subLabel = makeLabel(subtitle, 0.20f, {size.width / 2.0f, size.height / 2.0f + 47.0f}, ccc3(210, 220, 238));
        subLabel->limitLabelWidth(size.width - 150.0f, 0.20f, 0.14f);
        m_root->addChild(subLabel, 4);

        auto* count = makeLabel(
            fmt::format("{} / {} READY", resourceView.ready, resourceView.total),
            0.30f,
            {size.width / 2.0f, size.height / 2.0f + 10.0f},
            resourceView.ready == resourceView.total ? kGreen : ccc3(240, 244, 250),
            "goldFont.fnt"
        );
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
        m_root->addChild(barFill, 4);

        if (resourceView.state == RankedResourceDownloadState::Downloading) {
            auto* progressLabel = makeLabel(
                fmt::format("{}%", static_cast<int>(std::round(progress * 100.0))),
                0.22f,
                {size.width / 2.0f, size.height / 2.0f - 40.0f},
                kGold
            );
            m_root->addChild(progressLabel, 4);
        } else if (resourceView.state == RankedResourceDownloadState::Ready) {
            addButton("CONTINUE", {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onContinueResources), false, 0.72f);
        } else if (resourceView.state == RankedResourceDownloadState::Failed) {
            addButton("RETRY FAILED", {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), true, 0.68f);
        } else {
            addButton("DOWNLOAD ALL", {size.width / 2.0f, size.height / 2.0f - 62.0f}, menu_selector(CorumRankedLayer::onDownloadAllResources), false, 0.72f);
        }
    }

    void renderMain(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);

        auto* title = makeLabel("CORUM RANKED", 0.70f, {size.width / 2.0f, size.height - 34.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 4);
        auto* subtitle = makeLabel("COMPETITIVE 1V1 LADDER", 0.19f, {size.width / 2.0f, size.height - 58.0f}, kCyan);
        m_root->addChild(subtitle, 4);

        auto* historySprite = ButtonSprite::create("HISTORY", "bigFont.fnt", "GJ_button_04.png", 0.75f);
        historySprite->setScale(0.56f);
        auto* history = CCMenuItemSpriteExtra::create(historySprite, this, menu_selector(CorumRankedLayer::onHistory));
        history->setPosition({size.width - 60.0f, size.height - 28.0f});
        m_menu->addChild(history);

        auto const leftCenter = CCPoint{size.width / 2.0f - 118.0f, size.height / 2.0f - 2.0f};
        auto const rightCenter = CCPoint{size.width / 2.0f + 118.0f, size.height / 2.0f - 2.0f};

        auto* profilePanel = makeNeonPanel({188.0f, 196.0f}, leftCenter, tierColor(upper(view.profileTier)), kDeepPanel, 246);
        m_root->addChild(profilePanel, 1);
        auto* profileTitle = makeLabel("YOUR RANK", 0.18f, {leftCenter.x, leftCenter.y + 77.0f}, kCyan);
        m_root->addChild(profileTitle, 4);
        auto* emblem = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        emblem->setScale(1.10f);
        emblem->setColor(tierColor(upper(view.profileTier)));
        emblem->setPosition({leftCenter.x, leftCenter.y + 30.0f});
        m_root->addChild(emblem, 4);
        auto* tier = makeLabel(upper(view.profileTier), 0.42f, {leftCenter.x, leftCenter.y - 22.0f}, tierColor(upper(view.profileTier)), "goldFont.fnt");
        tier->limitLabelWidth(150.0f, 0.42f, 0.25f);
        m_root->addChild(tier, 4);
        auto* rating = makeLabel(fmt::format("{} LP", view.profileScore), 0.35f, {leftCenter.x, leftCenter.y - 52.0f}, ccc3(240, 244, 250), "goldFont.fnt");
        rating->limitLabelWidth(150.0f, 0.35f, 0.22f);
        m_root->addChild(rating, 4);
        auto* profileHint = makeLabel("SEASONAL LADDER PROGRESS", 0.15f, {leftCenter.x, leftCenter.y - 78.0f}, ccc3(195, 205, 224));
        m_root->addChild(profileHint, 4);

        auto* actionPanel = makeNeonPanel({188.0f, 196.0f}, rightCenter, kBlue, kDeepPanel, 246);
        m_root->addChild(actionPanel, 1);
        auto* actionTitle = makeLabel("MATCHMAKING", 0.18f, {rightCenter.x, rightCenter.y + 77.0f}, kCyan);
        m_root->addChild(actionTitle, 4);

        if (view.stage == RuntimeStage::Ready) {
            auto* playCircle = CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png");
            playCircle->setScale(0.92f);
            auto* play = CCMenuItemSpriteExtra::create(playCircle, this, menu_selector(CorumRankedLayer::onJoin));
            play->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            m_menu->addChild(play);
            auto* actionText = makeLabel("START QUEUE", 0.33f, {rightCenter.x, rightCenter.y - 40.0f}, kAccent, "goldFont.fnt");
            m_root->addChild(actionText, 4);
            auto* actionHint = makeLabel("FIND A 1V1 MATCH", 0.18f, {rightCenter.x, rightCenter.y - 72.0f}, ccc3(210, 220, 238));
            m_root->addChild(actionHint, 4);
        } else if (view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue) {
            auto* spinner = LoadingSpinner::create(58.0f);
            spinner->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            m_root->addChild(spinner, 4);
            auto* queueing = makeLabel("SEARCHING...", 0.30f, {rightCenter.x, rightCenter.y - 40.0f}, kGold, "goldFont.fnt");
            m_root->addChild(queueing, 4);
            addButton("LEAVE QUEUE", {rightCenter.x, rightCenter.y - 75.0f}, menu_selector(CorumRankedLayer::onLeave), true, 0.56f);
        } else if (view.stage == RuntimeStage::Loading) {
            auto* spinner = LoadingSpinner::create(58.0f);
            spinner->setPosition({rightCenter.x, rightCenter.y + 18.0f});
            m_root->addChild(spinner, 4);
            auto* loading = makeLabel("SYNCING...", 0.30f, {rightCenter.x, rightCenter.y - 40.0f}, kGold, "goldFont.fnt");
            m_root->addChild(loading, 4);
        } else {
            auto* failed = makeLabel("CONNECTION ISSUE", 0.24f, {rightCenter.x, rightCenter.y - 12.0f}, kRed, "goldFont.fnt");
            m_root->addChild(failed, 4);
            addButton("RETRY", {rightCenter.x, rightCenter.y - 58.0f}, menu_selector(CorumRankedLayer::onRetry), false, 0.66f);
        }

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        addButton("DEBUG BOT MATCH", {96.0f, 30.0f}, menu_selector(CorumRankedLayer::onDebugBot), true, 0.48f);
#endif

        auto detail = view.error.empty() ? view.status : view.error;
        if (!m_localMessage.empty()) detail = m_localMessage;
        if (!detail.empty()) {
            auto* statusPanel = makeNeonPanel({std::min(380.0f, size.width - 84.0f), 28.0f}, {size.width / 2.0f, 25.0f}, view.error.empty() ? kBlue : kRed, kDeepPanel2, 238);
            m_root->addChild(statusPanel, 1);
            auto* status = makeLabel(shorten(detail, 90), 0.20f, {size.width / 2.0f, 25.0f}, view.error.empty() ? ccc3(220, 228, 244) : kRed);
            status->limitLabelWidth(size.width - 120.0f, 0.20f, 0.15f);
            m_root->addChild(status, 4);
        }
    }

    void renderMatch(RuntimeView const& view) {
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
        // Once this player has triggered the opponent-only LAST ATTEMPT flow,
        // they are a spectator immediately. Do not render the ordinary round
        // prepare/countdown screen for the 10-second start window; that made a
        // player who had already reached two Clears see a misleading second
        // "STARTS IN 10" screen before spectator mode appeared.
        if (
            (match.state == "LAST_ATTEMPT_WINDOW" || match.state == "ROUND_SETTLING") &&
            match.spectatorActive
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
        addPlayerCard({100.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerATier, match.playerAScore, ownA);
        addPlayerCard({size.width - 100.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBTier, match.playerBScore, !ownA);

        auto* title = makeLabel("MATCH FOUND", 0.66f, {size.width / 2.0f, size.height - 34.0f}, ccc3(246, 249, 255), "goldFont.fnt");
        title->limitLabelWidth(size.width - 170.0f, 0.66f, 0.38f);
        m_root->addChild(title, 5);
        auto* subtitle = makeLabel("PRIVATE BAN PHASE", 0.21f, {size.width / 2.0f, size.height - 60.0f}, kCyan);
        m_root->addChild(subtitle, 5);

        auto const left = std::max(0, 5 - static_cast<int>(std::floor(phaseSeconds())));
        auto* countdownPlate = makeNeonPanel({154.0f, 86.0f}, {size.width / 2.0f, size.height / 2.0f - 2.0f}, left <= 2 ? kSideRed : kBlue, {7, 17, 36}, 248);
        m_root->addChild(countdownPlate, 2);
        auto* phase = makeLabel("MAP BAN IN", 0.23f, {size.width / 2.0f, size.height / 2.0f + 21.0f}, kCyan, "goldFont.fnt");
        m_root->addChild(phase, 5);
        auto* countdown = makeLabel(fmt::format("{}", left), 0.78f, {size.width / 2.0f, size.height / 2.0f - 10.0f}, left <= 2 ? kSideRed : kGold, "goldFont.fnt");
        m_root->addChild(countdown, 5);
        auto* hint = makeLabel("GET READY TO CHOOSE A PRIVATE BAN", 0.16f, {size.width / 2.0f, size.height / 2.0f - 46.0f}, ccc3(210, 219, 236));
        hint->limitLabelWidth(190.0f, 0.16f, 0.12f);
        m_root->addChild(hint, 5);
    }

    void renderBan(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);

        auto* title = makeLabel("MATCH FOUND", 0.64f, {size.width / 2.0f, size.height - 30.0f}, ccc3(244, 248, 255), "goldFont.fnt");
        title->limitLabelWidth(size.width - 150.0f, 0.62f, 0.38f);
        m_root->addChild(title, 6);
        auto* phase = makeLabel("BAN MAP", 0.36f, {size.width / 2.0f, size.height - 60.0f}, kCyan, "goldFont.fnt");
        m_root->addChild(phase, 6);

        auto const remaining = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
        auto* timerPlate = makeNeonPanel({54.0f, 48.0f}, {size.width - 42.0f, size.height - 41.0f}, remaining <= 3 ? kSideRed : kBlue, {8, 18, 40}, 250);
        m_root->addChild(timerPlate, 3);
        auto* timer = makeLabel(fmt::format("{}", remaining), 0.48f, {size.width - 42.0f, size.height - 34.0f}, remaining <= 3 ? kSideRed : ccc3(245, 248, 255), "goldFont.fnt");
        m_root->addChild(timer, 6);
        auto* timerCaption = makeLabel("TIME LEFT", 0.13f, {size.width - 42.0f, size.height - 56.0f}, kCyan);
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
            auto const accent = selected ? kGreen : (pending ? kGold : kBlue);

            auto* panel = makeNeonPanel({cardWidth - 5.0f, cardHeight}, {x, centerY}, accent, kDeepPanel, 248);
            m_root->addChild(panel, 1);

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

            auto* map = makeLabel(twoLineTitle(upper(candidate.title), 10), 0.225f, {x, centerY + 15.0f});
            map->limitLabelWidth(cardWidth - 12.0f, 0.225f, 0.16f);
            m_root->addChild(map, 4);

            auto* difficultyPlate = makeNeonPanel({cardWidth - 22.0f, 28.0f}, {x, centerY - 20.0f}, diffColor, {11, 20, 36}, 245);
            m_root->addChild(difficultyPlate, 2);
            auto* diffCaption = makeLabel("DIFFICULTY", 0.12f, {x, centerY - 14.0f}, kCyan);
            m_root->addChild(diffCaption, 4);
            auto* diff = makeLabel(candidate.difficulty, 0.24f, {x, centerY - 27.0f}, diffColor, "goldFont.fnt");
            diff->limitLabelWidth(cardWidth - 28.0f, 0.24f, 0.16f);
            m_root->addChild(diff, 4);

            auto const actionY = centerY - cardHeight / 2.0f + 21.0f;
            if (selected) {
                auto* state = makeTextPlate("BANNED", {cardWidth - 18.0f, 24.0f}, {x, actionY}, kGreen, 0.18f, kGreen);
                m_root->addChild(state, 3);
            } else if (pending) {
                auto* state = makeTextPlate("BANNING...", {cardWidth - 18.0f, 24.0f}, {x, actionY}, kGold, 0.16f, kGold);
                m_root->addChild(state, 3);
            } else if (m_selectedBan.empty() && m_pendingBan.empty()) {
                auto* button = addButton("BAN", {x, actionY}, menu_selector(CorumRankedLayer::onBan), true, 0.39f);
                button->setTag(static_cast<int>(i));
            }
        }

        auto* privacyPlate = makeNeonPanel({248.0f, 36.0f}, {size.width / 2.0f, 24.0f}, kBlue, {8, 18, 36}, 242);
        m_root->addChild(privacyPlate, 1);
        auto* privacy = makeLabel("YOUR BAN IS PRIVATE  |  REVEALED WHEN THE PHASE ENDS", 0.15f, {size.width / 2.0f, 24.0f}, ccc3(215, 225, 242));
        privacy->limitLabelWidth(232.0f, 0.15f, 0.11f);
        m_root->addChild(privacy, 4);
    }

    void renderPrepare(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        auto const deathmatch = match.deathmatchSequence > 0;

        // A/B cards keep the screen symmetric while the centre column is reserved
        // for match state, map information and resource readiness.
        addPlayerCard({96.0f, size.height / 2.0f - 2.0f}, match.playerAName, match.playerATier, match.playerAScore, ownA);
        addPlayerCard({size.width - 96.0f, size.height / 2.0f - 2.0f}, match.playerBName, match.playerBTier, match.playerBScore, !ownA);

        auto* title = makeLabel(
            deathmatch ? "DEATH MATCH" : fmt::format("ROUND {}", match.roundNumber),
            deathmatch ? 0.62f : 0.56f,
            {size.width / 2.0f, size.height - 27.0f},
            ccc3(245, 248, 255),
            "goldFont.fnt"
        );
        title->limitLabelWidth(size.width - 180.0f, deathmatch ? 0.62f : 0.56f, 0.34f);
        m_root->addChild(title, 6);

        if (deathmatch) {
            auto* sub = makeLabel("3 ATTEMPTS", 0.27f, {size.width / 2.0f, size.height - 52.0f}, kCyan, "goldFont.fnt");
            m_root->addChild(sub, 6);
            auto* attemptsPlate = makeNeonPanel({132.0f, 24.0f}, {size.width / 2.0f, size.height - 72.0f}, kBlue, {8, 18, 38}, 245);
            m_root->addChild(attemptsPlate, 2);
            auto* attempts = makeLabel(
                fmt::format("A  {}/3    |    B  {}/3", match.deathmatchAttemptsUsedA, match.deathmatchAttemptsUsedB),
                0.18f,
                {size.width / 2.0f, size.height - 72.0f},
                ccc3(226, 232, 245)
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
        auto* seriesCaption = makeLabel("SERIES", 0.12f, {size.width / 2.0f, seriesY + 7.0f}, kCyan);
        m_root->addChild(seriesCaption, 6);
        auto* seriesScore = makeLabel(
            fmt::format("{}   :   {}", match.roundWinsA, match.roundWinsB),
            0.25f,
            {size.width / 2.0f, seriesY - 5.0f},
            ccc3(245, 248, 255),
            "goldFont.fnt"
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
            auto* mapName = makeLabel(twoLineTitle(upper(match.currentMap->title), 15), 0.33f, {size.width / 2.0f, mapY + 7.0f});
            mapName->limitLabelWidth(154.0f, 0.33f, 0.20f);
            m_root->addChild(mapName, 6);

            auto const diffColor = difficultyColor(match.currentMap->difficulty);
            auto* diffPlate = makeNeonPanel({74.0f, 32.0f}, {size.width / 2.0f, mapY - 27.0f}, diffColor, {12, 17, 27}, 246);
            m_root->addChild(diffPlate, 2);
            auto* difficulty = makeLabel(match.currentMap->difficulty, 0.33f, {size.width / 2.0f, mapY - 27.0f}, diffColor, "goldFont.fnt");
            difficulty->limitLabelWidth(62.0f, 0.33f, 0.22f);
            m_root->addChild(difficulty, 6);
        }

        auto* local = findPlayableMap();
        auto const mapReady = local != nullptr;
        auto const songReady = mapReady && isSongReady(local);
        auto* infoPlate = makeNeonPanel({168.0f, 124.0f}, {size.width / 2.0f, size.height / 2.0f - 28.0f}, kBlue, kDeepPanel2, 244);
        m_root->addChild(infoPlate, 1);

        auto const mapPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 14.0f};
        auto const songPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 48.0f};
        if (mapReady) {
            auto* plate = makeTextPlate("MAP DOWNLOADED", {128.0f, 29.0f}, mapPos, kGreen, 0.20f, kGreen);
            m_root->addChild(plate, 3);
        } else {
            auto* plate = makeTextPlate("OPENING LEVEL...", {128.0f, 29.0f}, mapPos, kCyan, 0.20f, ccc3(235, 242, 255));
            m_root->addChild(plate, 3);
        }

        if (!mapReady) {
            auto* plate = makeTextPlate("WAITING FOR MAP", {128.0f, 29.0f}, songPos, kBlue, 0.19f, ccc3(218, 228, 244));
            m_root->addChild(plate, 3);
        } else if (songReady) {
            auto* plate = makeTextPlate("SONG DOWNLOADED", {128.0f, 29.0f}, songPos, kGreen, 0.19f, kGreen);
            m_root->addChild(plate, 3);
        } else if (RankedRuntime::get().songBypassAllowed()) {
            auto* plate = makeTextPlate("START WITHOUT SONG", {136.0f, 29.0f}, songPos, kGold, 0.18f, kGold);
            m_root->addChild(plate, 3);
        } else {
            // The real click target remains the vanilla song button on the level
            // info page. This button only opens that gated page.
            addButton("DOWNLOAD SONG", songPos, menu_selector(CorumRankedLayer::onDownloadSong), false, 0.48f);
        }

        auto const seconds = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
        std::string footer = fmt::format("STARTS IN  {:02d}", seconds);
        ccColor3B footerAccent = seconds <= 3 ? kSideRed : kBlue;
        if (match.state == "DEATHMATCH_PLAYING") {
            auto const ownUsed = ownA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
            auto const opponentUsed = ownA ? match.deathmatchAttemptsUsedB : match.deathmatchAttemptsUsedA;
            footer = ownUsed >= 3
                ? fmt::format("WAITING FOR OPPONENT   {}/3", opponentUsed)
                : fmt::format("ATTEMPT  {}  OF  3", std::min(3, ownUsed + 1));
            footerAccent = ownUsed >= 3 ? kBlue : kGold;
        } else if (phaseSeconds() >= 10.0) {
            auto const ownReady = ownA ? match.readyA : match.readyB;
            auto const opponentReady = ownA ? match.readyB : match.readyA;
            if (!ownReady) footer = "WAITING FOR YOUR DOWNLOAD";
            else if (!opponentReady) footer = fmt::format("WAITING FOR {}", upper(shorten(match.opponentName, 13)));
            else footer = "STARTING...";
            footerAccent = kGold;
        }
        auto* footerPlate = makeTextPlate(footer, {210.0f, 37.0f}, {size.width / 2.0f, 27.0f}, footerAccent, 0.25f, ccc3(245, 248, 255), "goldFont.fnt");
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

        auto* title = makeLabel("YOUR ATTEMPT IS FINISHED", 0.48f, {size.width / 2.0f, size.height - 34.0f}, kGold, "goldFont.fnt");
        title->limitLabelWidth(size.width - 70.0f, 0.48f, 0.30f);
        m_root->addChild(title, 5);
        auto* subtitle = makeLabel("WAITING FOR OPPONENT", 0.24f, {size.width / 2.0f, size.height - 59.0f}, kCyan);
        m_root->addChild(subtitle, 5);

        auto* panel = makeNeonPanel({246.0f, 142.0f}, {size.width / 2.0f, size.height / 2.0f - 4.0f}, kBlue, kDeepPanel, 238);
        m_root->addChild(panel, 1);

        auto* name = makeLabel(upper(shorten(opponent, 18)), 0.32f, {size.width / 2.0f, size.height / 2.0f + 38.0f});
        name->limitLabelWidth(205.0f, 0.32f, 0.21f);
        m_root->addChild(name, 5);
        auto* live = makeLabel(
            opponentAttemptActive ? "LIVE PROGRESS" : "WAITING TO START",
            0.16f,
            {size.width / 2.0f, size.height / 2.0f + 15.0f},
            kCyan
        );
        m_root->addChild(live, 5);
        auto* pct = makeLabel(
            opponentAttemptActive ? fmt::format("{}%", progress) : "--",
            0.74f,
            {size.width / 2.0f, size.height / 2.0f - 15.0f},
            kGold,
            "goldFont.fnt"
        );
        m_root->addChild(pct, 5);
        auto* score = makeLabel(
            fmt::format("YOUR SCORE  {}     OPPONENT  {}", formatScore(ownScore), formatScore(opponentScore)),
            0.18f,
            {size.width / 2.0f, size.height / 2.0f - 53.0f},
            ccc3(218, 228, 244)
        );
        score->limitLabelWidth(220.0f, 0.18f, 0.13f);
        m_root->addChild(score, 5);

        std::string hintText = "THE ROUND WILL RESOLVE WHEN THE ACTIVE ATTEMPT ENDS";
        if (!opponentAttemptActive && match.state == "LAST_ATTEMPT_WINDOW") {
            auto const seconds = std::max<std::int64_t>(0, RankedRuntime::get().deadlineSeconds().value_or(0));
            hintText = fmt::format("OPPONENT HAS {}s TO START THE LAST ATTEMPT", seconds);
        }
        auto* hint = makeLabel(hintText, 0.15f, {size.width / 2.0f, 28.0f}, ccc3(195, 205, 224));
        hint->limitLabelWidth(size.width - 80.0f, 0.15f, 0.11f);
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
        auto* caption = makeLabel("SERIES SCORE", 0.16f, {size.width / 2.0f, size.height / 2.0f + 14.0f}, kCyan);
        m_root->addChild(caption, 5);
        auto* score = makeLabel(fmt::format("{}   :   {}", match.roundWinsA, match.roundWinsB), 0.45f, {size.width / 2.0f, size.height / 2.0f - 10.0f}, ccc3(245, 248, 255), "goldFont.fnt");
        m_root->addChild(score, 5);

        auto* next = makeTextPlate("NEXT MAP PREPARING...", {166.0f, 30.0f}, {size.width / 2.0f, size.height / 2.0f - 58.0f}, kCyan, 0.18f, ccc3(225, 235, 248));
        m_root->addChild(next, 3);
    }

    void addClearChecks(int clears, CCPoint base, bool rightAligned = false) {
        auto const count = std::clamp(clears, 0, 2);
        for (int i = 0; i < count; ++i) {
            auto* check = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            check->setScale(0.23f);
            check->setColor(kGreen);
            check->setPosition({base.x + (rightAligned ? -1.0f : 1.0f) * i * 13.0f, base.y});
            m_root->addChild(check, 5);
        }
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
            auto* title = makeLabel("MATCH CANCELED", 0.70f, {size.width / 2.0f, size.height / 2.0f + 58.0f}, kRed, "goldFont.fnt");
            m_root->addChild(title, 3);
            auto* reason = makeLabel(
                match.cancellationReason.empty() ? "MATCH INVALID - NO RATING CHANGES" : match.cancellationReason,
                0.28f,
                {size.width / 2.0f, size.height / 2.0f + 12.0f},
                ccc3(220, 220, 225)
            );
            reason->limitLabelWidth(size.width - 100.0f, 0.28f, 0.18f);
            m_root->addChild(reason, 3);
            addButton("QUEUE AGAIN", {size.width / 2.0f, size.height / 2.0f - 55.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.66f);
            return;
        }

        auto const ownA = match.side == "A";
        auto const won = match.winnerSide == match.side;
        auto const tierA = match.profileAfterTierA.empty() ? match.effectiveTier : match.profileAfterTierA;
        auto const tierB = match.profileAfterTierB.empty() ? match.effectiveTier : match.profileAfterTierB;
        auto const scoreA = match.profileAfterScoreA.value_or(match.ratingAfterA.value_or(0));
        auto const scoreB = match.profileAfterScoreB.value_or(match.ratingAfterB.value_or(0));
        addPlayerCard({100.0f, size.height / 2.0f - 5.0f}, match.playerAName, tierA, scoreA, ownA);
        addPlayerCard({size.width - 100.0f, size.height / 2.0f - 5.0f}, match.playerBName, tierB, scoreB, !ownA);

        auto* title = makeLabel("MATCH END", 0.60f, {size.width / 2.0f, size.height - 25.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 4);
        auto* leftFinal = makeLabel(fmt::format("{}", match.roundWinsA), 0.82f, {size.width / 2.0f - 105.0f, size.height / 2.0f + 18.0f}, match.winnerSide == "A" ? kGreen : kRed);
        auto* rightFinal = makeLabel(fmt::format("{}", match.roundWinsB), 0.82f, {size.width / 2.0f + 105.0f, size.height / 2.0f + 18.0f}, match.winnerSide == "B" ? kGreen : kRed);
        m_root->addChild(leftFinal, 4);
        m_root->addChild(rightFinal, 4);
        auto* leftResult = makeLabel(match.winnerSide == "A" ? "WIN" : "LOSE", 0.35f, {100.0f, 38.0f}, match.winnerSide == "A" ? kGreen : kRed, "goldFont.fnt");
        auto* rightResult = makeLabel(match.winnerSide == "B" ? "WIN" : "LOSE", 0.35f, {size.width - 100.0f, 38.0f}, match.winnerSide == "B" ? kGreen : kRed, "goldFont.fnt");
        m_root->addChild(leftResult, 4);
        m_root->addChild(rightResult, 4);

        float y = size.height - 67.0f;
        for (auto const& round : match.rounds) {
            auto* row = makeLabel(fmt::format("ROUND {}   {} : {}", round.roundNumber, round.scoreA, round.scoreB), 0.24f, {size.width / 2.0f, y});
            m_root->addChild(row, 4);
            addClearChecks(round.clearsA, {size.width / 2.0f - 54.0f, y - 13.0f});
            addClearChecks(round.clearsB, {size.width / 2.0f + 54.0f, y - 13.0f}, true);
            y -= 36.0f;
        }
        for (auto const& deathmatch : match.deathmatches) {
            auto* row = makeLabel(fmt::format("DEATH MATCH {}   {} : {}", deathmatch.sequence, deathmatch.scoreA, deathmatch.scoreB), 0.22f, {size.width / 2.0f, y}, kRed);
            m_root->addChild(row, 4);
            y -= 28.0f;
        }

        if (match.mmrDeltaA) {
            auto* delta = makeLabel(fmt::format("{:+d} LP", *match.mmrDeltaA), 0.28f, {100.0f, 58.0f}, *match.mmrDeltaA >= 0 ? kGreen : kRed);
            m_root->addChild(delta, 4);
        }
        if (match.mmrDeltaB) {
            auto* delta = makeLabel(fmt::format("{:+d} LP", *match.mmrDeltaB), 0.28f, {size.width - 100.0f, 58.0f}, *match.mmrDeltaB >= 0 ? kGreen : kRed);
            m_root->addChild(delta, 4);
        }
        auto promotionText = [](std::string const& before, std::string const& after) -> std::string {
            if (before.empty() || after.empty() || before == after) return {};
            static std::vector<std::string> order = {"UNRANKED", "RED", "AQUA", "BRONZE", "SILVER", "GOLD"};
            auto const b = std::find(order.begin(), order.end(), before);
            auto const a = std::find(order.begin(), order.end(), after);
            if (b == order.end() || a == order.end()) return {};
            return a > b ? "PROMOTED" : "DEMOTED";
        };
        auto const promotionA = promotionText(match.profileBeforeTierA, tierA);
        auto const promotionB = promotionText(match.profileBeforeTierB, tierB);
        if (!promotionA.empty()) m_root->addChild(makeLabel(promotionA, 0.23f, {100.0f, 20.0f}, promotionA == "PROMOTED" ? kGreen : kRed), 4);
        if (!promotionB.empty()) m_root->addChild(makeLabel(promotionB, 0.23f, {size.width - 100.0f, 20.0f}, promotionB == "PROMOTED" ? kGreen : kRed), 4);

        addButton("QUEUE AGAIN", {size.width / 2.0f, 24.0f}, menu_selector(CorumRankedLayer::onQueueAgain), false, 0.52f);
    }

    void renderHistoryList(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(false);
        auto* title = makeLabel("MATCH HISTORY", 0.58f, {size.width / 2.0f, size.height - 28.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 3);
        if (view.historyLoading) {
            auto* spinner = LoadingSpinner::create(55.0f);
            spinner->setPosition({size.width / 2.0f, size.height / 2.0f});
            m_root->addChild(spinner, 3);
            return;
        }
        if (!view.historyError.empty()) {
            m_root->addChild(makeLabel(view.historyError, 0.27f, {size.width / 2.0f, size.height / 2.0f}, kRed), 3);
            addButton("RETRY", {size.width / 2.0f, size.height / 2.0f - 40.0f}, menu_selector(CorumRankedLayer::onHistory), false, 0.55f);
            return;
        }
        if (view.history.empty()) {
            m_root->addChild(makeLabel("NO COMPLETED MATCHES", 0.32f, {size.width / 2.0f, size.height / 2.0f}, ccc3(190, 195, 210)), 3);
            return;
        }
        auto const count = std::min<std::size_t>(6, view.history.size());
        for (std::size_t i = 0; i < count; ++i) {
            auto const& item = view.history[i];
            auto const y = size.height - 72.0f - static_cast<float>(i) * 38.0f;
            auto const won = item.side == item.winnerSide;
            auto* panel = makePanel({size.width - 110.0f, 31.0f}, {size.width / 2.0f, y}, kPanelLight);
            m_root->addChild(panel, 1);
            auto* row = makeLabel(
                fmt::format("{}   VS {}   {:+d} LP", won ? "WIN" : "LOSS", shorten(item.opponentName, 14), item.ownMmrDelta.value_or(0)),
                0.23f,
                {size.width / 2.0f - 18.0f, y},
                won ? kGreen : kRed
            );
            row->limitLabelWidth(size.width - 190.0f, 0.23f, 0.17f);
            m_root->addChild(row, 3);
            auto* detail = addButton("DETAIL", {size.width - 72.0f, y}, menu_selector(CorumRankedLayer::onHistoryDetail), false, 0.32f);
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
        auto* title = makeLabel(won ? "WIN" : "LOSS", 0.62f, {size.width / 2.0f, size.height - 28.0f}, won ? kGreen : kRed, "goldFont.fnt");
        m_root->addChild(title, 3);
        auto* opponent = makeLabel(fmt::format("VS {}   {} : {}", shorten(item.opponentName, 16), item.roundWinsA, item.roundWinsB), 0.30f, {size.width / 2.0f, size.height - 58.0f});
        m_root->addChild(opponent, 3);
        float y = size.height - 96.0f;
        for (auto const& round : item.rounds) {
            auto* row = makeLabel(fmt::format("ROUND {}  {}  {} : {}", round.roundNumber, shorten(round.mapTitle, 12), round.scoreA, round.scoreB), 0.25f, {size.width / 2.0f, y});
            m_root->addChild(row, 3);
            addClearChecks(round.clearsA, {size.width / 2.0f - 48.0f, y - 13.0f});
            addClearChecks(round.clearsB, {size.width / 2.0f + 48.0f, y - 13.0f}, true);
            y -= 40.0f;
        }
        for (auto const& deathmatch : item.deathmatches) {
            auto* row = makeLabel(fmt::format("DEATH MATCH {}  {} : {}", deathmatch.sequence, deathmatch.scoreA, deathmatch.scoreB), 0.24f, {size.width / 2.0f, y}, kRed);
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

    GJGameLevel* findLevelInfoMap() const {
        if (auto* level = findLocalMap()) return level;
        auto const& current = RankedRuntime::get().view().match.currentMap;
        if (!current || current->levelId <= 0) return nullptr;

        // LevelInfoLayer can own the real vanilla map download as long as it has
        // an online-level shell with the selected ID. The server snapshot already
        // gives us enough metadata to render that page until GD replaces it with
        // the downloaded payload.
        auto* level = GJGameLevel::create();
        if (!level) return nullptr;
        level->setLevelID(current->levelId);
        level->m_levelName = current->title;
        level->m_creatorName = current->creator;
        level->m_levelNotDownloaded = true;
        return level;
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
        if (!level) return;
        RankedAudioManager::get().fadeOutForGameplay();

        // The real Geometry Dash LevelInfo page owns map/song acquisition. The
        // explicit override is used as a recovery path when polling skipped a
        // very short PREPARE phase (notably Debug Trigger Death Match).
        auto const countdownRemaining = countdownOverride >= 0.0
            ? countdownOverride
            : std::max(0.0, 10.0 - phaseSeconds());
        m_enteringSongGate = corum::ranked::showRankedSongDownloadGate(level, countdownRemaining);
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
        if (match.state != "ROUND_PREPARE" && match.state != "DEATHMATCH_PREPARE") return;

        // Preparation now happens entirely on the real LevelInfoLayer. Opening
        // that page immediately lets Geometry Dash own the selected map download,
        // exposes only its vanilla song-download control, shows the countdown, and
        // auto-enters Play when the server starts the round.
        openSongDownloadGate();
    }

    void maybeEnterLevel() {
        auto const& view = RankedRuntime::get().view();
        if (m_enteringLevel || view.stage != RuntimeStage::Matched) return;
        auto const& state = view.match.state;
        if (state != "ROUND_PLAYING" && state != "DEATHMATCH_PLAYING") return;
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
        m_enteringLevel = true;
        RankedAudioManager::get().fadeOutForGameplay();
        // Do not construct PlayLayer directly. Enter through Geometry Dash's normal
        // LevelInfoLayer -> onPlay path; this lets the game own level validation,
        // loading transitions, audio setup, and the return/quit stack.
        auto* scene = LevelInfoLayer::scene(level, false);
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
        RankedAudioManager::get().setMode(RankedAudioMode::Menu);
    }

    void onJoin(CCObject*) {
        m_localMessage.clear();
        RankedRuntime::get().joinQueue();
    }

    void onLeave(CCObject*) {
        RankedRuntime::get().leaveQueue();
    }

    void onRetry(CCObject*) {
        RankedRuntime::get().begin();
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
        if (m_page == Page::HistoryDetail) {
            m_page = Page::HistoryList;
        } else {
            m_page = Page::Live;
        }
    }

    void onCloseLayer(CCObject*) {
        if (RankedRuntime::get().view().stage == RuntimeStage::Queued) RankedRuntime::get().leaveQueue();
        RankedAudioManager::get().restoreMenuMusic();
        // Ranked now lives in its own GD-style scene/tab. Return to the previous
        // Geometry Dash scene instead of merely deleting the fullscreen child.
        CCDirector::sharedDirector()->popScene();
    }

    void keyBackClicked() override {
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
