#include "DebugBotPopup.hpp"

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)

#include "RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <array>
#include <string>

using namespace geode::prelude;

namespace {

constexpr char kDebugPassword[] = "2008";
constexpr std::array<char const*, 3> kDifficulties {"EASY", "NORMAL", "HARD"};
constexpr std::array<char const*, 7> kScenarios {
    "NORMAL MATCH",
    "FORCE BOT 1 CLEAR",
    "FORCE BOT 2 CLEARS",
    "TRIGGER LAST ATTEMPT",
    "TRIGGER ROUND DRAW",
    "TRIGGER ROUND 3",
    "TRIGGER DEATHMATCH",
};
constexpr std::array<char const*, 7> kScenarioValues {
    "NORMAL_MATCH",
    "FORCE_BOT_1_CLEAR",
    "FORCE_BOT_2_CLEARS",
    "TRIGGER_LAST_ATTEMPT",
    "TRIGGER_ROUND_DRAW",
    "TRIGGER_ROUND_3",
    "TRIGGER_DEATHMATCH",
};

CCMenuItemSpriteExtra* button(
    CCObject* target,
    char const* text,
    CCPoint position,
    SEL_MenuHandler selector,
    float scale = 0.66f
) {
    auto* sprite = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_01.png", 0.72f);
    sprite->setScale(scale);
    auto* result = CCMenuItemSpriteExtra::create(sprite, target, selector);
    result->setPosition(position);
    return result;
}

class DebugBotConfigPopup final : public Popup {
protected:
    CCMenu* m_actions = nullptr;
    CCLabelBMFont* m_difficulty = nullptr;
    CCLabelBMFont* m_scenario = nullptr;
    CCLabelBMFont* m_ban = nullptr;
    CCLabelBMFont* m_discord = nullptr;
    std::size_t m_difficultyIndex = 1;
    std::size_t m_scenarioIndex = 0;
    bool m_randomBan = true;
    bool m_sendDiscord = false;

    bool init() override {
        if (!Popup::init(420.0f, 300.0f)) return false;
        setTitle("DEBUG BOT MATCH", "goldFont.fnt", 0.68f, 22.0f);
        auto* warning = CCLabelBMFont::create("DEVELOPMENT ONLY - RESULTS AFFECT RANKED RATING", "bigFont.fnt");
        warning->setColor(ccc3(255, 145, 105));
        warning->setPosition({210.0f, 244.0f});
        warning->limitLabelWidth(375.0f, 0.29f, 0.20f);
        m_mainLayer->addChild(warning, 3);

        m_actions = CCMenu::create();
        m_actions->setPosition(CCPointZero);
        m_mainLayer->addChild(m_actions, 4);
        m_actions->addChild(button(this, "Difficulty", {105.0f, 198.0f}, menu_selector(DebugBotConfigPopup::onDifficulty)));
        m_actions->addChild(button(this, "Scenario", {105.0f, 151.0f}, menu_selector(DebugBotConfigPopup::onScenario)));
        m_actions->addChild(button(this, "Bot Ban", {105.0f, 104.0f}, menu_selector(DebugBotConfigPopup::onBan)));
        m_actions->addChild(button(this, "Discord", {105.0f, 67.0f}, menu_selector(DebugBotConfigPopup::onDiscord)));
        m_actions->addChild(button(this, "START", {320.0f, 44.0f}, menu_selector(DebugBotConfigPopup::onStart), 0.78f));

        m_difficulty = valueLabel({300.0f, 198.0f});
        m_scenario = valueLabel({285.0f, 151.0f});
        m_ban = valueLabel({300.0f, 104.0f});
        m_discord = valueLabel({300.0f, 67.0f});
        refreshValues();
        return true;
    }

    CCLabelBMFont* valueLabel(CCPoint position) {
        auto* result = CCLabelBMFont::create("", "bigFont.fnt");
        result->setPosition(position);
        result->setColor(ccc3(110, 240, 255));
        result->limitLabelWidth(190.0f, 0.34f, 0.20f);
        m_mainLayer->addChild(result, 3);
        return result;
    }

    void refreshValues() {
        m_difficulty->setString(kDifficulties[m_difficultyIndex]);
        m_scenario->setString(kScenarios[m_scenarioIndex]);
        m_ban->setString(m_randomBan ? "RANDOM" : "NO BAN");
        m_discord->setString(m_sendDiscord ? "ON" : "OFF");
    }

    void onDifficulty(CCObject*) {
        m_difficultyIndex = (m_difficultyIndex + 1) % kDifficulties.size();
        refreshValues();
    }

    void onScenario(CCObject*) {
        m_scenarioIndex = (m_scenarioIndex + 1) % kScenarios.size();
        refreshValues();
    }

    void onBan(CCObject*) {
        m_randomBan = !m_randomBan;
        refreshValues();
    }

    void onDiscord(CCObject*) {
        m_sendDiscord = !m_sendDiscord;
        refreshValues();
    }

    void onStart(CCObject* sender) {
        corum::ranked::RankedRuntime::get().startDebugBotMatch({
            .password = kDebugPassword,
            .difficulty = kDifficulties[m_difficultyIndex],
            .scenario = kScenarioValues[m_scenarioIndex],
            .botBan = m_randomBan ? "RANDOM" : "NO_BAN",
            .sendDiscordEvents = m_sendDiscord,
        });
        onClose(sender);
    }

public:
    static DebugBotConfigPopup* create() {
        auto* popup = new DebugBotConfigPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class DebugBotPasswordPopup final : public Popup {
protected:
    TextInput* m_password = nullptr;

    bool init() override {
        if (!Popup::init(330.0f, 205.0f)) return false;
        setTitle("DEBUG BOT MATCH", "goldFont.fnt", 0.65f, 22.0f);
        m_password = TextInput::create(220.0f, "Password");
        m_password->setLabel("Password");
        m_password->setPasswordMode(true);
        m_password->setMaxCharCount(32);
        m_password->setPosition({165.0f, 113.0f});
        m_mainLayer->addChild(m_password, 3);

        auto* actions = CCMenu::create();
        actions->setPosition(CCPointZero);
        actions->addChild(button(this, "Cancel", {105.0f, 52.0f}, menu_selector(DebugBotPasswordPopup::onCancel)));
        actions->addChild(button(this, "Enter", {225.0f, 52.0f}, menu_selector(DebugBotPasswordPopup::onEnter)));
        m_mainLayer->addChild(actions, 4);
        return true;
    }

    void onCancel(CCObject* sender) {
        if (m_password) m_password->setString("");
        onClose(sender);
    }

    void onEnter(CCObject* sender) {
        auto const entered = m_password ? std::string(m_password->getString()) : std::string();
        if (entered != kDebugPassword) {
            if (m_password) m_password->setString("");
            FLAlertLayer::create("DEBUG BOT MATCH", "Incorrect password.", "OK")->show();
            return;
        }
        if (m_password) m_password->setString("");
        auto* next = DebugBotConfigPopup::create();
        onClose(sender);
        if (next) next->show();
    }

public:
    static DebugBotPasswordPopup* create() {
        auto* popup = new DebugBotPasswordPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace

namespace corum::ranked {

void showDebugBotPasswordPopup() {
    if (auto* popup = DebugBotPasswordPopup::create()) popup->show();
}

} // namespace corum::ranked

#endif
