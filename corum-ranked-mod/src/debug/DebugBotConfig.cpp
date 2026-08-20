#include "DebugBotConfig.hpp"

namespace {

constexpr std::string_view kDevelopmentPassword = "2008";

} // namespace

namespace corum::ranked::debug {

bool isDebugBotPasswordValid(std::string_view password) {
    return password == kDevelopmentPassword;
}

char const* serverValue(BotDifficulty value) {
    switch (value) {
        case BotDifficulty::Easy: return "EASY";
        case BotDifficulty::Normal: return "NORMAL";
        case BotDifficulty::Hard: return "HARD";
    }
    return "NORMAL";
}

char const* serverValue(BotScenario value) {
    switch (value) {
        case BotScenario::NormalMatch: return "NORMAL_MATCH";
        case BotScenario::ForceBotOneClear: return "FORCE_BOT_ONE_CLEAR";
        case BotScenario::ForceBotTwoClears: return "FORCE_BOT_TWO_CLEARS";
        case BotScenario::TriggerLastAttempt: return "TRIGGER_LAST_ATTEMPT";
        case BotScenario::TriggerRoundDraw: return "TRIGGER_ROUND_DRAW";
        case BotScenario::TriggerRoundThree: return "TRIGGER_ROUND_THREE";
        case BotScenario::TriggerDeathmatch: return "TRIGGER_DEATHMATCH";
    }
    return "NORMAL_MATCH";
}

char const* serverValue(BotBanMode value) {
    switch (value) {
        case BotBanMode::Random: return "RANDOM";
        case BotBanMode::NoBan: return "NO_BAN";
    }
    return "RANDOM";
}

char const* displayName(BotDifficulty value) {
    switch (value) {
        case BotDifficulty::Easy: return "Easy";
        case BotDifficulty::Normal: return "Normal";
        case BotDifficulty::Hard: return "Hard";
    }
    return "Normal";
}

char const* displayName(BotScenario value) {
    switch (value) {
        case BotScenario::NormalMatch: return "Normal Match";
        case BotScenario::ForceBotOneClear: return "Force Bot 1 Clear";
        case BotScenario::ForceBotTwoClears: return "Force Bot 2 Clears";
        case BotScenario::TriggerLastAttempt: return "Trigger LAST ATTEMPT";
        case BotScenario::TriggerRoundDraw: return "Trigger Round Draw";
        case BotScenario::TriggerRoundThree: return "Trigger Round 3";
        case BotScenario::TriggerDeathmatch: return "Trigger Deathmatch";
    }
    return "Normal Match";
}

char const* displayName(BotBanMode value) {
    switch (value) {
        case BotBanMode::Random: return "Random";
        case BotBanMode::NoBan: return "No Ban";
    }
    return "Random";
}

} // namespace corum::ranked::debug
