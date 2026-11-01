#pragma once

// In-game HUD customization (pure, no cocos). A HUD element is a group of
// nodes authored at absolute default coordinates. Customization moves and
// scales that group around a fixed pivot (the element's authored anchor
// point) and can hide it. Placements are display-only: they never reach the
// server, scoring, judgement or replays.

#include <matjson.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace corum::ranked {

enum class HudMode { Solo, Survival };

struct HudPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool operator==(HudPoint const&) const = default;
};

struct HudElementSpec {
    std::string_view id;
    std::string_view labelKey;
    std::string_view fallback;
    // The timer is the minimum match information and cannot be hidden.
    bool hideable = true;
    // Survival progress-bar markers live on GD's own bar: visibility only.
    bool movable = true;
};

inline constexpr std::array<HudElementSpec, 9> SoloHudElements {{
    {"timer", "hud-element-timer", "Timer", false, true},
    {"round", "hud-element-round", "Round", true, true},
    {"state", "hud-element-state", "Round state", true, true},
    {"own", "hud-element-own", "My score", true, true},
    {"opponent", "hud-element-opponent", "Opponent score", true, true},
    {"qualifying", "hud-element-qualifying", "Qualifying", true, true},
    {"progress", "hud-element-progress", "Opponent progress", true, true},
    {"feed", "hud-element-feed", "Death feed", true, true},
    {"fps", "hud-element-fps", "FPS", true, true},
}};

inline constexpr std::array<HudElementSpec, 9> SurvivalHudElements {{
    {"timer", "hud-element-timer", "Timer", false, true},
    {"round", "hud-element-round", "Round", true, true},
    {"difference", "hud-element-difference", "Cutoff gap", true, true},
    {"standings", "hud-element-standings", "Standings", true, true},
    {"rank", "hud-element-rank", "My rank", true, true},
    {"waiting", "hud-element-waiting", "Waiting text", true, true},
    {"aux", "hud-element-aux", "Death match clock", true, true},
    {"cutline", "hud-element-cutline", "Progress bar markers", true, false},
    {"fps", "hud-element-fps", "FPS", true, true},
}};

inline std::span<HudElementSpec const> hudElements(HudMode mode) {
    return mode == HudMode::Solo
        ? std::span<HudElementSpec const>(SoloHudElements)
        : std::span<HudElementSpec const>(SurvivalHudElements);
}

inline HudElementSpec const* hudElement(HudMode mode, std::string_view id) {
    for (auto const& spec : hudElements(mode)) if (spec.id == id) return &spec;
    return nullptr;
}

inline constexpr float HudMinimumScale = 0.6f;
inline constexpr float HudMaximumScale = 1.6f;
inline constexpr float HudScaleStep = 0.1f;
inline constexpr float HudSnapGrid = 8.0f;
inline constexpr float HudOffsetLimit = 4096.0f;

inline float clampHudScale(float scale) {
    if (!std::isfinite(scale)) return 1.0f;
    auto const steps = std::round(std::clamp(scale, HudMinimumScale, HudMaximumScale) / HudScaleStep);
    return static_cast<float>(steps) * HudScaleStep;
}

inline float snapHud(float value) {
    return std::round(value / HudSnapGrid) * HudSnapGrid;
}

// Authored pivot of each element at the given window size. These mirror the
// anchor points of the in-game labels (PlayLayer / SurvivalHud use the same
// layoutHud() values), so a default placement is an identity transform.
inline std::optional<HudPoint> hudDefaultPivot(HudMode mode, std::string_view id, float width, float height) {
    auto const top = height - 7.0f;
    if (id == "fps") return HudPoint{7.0f, top - 0.5f};
    if (mode == HudMode::Solo) {
        auto const left = 42.0f + 8.0f, right = width - 42.0f - 8.0f;
        if (id == "own") return HudPoint{left, top - 4.0f};
        if (id == "opponent") return HudPoint{right, top - 4.0f};
        if (id == "round") return HudPoint{width / 2.0f, top - 1.0f};
        if (id == "timer") return HudPoint{width / 2.0f, top - 18.0f};
        if (id == "state") return HudPoint{width / 2.0f, top - 47.0f};
        if (id == "qualifying") return HudPoint{14.0f, 12.0f};
        if (id == "progress") return HudPoint{width - 10.0f - 42.0f, 22.0f};
        if (id == "feed") return HudPoint{14.0f, 39.0f};
        return std::nullopt;
    }
    if (id == "aux") return HudPoint{8.0f, top - 18.0f};
    if (id == "standings") return HudPoint{8.0f, top - 28.0f};
    if (id == "round") return HudPoint{width / 2.0f, top - 5.0f};
    if (id == "timer") return HudPoint{width / 2.0f, top - 18.0f};
    if (id == "difference") return HudPoint{width / 2.0f + 34.0f, top - 18.0f};
    if (id == "rank") return HudPoint{12.0f, 12.0f};
    if (id == "waiting") return HudPoint{width / 2.0f, 14.0f};
    if (id == "cutline") return HudPoint{width / 2.0f, height - 8.0f};
    return std::nullopt;
}

// Nine screen zones; row 0 is the top row (GL y grows upward).
enum class HudAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

inline constexpr std::array<std::string_view, 9> HudAnchorNames {
    "top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right",
};

inline std::string_view hudAnchorName(HudAnchor anchor) {
    return HudAnchorNames[static_cast<std::size_t>(anchor)];
}

inline std::optional<HudAnchor> parseHudAnchor(std::string_view name) {
    for (std::size_t index = 0; index < HudAnchorNames.size(); ++index)
        if (HudAnchorNames[index] == name) return static_cast<HudAnchor>(index);
    return std::nullopt;
}

inline HudPoint hudAnchorPoint(HudAnchor anchor, float width, float height) {
    auto const index = static_cast<int>(anchor);
    auto const column = index % 3, row = index / 3;
    return {static_cast<float>(column) * width / 2.0f, height - static_cast<float>(row) * height / 2.0f};
}

inline HudAnchor hudZone(HudPoint center, float width, float height) {
    auto const column = center.x < width / 3.0f ? 0 : center.x > width * 2.0f / 3.0f ? 2 : 1;
    auto const row = center.y > height * 2.0f / 3.0f ? 0 : center.y < height / 3.0f ? 2 : 1;
    return static_cast<HudAnchor>(row * 3 + column);
}

struct HudPlacement {
    bool moved = false;
    HudAnchor anchor = HudAnchor::TopLeft;
    float dx = 0.0f;
    float dy = 0.0f;
    float scale = 1.0f;
    bool visible = true;
    bool operator==(HudPlacement const&) const = default;
};

using HudPlacements = std::map<std::string, HudPlacement, std::less<>>;

struct HudLayoutConfig {
    HudPlacements solo;
    HudPlacements survival;
    HudPlacements& placements(HudMode mode) { return mode == HudMode::Solo ? solo : survival; }
    HudPlacements const& placements(HudMode mode) const { return mode == HudMode::Solo ? solo : survival; }
    bool operator==(HudLayoutConfig const&) const = default;
};

struct HudTransform {
    HudPoint pivot;
    HudPoint position;
    float scale = 1.0f;
    bool visible = true;
    bool identity() const { return pivot == position && scale == 1.0f && visible; }
};

// Pivot `position` (where the authored pivot lands) for an element whose
// customized box is centered at `boxCenter`. The zone of the box center
// picks the anchor so corner elements stay in their corner on other aspect
// ratios (iPad, ultrawide).
inline HudPlacement hudPlacementAt(HudPoint position, HudPoint boxCenter, float width, float height, float scale, bool visible) {
    auto const anchor = hudZone(boxCenter, width, height);
    auto const reference = hudAnchorPoint(anchor, width, height);
    return {
        .moved = true,
        .anchor = anchor,
        .dx = std::clamp(position.x - reference.x, -HudOffsetLimit, HudOffsetLimit),
        .dy = std::clamp(position.y - reference.y, -HudOffsetLimit, HudOffsetLimit),
        .scale = clampHudScale(scale),
        .visible = visible,
    };
}

inline HudTransform resolveHudTransform(HudMode mode, std::string_view id, HudPlacement const* placement, float width, float height) {
    auto const spec = hudElement(mode, id);
    auto const pivot = hudDefaultPivot(mode, id, width, height).value_or(HudPoint{});
    HudTransform result {.pivot = pivot, .position = pivot};
    if (!spec || !placement) return result;
    result.scale = spec->movable ? clampHudScale(placement->scale) : 1.0f;
    result.visible = spec->hideable ? placement->visible : true;
    if (spec->movable && placement->moved && std::isfinite(placement->dx) && std::isfinite(placement->dy)) {
        auto const reference = hudAnchorPoint(placement->anchor, width, height);
        result.position = {
            std::clamp(reference.x + placement->dx, 0.0f, std::max(0.0f, width)),
            std::clamp(reference.y + placement->dy, 0.0f, std::max(0.0f, height)),
        };
    }
    return result;
}

inline HudTransform resolveHudTransform(HudMode mode, std::string_view id, HudLayoutConfig const& config, float width, float height) {
    auto const& placements = config.placements(mode);
    auto const found = placements.find(id);
    return resolveHudTransform(mode, id, found == placements.end() ? nullptr : &found->second, width, height);
}

inline constexpr std::string_view HudLayoutSavedKey = "hud-layout-v1";
inline constexpr int HudLayoutVersion = 1;

inline matjson::Value hudPlacementsJson(HudMode mode, HudPlacements const& placements) {
    auto result = matjson::Value::object();
    for (auto const& [id, placement] : placements) {
        auto const spec = hudElement(mode, id);
        if (!spec) continue;
        auto entry = matjson::Value::object();
        entry["scale"] = static_cast<double>(clampHudScale(placement.scale));
        entry["visible"] = spec->hideable ? placement.visible : true;
        if (placement.moved && spec->movable) {
            entry["anchor"] = std::string(hudAnchorName(placement.anchor));
            entry["dx"] = static_cast<double>(std::round(placement.dx * 10.0f) / 10.0f);
            entry["dy"] = static_cast<double>(std::round(placement.dy * 10.0f) / 10.0f);
        }
        result[id] = entry;
    }
    return result;
}

inline matjson::Value hudLayoutJson(HudLayoutConfig const& config) {
    auto result = matjson::Value::object();
    result["version"] = HudLayoutVersion;
    result["solo"] = hudPlacementsJson(HudMode::Solo, config.solo);
    result["survival"] = hudPlacementsJson(HudMode::Survival, config.survival);
    return result;
}

inline HudPlacements parseHudPlacements(HudMode mode, matjson::Value const& value) {
    HudPlacements result;
    if (!value.isObject()) return result;
    for (auto const& [id, entry] : value) {
        auto const spec = hudElement(mode, id);
        if (!spec || !entry.isObject()) continue;
        HudPlacement placement;
        auto const scale = entry["scale"].asDouble();
        placement.scale = scale.isOk() ? clampHudScale(static_cast<float>(scale.unwrap())) : 1.0f;
        auto const visible = entry["visible"].asBool();
        placement.visible = spec->hideable ? visible.unwrapOr(true) : true;
        auto const anchorName = entry["anchor"].asString();
        auto const dx = entry["dx"].asDouble(), dy = entry["dy"].asDouble();
        if (spec->movable && anchorName.isOk() && dx.isOk() && dy.isOk() &&
            std::isfinite(dx.unwrap()) && std::isfinite(dy.unwrap())) {
            if (auto const anchor = parseHudAnchor(anchorName.unwrap())) {
                placement.moved = true;
                placement.anchor = *anchor;
                placement.dx = std::clamp(static_cast<float>(dx.unwrap()), -HudOffsetLimit, HudOffsetLimit);
                placement.dy = std::clamp(static_cast<float>(dy.unwrap()), -HudOffsetLimit, HudOffsetLimit);
            }
        }
        if (placement != HudPlacement{}) result.emplace(std::string(id), placement);
    }
    return result;
}

// Unknown versions, keys and malformed values fall back to the default
// placement, so a future element or a corrupted save never hides the HUD.
inline HudLayoutConfig parseHudLayout(matjson::Value const& value) {
    HudLayoutConfig result;
    if (!value.isObject() || value["version"].asInt().unwrapOr(0) != HudLayoutVersion) return result;
    result.solo = parseHudPlacements(HudMode::Solo, value["solo"]);
    result.survival = parseHudPlacements(HudMode::Survival, value["survival"]);
    return result;
}

inline HudLayoutConfig parseHudLayoutText(std::string const& text) {
    if (text.empty()) return {};
    auto parsed = matjson::parse(text);
    if (parsed.isErr()) return {};
    return parseHudLayout(parsed.unwrap());
}

enum class HudPreset { Default, Minimal, Focus };

inline constexpr std::array<HudPreset, 3> HudPresets {HudPreset::Default, HudPreset::Minimal, HudPreset::Focus};

inline std::string_view hudPresetKey(HudMode mode, HudPreset preset) {
    switch (preset) {
        case HudPreset::Minimal: return "hud-preset-minimal";
        case HudPreset::Focus: return mode == HudMode::Solo ? "hud-preset-broadcast" : "hud-preset-rank-focus";
        case HudPreset::Default: break;
    }
    return "hud-preset-default";
}

inline std::string_view hudPresetFallback(HudMode mode, HudPreset preset) {
    switch (preset) {
        case HudPreset::Minimal: return "Minimal";
        case HudPreset::Focus: return mode == HudMode::Solo ? "Broadcast" : "Rank focus";
        case HudPreset::Default: break;
    }
    return "Default";
}

inline HudPlacement hudMovedPlacement(HudAnchor anchor, float dx, float dy, float scale = 1.0f) {
    return {.moved = true, .anchor = anchor, .dx = dx, .dy = dy, .scale = scale, .visible = true};
}

inline HudPlacement hudHiddenPlacement() {
    return {.visible = false};
}

inline HudPlacement hudScaledPlacement(float scale) {
    return {.scale = scale};
}

// Presets are anchor relative, so they are valid on every window size.
inline HudPlacements hudPresetPlacements(HudMode mode, HudPreset preset) {
    HudPlacements result;
    if (preset == HudPreset::Default) return result;
    if (mode == HudMode::Solo) {
        if (preset == HudPreset::Minimal) {
            for (auto id : {"fps", "qualifying", "state", "progress", "feed", "round"})
                result.emplace(id, hudHiddenPlacement());
            return result;
        }
        // Broadcast: scores move to the lower corners, the clock grows and the
        // top band stays clear for a facecam/overlay.
        result.emplace("own", hudMovedPlacement(HudAnchor::BottomLeft, 50.0f, 96.0f));
        result.emplace("opponent", hudMovedPlacement(HudAnchor::BottomRight, -50.0f, 96.0f));
        result.emplace("timer", hudScaledPlacement(1.2f));
        result.emplace("feed", hudHiddenPlacement());
        return result;
    }
    if (preset == HudPreset::Minimal) {
        result.emplace("fps", hudHiddenPlacement());
        result.emplace("waiting", hudHiddenPlacement());
        result.emplace("standings", hudScaledPlacement(0.8f));
        result.emplace("rank", hudScaledPlacement(0.7f));
        return result;
    }
    result.emplace("standings", hudScaledPlacement(1.2f));
    result.emplace("rank", hudMovedPlacement(HudAnchor::BottomRight, -80.0f, 12.0f));
    result.emplace("difference", hudMovedPlacement(HudAnchor::Top, 0.0f, -56.0f));
    return result;
}

} // namespace corum::ranked
