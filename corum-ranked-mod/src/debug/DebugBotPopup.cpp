#include "DebugBotPopup.hpp"

#include "DebugBotConfig.hpp"
#include "../RankedRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <array>
#include <string>

using namespace geode::prelude;

namespace {

char const* buttonBackground(bool danger = false) {
    return danger ? "GJ_button_06.png" : "GJ_button_01.png";
}

CCMenuItemSpriteExtra* menuButton(
    CCMenu* menu,
    CCObject* target,
    char const* text,
    CCPoint position,
    SEL_MenuHandler selector,
    bool danger = false,
    float scale = 0.62f
) {
    auto* sprite = ButtonSprite::create(text, "bigFont.fnt", buttonBackground(danger), 0.75f);
    sprite->setScale(scale);
    auto* button = CCMenuItemSpriteExtra::create(sprite, target, selector);
    button->setPosition(position);
    menu->addChild(button);
    return button;
}

class DebugBotSetupPopup final : public Popup {
protected:
    std::string m_password;
    corum::ranked::debug::DebugBotOptions m_options;
    CCMenu* m_menu = nullptr;
    CCLabelBMFont* m_difficultyValue = nullptr;
    CCLabelBMFont* m_scenarioValue = nullptr;
    CCLabelBMFont* m_banValue = nullptr;
    CCLabelBMFont* m_discordValue = nullptr;

    ~DebugBotSetupPopup() override {
        std::fill(m_password.begin(), m_password.end(), '\0');
        m_password.clear();
    }

    bool init(std::string password) {
        if (!Popup::init(440.0f, 310.0f)) return false;
        m_password = std::move(password);
        setTitle("DEBUG BOT MATCH", "goldFont.fnt", 0.68f, 22.0f);

        auto* warning = CCLabelBMFont::create("Development-only server harness", "bigFont.fnt");
        warning->setColor(ccc3(255, 180, 80));
        warning->setScale(0.28f);
        warning->setPosition({220.0f, 260.0f});
        m_mainLayer->addChild(warning, 2);

        m_menu = CCMenu::create();
        m_menu->setPosition(CCPointZero);
        m_mainLayer->addChild(m_menu, 4);

        addCycleRow(
            "Bot Difficulty",
            214.0f,
            m_difficultyValue,
            menu_selector(DebugBotSetupPopup::onPreviousDifficulty),
            menu_selector(DebugBotSetupPopup::onNextDifficulty)
        );
        addCycleRow(
            "Scenario",
            164.0f,
            m_scenarioValue,
            menu_selector(DebugBotSetupPopup::onPreviousScenario),
            menu_selector(DebugBotSetupPopup::onNextScenario)
        );
        addCycleRow(
            "Bot Ban",
            114.0f,
            m_banValue,
            menu_selector(DebugBotSetupPopup::onToggleBan),
            menu_selector(DebugBotSetupPopup::onToggleBan)
        );
        addCycleRow(
            "Send Discord Events",
            70.0f,
            m_discordValue,
            menu_selector(DebugBotSetupPopup::onToggleDiscord),
            menu_selector(DebugBotSetupPopup::onToggleDiscord)
        );

        menuButton(
            m_menu,
            this,
            "Start",
            {280.0f, 28.0f},
            menu_selector(DebugBotSetupPopup::onStart),
            false,
            0.7f
        );
        menuButton(
            m_menu,
            this,
            "Cancel",
            {160.0f, 28.0f},
            menu_selector(DebugBotSetupPopup::onClose),
            true,
            0.7f
        );
        updateValues();
        return true;
    }

    void addCycleRow(
        char const* title,
        float y,
        CCLabelBMFont*& valueLabel,
        SEL_MenuHandler previous,
        SEL_MenuHandler next
    ) {
        auto* titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
        titleLabel->setAnchorPoint({0.0f, 0.5f});
        titleLabel->setPosition({44.0f, y + 16.0f});
        titleLabel->setScale(0.29f);
        m_mainLayer->addChild(titleLabel, 2);

        valueLabel = CCLabelBMFont::create("", "goldFont.fnt");
        valueLabel->setPosition({220.0f, y - 2.0f});
        valueLabel->limitLabelWidth(250.0f, 0.36f, 0.22f);
        m_mainLayer->addChild(valueLabel, 2);

        menuButton(m_menu, this, "<", {68.0f, y - 2.0f}, previous, false, 0.48f);
        menuButton(m_menu, this, ">", {372.0f, y - 2.0f}, next, false, 0.48f);
    }

    void updateValues() {
        using namespace corum::ranked::debug;
        m_difficultyValue->setString(displayName(m_options.difficulty));
        m_scenarioValue->setString(displayName(m_options.scenario));
        m_banValue->setString(displayName(m_options.botBan));
        m_discordValue->setString(m_options.sendDiscordEvents ? "ON" : "OFF");
        m_discordValue->setColor(m_options.sendDiscordEvents
            ? ccc3(120, 255, 145)
            : ccc3(180, 180, 180));
    }

    void onPreviousDifficulty(CCObject*) {
        using corum::ranked::debug::BotDifficulty;
        if (m_options.difficulty == BotDifficulty::Easy) m_options.difficulty = BotDifficulty::Hard;
        else if (m_options.difficulty == BotDifficulty::Normal) m_options.difficulty = BotDifficulty::Easy;
        else m_options.difficulty = BotDifficulty::Normal;
        updateValues();
    }

    void onNextDifficulty(CCObject*) {
        using corum::ranked::debug::BotDifficulty;
        if (m_options.difficulty == BotDifficulty::Easy) m_options.difficulty = BotDifficulty::Normal;
        else if (m_options.difficulty == BotDifficulty::Normal) m_options.difficulty = BotDifficulty::Hard;
        else m_options.difficulty = BotDifficulty::Easy;
        updateValues();
    }

    void onPreviousScenario(CCObject*) {
        using corum::ranked::debug::BotScenario;
        constexpr std::array values {
            BotScenario::NormalMatch,
            BotScenario::ForceBotOneClear,
            BotScenario::ForceBotTwoClears,
            BotScenario::TriggerLastAttempt,
            BotScenario::TriggerRoundDraw,
            BotScenario::TriggerRoundThree,
            BotScenario::TriggerDeathmatch,
        };
        auto found = std::find(values.begin(), values.end(), m_options.scenario);
        auto index = found == values.begin() ? values.size() - 1 : static_cast<std::size_t>(found - values.begin() - 1);
        m_options.scenario = values[index];
        updateValues();
    }

    void onNextScenario(CCObject*) {
        using corum::ranked::debug::BotScenario;
        constexpr std::array values {
            BotScenario::NormalMatch,
            BotScenario::ForceBotOneClear,
            BotScenario::ForceBotTwoClears,
            BotScenario::TriggerLastAttempt,
            BotScenario::TriggerRoundDraw,
            BotScenario::TriggerRoundThree,
            BotScenario::TriggerDeathmatch,
        };
        auto found = std::find(values.begin(), values.end(), m_options.scenario);
        auto const index = (static_cast<std::size_t>(found - values.begin()) + 1) % values.size();
        m_options.scenario = values[index];
        updateValues();
    }

    void onToggleBan(CCObject*) {
        using corum::ranked::debug::BotBanMode;
        m_options.botBan = m_options.botBan == BotBanMode::Random
            ? BotBanMode::NoBan
            : BotBanMode::Random;
        updateValues();
    }

    void onToggleDiscord(CCObject*) {
        m_options.sendDiscordEvents = !m_options.sendDiscordEvents;
        updateValues();
    }

    void onStart(CCObject* sender) {
        corum::ranked::RankedRuntime::get().startDebugBotMatch(m_password, m_options);
        Popup::onClose(sender);
    }

public:
    static DebugBotSetupPopup* create(std::string password) {
        auto* popup = new DebugBotSetupPopup();
        if (popup && popup->init(std::move(password))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

class DebugBotPasswordPopup final : public Popup {
protected:
    TextInput* m_passwordInput = nullptr;
    CCLabelBMFont* m_errorLabel = nullptr;
    CCMenu* m_menu = nullptr;

    bool init() override {
        if (!Popup::init(340.0f, 215.0f)) return false;
        setTitle("DEBUG BOT MATCH", "goldFont.fnt", 0.65f, 21.0f);

        auto* label = CCLabelBMFont::create("Password", "bigFont.fnt");
        label->setPosition({170.0f, 155.0f});
        label->setScale(0.34f);
        m_mainLayer->addChild(label, 2);

        m_passwordInput = TextInput::create(190.0f, "Password");
        m_passwordInput->setPosition({170.0f, 120.0f});
        m_passwordInput->setCommonFilter(CommonFilter::Uint);
        m_passwordInput->setMaxCharCount(4);
        m_passwordInput->setPasswordMode(true);
        m_mainLayer->addChild(m_passwordInput, 3);

        m_errorLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_errorLabel->setColor(ccc3(255, 110, 110));
        m_errorLabel->setPosition({170.0f, 87.0f});
        m_errorLabel->setScale(0.28f);
        m_mainLayer->addChild(m_errorLabel, 2);

        m_menu = CCMenu::create();
        m_menu->setPosition(CCPointZero);
        m_mainLayer->addChild(m_menu, 4);
        menuButton(
            m_menu,
            this,
            "Cancel",
            {110.0f, 43.0f},
            menu_selector(DebugBotPasswordPopup::onClose),
            true,
            0.66f
        );
        menuButton(
            m_menu,
            this,
            "Enter",
            {230.0f, 43.0f},
            menu_selector(DebugBotPasswordPopup::onEnter),
            false,
            0.66f
        );
        m_passwordInput->focus();
        return true;
    }

    void onEnter(CCObject* sender) {
        auto password = std::string(m_passwordInput->getString());
        if (!corum::ranked::debug::isDebugBotPasswordValid(password)) {
            m_errorLabel->setString("Incorrect password.");
            m_passwordInput->setString("");
            return;
        }
        m_passwordInput->defocus();
        Popup::onClose(sender);
        if (auto* setup = DebugBotSetupPopup::create(std::move(password))) setup->show();
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

namespace corum::ranked::debug {

void showDebugBotPasswordPopup() {
    if (auto* popup = DebugBotPasswordPopup::create()) popup->show();
}

} // namespace corum::ranked::debug
