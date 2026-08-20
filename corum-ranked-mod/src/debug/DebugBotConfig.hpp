#pragma once

#include <string_view>

namespace corum::ranked::debug {

enum class BotDifficulty {
    Easy,
    Normal,
    Hard,
};

enum class BotScenario {
    NormalMatch,
    ForceBotOneClear,
    ForceBotTwoClears,
    TriggerLastAttempt,
    TriggerRoundDraw,
    TriggerRoundThree,
    TriggerDeathmatch,
};

enum class BotBanMode {
    Random,
    NoBan,
};

struct DebugBotOptions {
    BotDifficulty difficulty = BotDifficulty::Normal;
    BotScenario scenario = BotScenario::NormalMatch;
    BotBanMode botBan = BotBanMode::Random;
    bool sendDiscordEvents = false;
};

[[nodiscard]] bool isDebugBotPasswordValid(std::string_view password);
[[nodiscard]] char const* serverValue(BotDifficulty value);
[[nodiscard]] char const* serverValue(BotScenario value);
[[nodiscard]] char const* serverValue(BotBanMode value);
[[nodiscard]] char const* displayName(BotDifficulty value);
[[nodiscard]] char const* displayName(BotScenario value);
[[nodiscard]] char const* displayName(BotBanMode value);

} // namespace corum::ranked::debug
