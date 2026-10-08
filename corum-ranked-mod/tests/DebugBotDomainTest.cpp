#include "../src/debug/DebugBotConfig.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace corum::ranked::debug;

int main() {
    assert(isDebugBotPasswordValid("2008"));
    assert(!isDebugBotPasswordValid("2007"));
    assert(!isDebugBotPasswordValid("02008"));
    assert(!isDebugBotPasswordValid(""));

    assert(std::string(serverValue(BotDifficulty::Easy)) == "EASY");
    assert(std::string(serverValue(BotDifficulty::Normal)) == "NORMAL");
    assert(std::string(serverValue(BotDifficulty::Hard)) == "HARD");
    assert(std::string(serverValue(BotScenario::NormalMatch)) == "NORMAL_MATCH");
    assert(std::string(serverValue(BotScenario::ForceBotOneClear)) == "FORCE_BOT_1_CLEAR");
    assert(std::string(serverValue(BotScenario::ForceBotThreeClears)) == "FORCE_BOT_3_CLEARS");
    assert(std::string(serverValue(BotScenario::ForceWin)) == "FORCE_WIN");
    assert(std::string(serverValue(BotScenario::ForceLose)) == "FORCE_LOSE");
    assert(std::string(serverValue(BotScenario::TriggerLastAttempt)) == "TRIGGER_LAST_ATTEMPT");
    assert(std::string(serverValue(BotScenario::TriggerRoundDraw)) == "TRIGGER_ROUND_DRAW");
    assert(std::string(serverValue(BotScenario::TriggerRoundThree)) == "TRIGGER_ROUND_3");
    assert(std::string(serverValue(BotScenario::TriggerDeathmatch)) == "TRIGGER_DEATHMATCH");
    assert(std::string(serverValue(BotBanMode::Random)) == "RANDOM");
    assert(std::string(serverValue(BotBanMode::NoBan)) == "NO_BAN");

    std::cout << "Corum Ranked Debug Bot option tests passed\n";
}
