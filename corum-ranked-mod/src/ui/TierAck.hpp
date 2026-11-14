#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <optional>
#include <string>

namespace corum::ranked::ui {

// What the player has already been shown, per GD account: the Solo and
// Survival tiers ("tier-solo", "tier-survival") and the achievement grade
// ("achievement-grade"). A difference between this and the live value is a
// change the player has not seen yet (another player's result moved the
// ladder, a grade earned by claiming AP), so the lobby plays its motion once.
inline std::string tierAckKey(char const* what) {
    auto* account = GJAccountManager::get();
    return fmt::format("ack-{}:{}", what, account ? account->m_accountID : 0);
}

inline std::optional<std::string> readTierAck(char const* what) {
    auto const key = tierAckKey(what);
    if (!geode::Mod::get()->hasSavedValue(key)) return std::nullopt;
    return geode::Mod::get()->getSavedValue<std::string>(key);
}

inline void writeTierAck(char const* what, std::string const& value) {
    if (readTierAck(what) == value) return;
    geode::Mod::get()->setSavedValue(tierAckKey(what), value);
}

} // namespace corum::ranked::ui
