#pragma once
#include <optional>
#include <initializer_list>
#include <string_view>

namespace corum::ranked {
enum class SpectatorGameMode { Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing };
inline constexpr std::string_view spectatorModeName(SpectatorGameMode mode) {
    switch (mode) {
        case SpectatorGameMode::Cube: return "CUBE";
        case SpectatorGameMode::Ship: return "SHIP";
        case SpectatorGameMode::Ball: return "BALL";
        case SpectatorGameMode::UFO: return "UFO";
        case SpectatorGameMode::Wave: return "WAVE";
        case SpectatorGameMode::Robot: return "ROBOT";
        case SpectatorGameMode::Spider: return "SPIDER";
        case SpectatorGameMode::Swing: return "SWING";
    }
    return {};
}
inline std::optional<SpectatorGameMode> parseSpectatorMode(std::string_view name) {
    for (auto mode : {SpectatorGameMode::Cube, SpectatorGameMode::Ship,
        SpectatorGameMode::Ball, SpectatorGameMode::UFO, SpectatorGameMode::Wave,
        SpectatorGameMode::Robot, SpectatorGameMode::Spider, SpectatorGameMode::Swing}) {
        if (spectatorModeName(mode) == name) return mode;
    }
    return std::nullopt;
}
} // namespace corum::ranked
