#pragma once

// Registry expectations only: no native hooks, binding calls or runtime toggles.
#include <array>
#include <string_view>

namespace corum::ranked::diagnostic {
struct PlayerHookExpectation {
    std::string_view target;
    bool selected;
};

inline constexpr std::array<PlayerHookExpectation, 7> playerHookExpectations{{
    {"PlayerObject::update",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_UPDATE_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::updateRotation",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_UPDATE_ROTATION_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::spiderTestJump",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_SPIDER_TEST_JUMP_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::setPosition",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_SET_POSITION_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::setRotation",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_SET_ROTATION_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::setVisible",
#if defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK)
        false},
#else
        true},
#endif
    {"PlayerObject::updateSpecial",
#if defined(GEODE_IS_WINDOWS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_HOOKS) || defined(CORUM_CRASH_DIAG_NO_PLAYER_UPDATE_SPECIAL_HOOK)
        false},
#else
        true},
#endif
}};

struct PlayerHookRegistration {
    unsigned registered = 0;
    unsigned enabled = 0;
};

constexpr bool playerHookRegistryMatches(std::array<PlayerHookRegistration, 7> const& actual) {
    for (std::size_t i = 0; i < actual.size(); ++i) {
        unsigned const expected = playerHookExpectations[i].selected ? 1 : 0;
        if (actual[i].registered != expected || actual[i].enabled != expected) return false;
    }
    return true;
}
} // namespace corum::ranked::diagnostic
