#include "RankedPopup.hpp"

#include "RankedRuntime.hpp"
#include "DebugBotPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;
using corum::ranked::DeathmatchSummaryView;
using corum::ranked::HistoryMatchView;
using corum::ranked::MatchView;
using corum::ranked::RankedRuntime;
using corum::ranked::RoundSummaryView;
using corum::ranked::RuntimeStage;
using corum::ranked::RuntimeView;

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr ccColor3B kPanelColor = {27, 32, 47};
constexpr ccColor3B kPanelLight = {42, 49, 69};
constexpr ccColor3B kAccent = {95, 180, 255};
constexpr ccColor3B kGreen = {78, 232, 112};
constexpr ccColor3B kRed = {255, 92, 92};
constexpr ccColor3B kGold = {255, 216, 86};

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

CCScale9Sprite* makePanel(CCSize size, CCPoint position, ccColor3B color = kPanelColor, GLubyte opacity = 235) {
    auto* panel = CCScale9Sprite::create("square02_001.png", {0.0f, 0.0f, 80.0f, 80.0f});
    panel->setContentSize(size);
    panel->setColor(color);
    panel->setOpacity(opacity);
    panel->setPosition(position);
    return panel;
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

class CorumRankedLayer final : public CCLayerColor, public LevelDownloadDelegate {
    enum class Page {
        Live,
        HistoryList,
        HistoryDetail,
    };

    CCNode* m_root = nullptr;
    CCMenu* m_menu = nullptr;
    GJGameLevel* m_downloadedLevel = nullptr;
    LevelInfoLayer* m_songDriver = nullptr;
    Page m_page = Page::Live;
    std::size_t m_historyIndex = 0;
    std::string m_phaseKey;
    std::string m_resourceKey;
    std::string m_lastMatchId;
    std::string m_selectedBan;
    std::string m_pendingBan;
    std::string m_localMessage;
    SteadyClock::time_point m_phaseStartedAt {};
    std::optional<SteadyClock::time_point> m_mapDownloadStartedAt;
    std::optional<SteadyClock::time_point> m_lastMapDownloadAttemptAt;
    std::optional<SteadyClock::time_point> m_songDownloadStartedAt;
    std::vector<int> m_songIds;
    std::unordered_set<int> m_songInfoRequested;
    std::unordered_set<int> m_songDownloadRequested;
    std::unordered_map<int, SteadyClock::time_point> m_songLastRequestAt;
    int m_downloadingLevelId = 0;
    bool m_readySent = false;
    bool m_matchFoundReadySent = false;
    bool m_mapFailureReported = false;
    bool m_songBypassed = false;
    bool m_vanillaSongDownloadKicked = false;
    bool m_enteringLevel = false;

    ~CorumRankedLayer() override {
        detachLevelDownloadDelegate();
        CC_SAFE_RELEASE(m_songDriver);
        CC_SAFE_RELEASE(m_downloadedLevel);
    }

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
            background->setColor(ccc3(0, 108, 235));
            addChild(background, 0);
        }

        m_root = CCNode::create();
        m_root->setAnchorPoint({0.0f, 0.0f});
        m_root->setPosition(CCPointZero);
        m_root->setContentSize(CCDirector::sharedDirector()->getWinSize());
        addChild(m_root, 1);

        m_menu = CCMenu::create();
        m_menu->setPosition(CCPointZero);
        m_menu->setID("corum-ranked-fullscreen-menu"_spr);
        addChild(m_menu, 10);

        RankedRuntime::get().begin();
        syncPhase();
        render();
        schedule(schedule_selector(CorumRankedLayer::refresh), 0.20f);
        return true;
    }

    void refresh(float) {
        auto& runtime = RankedRuntime::get();
        runtime.tick();
        syncPhase();
        updateAutomation();
        render();
        maybeEnterLevel();
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
                m_mapDownloadStartedAt.reset();
                m_lastMapDownloadAttemptAt.reset();
                m_songDownloadStartedAt.reset();
                m_readySent = false;
                m_mapFailureReported = false;
                m_songBypassed = false;
                m_vanillaSongDownloadKicked = false;
                RankedRuntime::get().setSongBypassAllowed(false);
                m_enteringLevel = false;
                m_songIds.clear();
                m_songInfoRequested.clear();
                m_songDownloadRequested.clear();
                m_songLastRequestAt.clear();
                m_downloadingLevelId = 0;
                CC_SAFE_RELEASE(m_songDriver);
                m_songDriver = nullptr;
                CC_SAFE_RELEASE(m_downloadedLevel);
                m_downloadedLevel = nullptr;
            }

        }
    }

    double phaseSeconds() const {
        if (m_phaseStartedAt == SteadyClock::time_point{}) return 0.0;
        return std::chrono::duration<double>(SteadyClock::now() - m_phaseStartedAt).count();
    }

    double elapsed(std::optional<SteadyClock::time_point> const& start) const {
        if (!start) return 0.0;
        return std::chrono::duration<double>(SteadyClock::now() - *start).count();
    }

    void clearUi() {
        m_root->removeAllChildrenWithCleanup(true);
        m_menu->removeAllChildrenWithCleanup(true);
    }

    void render() {
        clearUi();
        auto const& view = RankedRuntime::get().view();
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
        auto* panel = makePanel({148.0f, 158.0f}, center);
        m_root->addChild(panel, 1);

        int frame = 1;
        if (self) {
            if (auto* game = GameManager::sharedState()) frame = std::max(1, game->getPlayerFrame());
        }
        auto* icon = SimplePlayer::create(frame);
        icon->setScale(1.08f);
        icon->setPosition({center.x, center.y + 31.0f});
        if (self) {
            if (auto* game = GameManager::sharedState()) {
                icon->setColors(game->colorForIdx(game->getPlayerColor()), game->colorForIdx(game->getPlayerColor2()));
            }
        }
        m_root->addChild(icon, 3);

        auto* nameLabel = makeLabel(shorten(name, 15), 0.34f, {center.x, center.y - 35.0f});
        nameLabel->limitLabelWidth(128.0f, 0.34f, 0.22f);
        m_root->addChild(nameLabel, 3);
        auto* tierLabel = makeLabel(
            fmt::format("{}  {} LP", upper(tier), score),
            0.29f,
            {center.x, center.y - 57.0f},
            tierColor(upper(tier))
        );
        tierLabel->limitLabelWidth(132.0f, 0.29f, 0.20f);
        m_root->addChild(tierLabel, 3);
    }

    void renderMain(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        addTopBack(true);

        auto* title = makeLabel("CORUM RANKED", 0.68f, {size.width / 2.0f, size.height - 34.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 3);

        auto* historySprite = ButtonSprite::create("HISTORY", "bigFont.fnt", "GJ_button_04.png", 0.75f);
        historySprite->setScale(0.56f);
        auto* history = CCMenuItemSpriteExtra::create(historySprite, this, menu_selector(CorumRankedLayer::onHistory));
        history->setPosition({size.width - 60.0f, size.height - 28.0f});
        m_menu->addChild(history);

        auto* body = makePanel({std::min(410.0f, size.width - 70.0f), 190.0f}, {size.width / 2.0f, size.height / 2.0f - 3.0f});
        m_root->addChild(body, 0);

        auto const leftX = size.width / 2.0f - 96.0f;
        auto const rightX = size.width / 2.0f + 96.0f;
        auto* emblem = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        emblem->setScale(1.18f);
        emblem->setColor(tierColor(upper(view.profileTier)));
        emblem->setPosition({leftX, size.height / 2.0f + 26.0f});
        m_root->addChild(emblem, 2);
        auto* tier = makeLabel(
            fmt::format("{} - {} LP", upper(view.profileTier), view.profileScore),
            0.38f,
            {leftX, size.height / 2.0f - 48.0f},
            tierColor(upper(view.profileTier))
        );
        tier->limitLabelWidth(180.0f, 0.38f, 0.24f);
        m_root->addChild(tier, 3);

        if (view.stage == RuntimeStage::Ready) {
            auto* playCircle = CCSprite::createWithSpriteFrameName("GJ_playBtn_001.png");
            playCircle->setScale(0.82f);
            auto* play = CCMenuItemSpriteExtra::create(playCircle, this, menu_selector(CorumRankedLayer::onJoin));
            play->setPosition({rightX, size.height / 2.0f + 20.0f});
            m_menu->addChild(play);
            auto* gameStart = makeLabel("GAME START", 0.31f, {rightX, size.height / 2.0f - 48.0f}, kAccent);
            m_root->addChild(gameStart, 3);
        } else if (view.stage == RuntimeStage::Queued || view.stage == RuntimeStage::JoiningQueue) {
            auto* spinner = LoadingSpinner::create(56.0f);
            spinner->setPosition({rightX, size.height / 2.0f + 20.0f});
            m_root->addChild(spinner, 3);
            addButton("LEAVE QUEUE", {rightX, size.height / 2.0f - 48.0f}, menu_selector(CorumRankedLayer::onLeave), true, 0.56f);
        } else if (view.stage == RuntimeStage::Loading) {
            auto* spinner = LoadingSpinner::create(56.0f);
            spinner->setPosition({rightX, size.height / 2.0f + 20.0f});
            m_root->addChild(spinner, 3);
        } else {
            addButton("RETRY", {rightX, size.height / 2.0f + 10.0f}, menu_selector(CorumRankedLayer::onRetry), false, 0.66f);
        }

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        addButton("DEBUG BOT MATCH", {92.0f, 30.0f}, menu_selector(CorumRankedLayer::onDebugBot), true, 0.48f);
#endif

        auto detail = view.error.empty() ? view.status : view.error;
        if (!m_localMessage.empty()) detail = m_localMessage;
        if (!detail.empty()) {
            auto* status = makeLabel(shorten(detail, 90), 0.23f, {size.width / 2.0f, 24.0f}, view.error.empty() ? ccc3(190, 200, 220) : kRed);
            status->limitLabelWidth(size.width - 210.0f, 0.23f, 0.17f);
            m_root->addChild(status, 3);
        }
    }

    void renderMatch(RuntimeView const& view) {
        auto const& match = view.match;
        if (match.state == "MATCH_RESULT" || match.state == "CANCELLED") {
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
        addPlayerCard({112.0f, size.height / 2.0f - 7.0f}, match.playerAName, match.playerATier, match.playerAScore, ownA);
        addPlayerCard({size.width - 112.0f, size.height / 2.0f - 7.0f}, match.playerBName, match.playerBTier, match.playerBScore, !ownA);
        auto* title = makeLabel("MATCH FOUND", 0.62f, {size.width / 2.0f, size.height - 34.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 3);
        auto const left = std::max(0, 5 - static_cast<int>(std::floor(phaseSeconds())));
        auto* countdown = makeLabel(fmt::format("MAP BAN IN...\n{}", left), 0.52f, {size.width / 2.0f, size.height / 2.0f - 1.0f}, kRed, "goldFont.fnt");
        countdown->setAnchorPoint({0.5f, 0.5f});
        m_root->addChild(countdown, 3);
    }

    void renderBan(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* title = makeLabel("MATCH FOUND", 0.52f, {size.width / 2.0f, size.height - 30.0f}, kGold, "goldFont.fnt");
        m_root->addChild(title, 3);
        auto* phase = makeLabel("BAN MAP", 0.44f, {size.width / 2.0f, size.height - 58.0f}, kRed, "goldFont.fnt");
        m_root->addChild(phase, 3);
        auto const remaining = RankedRuntime::get().deadlineSeconds().value_or(0);
        auto* timer = makeLabel(fmt::format("{}", std::max<std::int64_t>(0, remaining)), 0.55f, {size.width - 40.0f, size.height - 42.0f}, kRed);
        m_root->addChild(timer, 3);

        auto const count = std::min<std::size_t>(5, match.candidateMaps.size());
        auto const cardWidth = (size.width - 44.0f) / 5.0f;
        auto const startX = 22.0f + cardWidth / 2.0f;
        for (std::size_t i = 0; i < count; ++i) {
            auto const x = startX + cardWidth * static_cast<float>(i);
            auto* panel = makePanel({cardWidth - 7.0f, 128.0f}, {x, size.height / 2.0f - 7.0f}, kPanelLight);
            m_root->addChild(panel, 1);
            auto* map = makeLabel(twoLineTitle(match.candidateMaps[i].title, 10), 0.235f, {x, size.height / 2.0f + 27.0f});
            map->limitLabelWidth(cardWidth - 12.0f, 0.235f, 0.17f);
            m_root->addChild(map, 3);
            auto* diff = makeLabel(match.candidateMaps[i].difficulty, 0.23f, {x, size.height / 2.0f - 9.0f}, difficultyColor(match.candidateMaps[i].difficulty));
            diff->limitLabelWidth(cardWidth - 14.0f, 0.23f, 0.17f);
            m_root->addChild(diff, 3);
            if (m_selectedBan == match.candidateMaps[i].canonicalLevelId) {
                addStatusPill("BANNED", {x, size.height / 2.0f - 50.0f}, kGreen);
            } else if (m_pendingBan == match.candidateMaps[i].canonicalLevelId) {
                addStatusPill("BANNING...", {x, size.height / 2.0f - 50.0f}, kAccent);
            } else if (m_selectedBan.empty() && m_pendingBan.empty()) {
                auto* button = addButton("BAN", {x, size.height / 2.0f - 50.0f}, menu_selector(CorumRankedLayer::onBan), true, 0.36f);
                button->setTag(static_cast<int>(i));
            }
        }
        auto* privacy = makeLabel("YOUR BAN IS PRIVATE UNTIL THE PHASE ENDS", 0.20f, {size.width / 2.0f, 24.0f}, ccc3(180, 190, 210));
        m_root->addChild(privacy, 2);
    }

    void renderPrepare(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const ownA = match.side == "A";
        addPlayerCard({103.0f, size.height / 2.0f - 3.0f}, match.playerAName, match.playerATier, match.playerAScore, ownA);
        addPlayerCard({size.width - 103.0f, size.height / 2.0f - 3.0f}, match.playerBName, match.playerBTier, match.playerBScore, !ownA);

        auto* title = makeLabel(
            match.deathmatchSequence > 0 ? "DEATH MATCH" : fmt::format("ROUND {}", match.roundNumber),
            0.56f,
            {size.width / 2.0f, size.height - 26.0f},
            match.deathmatchSequence > 0 ? kRed : kGold,
            "goldFont.fnt"
        );
        m_root->addChild(title, 4);
        if (match.deathmatchSequence > 0) {
            auto* sub = makeLabel("3 ATTEMPTS", 0.27f, {size.width / 2.0f, size.height - 49.0f}, kGreen);
            m_root->addChild(sub, 4);
            auto* attempts = makeLabel(
                fmt::format("A {}/3   B {}/3", match.deathmatchAttemptsUsedA, match.deathmatchAttemptsUsedB),
                0.20f,
                {size.width / 2.0f, size.height - 66.0f},
                ccc3(215, 220, 230)
            );
            m_root->addChild(attempts, 4);
        } else if (!match.banner.empty() && match.banner != "NONE") {
            auto* sub = makeLabel(displayBanner(match.banner), 0.30f, {size.width / 2.0f, size.height - 49.0f}, kRed, "goldFont.fnt");
            m_root->addChild(sub, 4);
        }

        if (match.currentMap) {
            auto* mapName = makeLabel(twoLineTitle(match.currentMap->title, 18), 0.34f, {size.width / 2.0f, size.height / 2.0f + 53.0f});
            mapName->limitLabelWidth(210.0f, 0.34f, 0.22f);
            m_root->addChild(mapName, 4);
            auto* difficulty = makeLabel(match.currentMap->difficulty, 0.42f, {size.width / 2.0f, size.height / 2.0f + 20.0f}, difficultyColor(match.currentMap->difficulty));
            m_root->addChild(difficulty, 4);
        }

        auto* leftScore = makeLabel(fmt::format("{}", match.scoreA), 0.82f, {size.width / 2.0f - 74.0f, size.height / 2.0f + 5.0f}, kRed);
        auto* rightScore = makeLabel(fmt::format("{}", match.scoreB), 0.82f, {size.width / 2.0f + 74.0f, size.height / 2.0f + 5.0f}, kRed);
        m_root->addChild(leftScore, 4);
        m_root->addChild(rightScore, 4);

        auto* local = findPlayableMap();
        auto const mapReady = local != nullptr;
        auto const songReady = mapReady && isSongReady(local);
        auto const mapPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 28.0f};
        auto const songPos = CCPoint{size.width / 2.0f, size.height / 2.0f - 62.0f};
        if (mapReady) {
            addStatusPill("DOWNLOADED", mapPos, kGreen);
        } else if (m_downloadingLevelId != 0) {
            addStatusPill("DOWNLOADING...", mapPos, kAccent);
        } else {
            addButton("DOWNLOAD MAP", mapPos, menu_selector(CorumRankedLayer::onDownloadMap), false, 0.50f);
        }

        if (!mapReady) {
            addStatusPill("WAITING FOR MAP", songPos, kPanelLight);
        } else if (songReady) {
            addStatusPill("DOWNLOADED", songPos, kGreen);
        } else if (m_songBypassed) {
            addStatusPill("START WITHOUT SONG", songPos, kGold);
        } else if (isFetchingSongInfo(local)) {
            addStatusPill("FETCHING SONG INFO...", songPos, kAccent);
        } else if (isSongDownloading(local)) {
            addStatusPill("DOWNLOADING...", songPos, kAccent);
        } else {
            addButton("DOWNLOAD SONG", songPos, menu_selector(CorumRankedLayer::onDownloadSong), false, 0.50f);
        }

        auto const seconds = std::max(0, 10 - static_cast<int>(std::floor(phaseSeconds())));
        std::string footer = fmt::format("STARTS IN... {}", seconds);
        if (match.state == "DEATHMATCH_PLAYING") {
            auto const ownUsed = ownA ? match.deathmatchAttemptsUsedA : match.deathmatchAttemptsUsedB;
            auto const opponentUsed = ownA ? match.deathmatchAttemptsUsedB : match.deathmatchAttemptsUsedA;
            footer = ownUsed >= 3
                ? fmt::format("WAITING FOR OPPONENT...  {} / 3", opponentUsed)
                : fmt::format("ATTEMPT {} OF 3", std::min(3, ownUsed + 1));
        } else if (phaseSeconds() >= 10.0) {
            auto const ownReady = ownA ? match.readyA : match.readyB;
            auto const opponentReady = ownA ? match.readyB : match.readyA;
            if (!ownReady) footer = "WAITING FOR YOUR DOWNLOAD...";
            else if (!opponentReady) footer = fmt::format("WAITING FOR {}'S DOWNLOAD...", upper(shorten(match.opponentName, 13)));
            else footer = "STARTING...";
        }
        auto* footerLabel = makeLabel(footer, 0.42f, {size.width / 2.0f, 29.0f}, phaseSeconds() >= 10.0 ? kGold : ccc3(225, 225, 230));
        footerLabel->setAnchorPoint({0.5f, 0.5f});
        footerLabel->limitLabelWidth(size.width - 170.0f, 0.42f, 0.24f);
        m_root->addChild(footerLabel, 4);
    }

    void renderInterRound(MatchView const& match) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* title = makeLabel(
            match.state == "DEATHMATCH_RESULT" ? "DEATH MATCH RESULT" : "ROUND RESULT",
            0.62f,
            {size.width / 2.0f, size.height / 2.0f + 50.0f},
            kGold,
            "goldFont.fnt"
        );
        m_root->addChild(title, 3);
        auto* score = makeLabel(fmt::format("{} : {}", match.roundWinsA, match.roundWinsB), 0.88f, {size.width / 2.0f, size.height / 2.0f});
        m_root->addChild(score, 3);
        auto* next = makeLabel("NEXT MAP PREPARING...", 0.28f, {size.width / 2.0f, size.height / 2.0f - 52.0f}, kAccent);
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
        if (m_downloadedLevel && static_cast<int>(m_downloadedLevel->m_levelID) == id) return m_downloadedLevel;
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
        m_songInfoRequested.clear();
        m_songDownloadRequested.clear();
        m_songLastRequestAt.clear();
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

    bool isFetchingSongInfo(GJGameLevel* level) {
        if (!level) return false;
        refreshSongIds(level);
        auto* manager = MusicDownloadManager::sharedState();
        if (!manager) return false;
        for (auto const id : m_songIds) {
            if (songIdReady(manager, id) || manager->getSongInfoObject(id)) continue;
            auto const* key = manager->getSongInfoKey(id);
            if (key && manager->isDLActive(key)) return true;
        }
        return false;
    }

    bool isSongDownloading(GJGameLevel* level) {
        if (!level || !m_songDownloadStartedAt) return false;
        refreshSongIds(level);
        if (auto* driver = ensureSongDriver(level)) {
            if (driver->m_songWidget && !driver->m_songWidget->m_isNotDownloading) return true;
        }
        auto* manager = MusicDownloadManager::sharedState();
        if (!manager) return false;
        for (auto const id : m_songIds) {
            if (songIdReady(manager, id)) continue;
            auto const* key = manager->getSongDownloadKey(id);
            if ((key && manager->isDLActive(key)) || manager->isRunningActionForSongID(id)) return true;
        }
        return false;
    }

    bool requestCooldownElapsed(int id, double seconds) const {
        auto const it = m_songLastRequestAt.find(id);
        if (it == m_songLastRequestAt.end()) return true;
        return std::chrono::duration<double>(SteadyClock::now() - it->second).count() >= seconds;
    }

    void startMapDownload() {
        auto const id = RankedRuntime::get().currentLevelId();
        if (id <= 0 || findPlayableMap() || m_downloadingLevelId != 0) return;
        if (m_lastMapDownloadAttemptAt && elapsed(m_lastMapDownloadAttemptAt) < 1.5) return;
        auto* manager = GameLevelManager::sharedState();
        if (!manager) return;
        auto const now = SteadyClock::now();
        if (!m_mapDownloadStartedAt) m_mapDownloadStartedAt = now;
        m_lastMapDownloadAttemptAt = now;
        m_downloadingLevelId = id;
        manager->m_levelDownloadDelegate = this;
        manager->downloadLevel(id, false, 0);
    }

    LevelInfoLayer* ensureSongDriver(GJGameLevel* level) {
        if (!level) return nullptr;
        auto const id = static_cast<int>(level->m_levelID);
        if (m_songDriver && m_songDriver->m_level && static_cast<int>(m_songDriver->m_level->m_levelID) == id) {
            return m_songDriver;
        }
        CC_SAFE_RELEASE(m_songDriver);
        m_songDriver = LevelInfoLayer::create(level, false);
        CC_SAFE_RETAIN(m_songDriver);
        return m_songDriver;
    }

    bool kickVanillaSongDownload(GJGameLevel* level) {
        auto* driver = ensureSongDriver(level);
        if (!driver || !driver->m_songWidget) return false;
        auto* widget = driver->m_songWidget;
        if (!widget->m_isNotDownloading) return true;
        widget->onDownload(nullptr);
        m_vanillaSongDownloadKicked = true;
        return true;
    }

    void startSongDownload() {
        auto* level = findPlayableMap();
        if (!level) return;
        refreshSongIds(level);
        auto* manager = MusicDownloadManager::sharedState();
        if (!manager || m_songIds.empty() || isSongReady(level)) return;

        if (!m_songDownloadStartedAt) m_songDownloadStartedAt = SteadyClock::now();
        // Primary path: press Geometry Dash's own hidden CustomSongWidget download
        // handler. Direct MusicDownloadManager calls remain only as a fallback for
        // additional 2.2 song IDs / stalled metadata.
        kickVanillaSongDownload(level);
        auto const now = SteadyClock::now();
        for (auto const id : m_songIds) {
            if (songIdReady(manager, id)) {
                m_songInfoRequested.erase(id);
                m_songDownloadRequested.erase(id);
                continue;
            }

            auto* info = manager->getSongInfoObject(id);
            if (!info || info->m_unloaded) {
                // Song metadata is required before a reliable custom-song download.
                // Re-request it if the previous request stalled instead of getting
                // permanently stuck in the alpha.11 "Downloading" state.
                if (!m_songInfoRequested.contains(id) || requestCooldownElapsed(id, 2.0)) {
                    // The second argument is the vanilla "download after info" flag.
                    // alpha.15 passed false, which could fetch metadata without ever
                    // engaging the actual custom-song download.
                    manager->getSongInfo(id, true);
                    m_songInfoRequested.insert(id);
                    m_songLastRequestAt[id] = now;
                }
                continue;
            }

            m_songInfoRequested.erase(id);
            // MusicDownloadManager deduplicates its active download list. Reissuing
            // after a short cooldown also recovers from silent network failures.
            if (!m_songDownloadRequested.contains(id) || requestCooldownElapsed(id, 3.0)) {
                // Ranked maps use custom songs. downloadCustomSong follows GD's
                // custom-content URL/path pipeline; downloadSong is not the correct
                // fallback for this flow on current GD builds.
                manager->downloadCustomSong(id);
                m_songDownloadRequested.insert(id);
                m_songLastRequestAt[id] = now;
            }
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
        if (match.state != "ROUND_PREPARE" && match.state != "DEATHMATCH_PREPARE") return;

        auto* level = findPlayableMap();
        if (phaseSeconds() >= 5.0) {
            if (!level) startMapDownload();
            level = findPlayableMap();
            if (level && !isSongReady(level)) startSongDownload();
        }

        if (!level && m_mapDownloadStartedAt && elapsed(m_mapDownloadStartedAt) >= 30.0 && !m_mapFailureReported) {
            m_mapFailureReported = true;
            runtime.reportMapDownloadFailure();
            return;
        }
        if (level && !isSongReady(level) && m_songDownloadStartedAt && elapsed(m_songDownloadStartedAt) >= 20.0) {
            // Audio is optional after the 20-second ceiling. Do not cancel the
            // MusicDownloadManager request: an in-flight song can still finish in
            // the background and GD will pick it up on a later attempt.
            m_songBypassed = true;
            runtime.setSongBypassAllowed(true);
        }

        if (level && isSongReady(level)) {
            m_songBypassed = false;
            runtime.setSongBypassAllowed(false);
        }
        auto const ownReady = match.side == "A" ? match.readyA : match.readyB;
        auto const resourcesReady = level && (isSongReady(level) || m_songBypassed);
        if (phaseSeconds() >= 10.0 && resourcesReady && !ownReady && !m_readySent) {
            m_readySent = true;
            runtime.submitReady();
        }
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
            m_localMessage = "Waiting for complete level data before entering.";
            return;
        }
        m_enteringLevel = true;
        detachLevelDownloadDelegate();
        // Do not construct PlayLayer directly. Enter through Geometry Dash's normal
        // LevelInfoLayer -> onPlay path; this lets the game own level validation,
        // loading transitions, audio setup, and the return/quit stack.
        auto* scene = LevelInfoLayer::scene(level, false);
        CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.25f, scene));
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
        startMapDownload();
    }

    void onDownloadSong(CCObject*) {
        startSongDownload();
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

    void levelDownloadFinished(GJGameLevel* level) override {
        detachLevelDownloadDelegate();
        m_downloadingLevelId = 0;
        if (!level || level->m_levelString.empty() || level->m_levelNotDownloaded) {
            // A list/search metadata object is not enough to construct gameplay.
            // Keep the download timer running and allow the normal retry loop to
            // request the full downloadGJLevel payload again.
            m_localMessage = "Map metadata loaded, but playable level data is still missing. Retrying...";
            return;
        }
        CC_SAFE_RETAIN(level);
        CC_SAFE_RELEASE(m_downloadedLevel);
        m_downloadedLevel = level;
        m_localMessage = "Map downloaded.";
        refreshSongIds(level);
        if (phaseSeconds() >= 5.0 && !isSongReady(level)) startSongDownload();
    }

    void levelDownloadFailed(int response) override {
        detachLevelDownloadDelegate();
        m_downloadingLevelId = 0;
        m_localMessage = fmt::format("Map download failed ({}). Retrying is allowed until the 30s limit.", response);
    }

    void detachLevelDownloadDelegate() {
        auto* manager = GameLevelManager::sharedState();
        if (manager && manager->m_levelDownloadDelegate == this) manager->m_levelDownloadDelegate = nullptr;
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
