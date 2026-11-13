#pragma once

#include "../RankedRuntime.hpp"
#include "../domain/MapDifficulty.hpp"
#include "../domain/MapPoolPresentation.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>

// Shared pieces of the Solo map pool page and its map info popup.
namespace corum::ranked::ui::mappool {
using namespace geode::prelude;

inline ccColor3B poolColor(int pool) {
    switch (pool) {
        case 1: return Green;
        case 2: return Accent;
        case 3: return Steel;
        case 4: return Gold;
        case 5: return Orange;
        default: return Red;
    }
}

inline CCLabelBMFont* caption(CCNode* parent, std::string const& text, CCPoint at, float scale, float width,
    ccColor3B color = Text, FontWeight weight = FontWeight::SemiBold, float anchorX = 0.5f) {
    auto* label = menuLabel(text, scale, at, color, weight);
    label->setAnchorPoint({anchorX, 0.5f});
    fitMenuLabelWidth(label, width, scale, scale * 0.55f);
    parent->addChild(label, 6);
    return label;
}

inline CCNode* buttonFace(std::string const& text, CCSize size, ccColor3B accent, bool selected, float scale = 0.24f,
    ccColor3B color = Text) {
    auto* face = surface(size, {}, selected ? Gold : accent, selected ? SurfaceSoft : SurfaceRaised, 250, true);
    auto* label = menuLabel(text, scale, {size.width / 2, size.height / 2}, selected ? Gold : color, FontWeight::ExtraBold);
    fitMenuLabelWidth(label, size.width - 6, scale, scale * 0.55f);
    face->addChild(label, 6);
    return face;
}

// A plain color rectangle: unlike a CCDrawNode it follows lobby opacity fades.
inline CCLayerColor* bar(CCNode* parent, CCRect rect, ccColor3B color, GLubyte opacity = 255, int z = 4) {
    auto* layer = CCLayerColor::create({color.r, color.g, color.b, opacity}, std::max(0.0f, rect.size.width), std::max(0.0f, rect.size.height));
    layer->setPosition(rect.origin);
    parent->addChild(layer, z);
    return layer;
}

inline std::optional<double> number(matjson::Value const& value) {
    if (!value.isNumber()) return std::nullopt;
    auto const result = value.asDouble();
    if (!result.isOk() || !std::isfinite(result.unwrap())) return std::nullopt;
    return result.unwrap();
}
inline int integer(matjson::Value const& value) {
    return static_cast<int>(std::clamp(std::llround(number(value).value_or(0)), -1'000'000'000LL, 1'000'000'000LL));
}
inline std::optional<int> optionalInteger(matjson::Value const& value) {
    auto const result = number(value);
    if (!result) return std::nullopt;
    return static_cast<int>(std::clamp(std::llround(*result), -1'000'000LL, 1'000'000LL));
}
inline std::string text(matjson::Value const& value, std::size_t limit = 120) {
    auto result = value.asString().unwrapOr("");
    if (result.size() > limit) result.resize(limit);
    return result;
}

inline MapPoolRow parseRow(matjson::Value const& value) {
    MapPoolRow row;
    row.canonicalLevelId = text(value["canonicalLevelId"], 16);
    row.levelId = text(value["levelId"], 16);
    row.title = text(value["title"]);
    row.creator = text(value["creator"]);
    row.difficulty = text(value["difficulty"], 32);
    row.gdDifficulty = text(value["gdDifficulty"], 32);
    row.rating = number(value["rating"]);
    row.kind = text(value["kind"], 16);
    row.gddlColor = text(value["gddlColor"], 16);
    row.difficultyRank = integer(value["difficultyRank"]);
    row.pool = std::clamp(integer(value["pool"]), 1, 6);
    row.qualifyingPercent = number(value["qualifyingPercent"]).value_or(0);
    row.stars = optionalInteger(value["stars"]);
    row.gddlTier = optionalInteger(value["gddlTier"]);
    row.durationSeconds = number(value["durationSeconds"]);
    row.rounds = std::max(0, integer(value["rounds"]));
    row.draws = std::max(0, integer(value["draws"]));
    row.banOffers = std::max(0, integer(value["banOffers"]));
    row.bans = std::max(0, integer(value["bans"]));
    row.share = number(value["share"]);
    row.drawRate = number(value["drawRate"]);
    row.banRate = number(value["banRate"]);
    return row;
}

inline MapMetadata metadataOf(MapPoolRow const& row) {
    MapMetadata metadata;
    metadata.kind = row.kind.empty() ? "UNKNOWN" : row.kind;
    metadata.stars = row.stars;
    metadata.gddlTier = row.gddlTier;
    metadata.gddlColor = row.gddlColor;
    if (row.durationSeconds) metadata.durationSeconds = *row.durationSeconds;
    return metadata;
}

// The GD face and name come from the GD label ("Extreme Demon"). The sheet's
// difficulty is a numeric Corum rating and never names a face.
inline std::optional<MapDifficulty> displayedDifficulty(MapPoolRow const& row) {
    return row.gdDifficulty.empty() ? std::nullopt : recordedMapDifficulty(row.gdDifficulty);
}

inline web::WebRequest request() {
    web::WebRequest request;[](auto& q){if constexpr(requires{q.getHttpVersion();})q.version(decltype(q.getHttpVersion())::VERSION_2TLS);}(request); /* HTTP/2: see PREFER_HTTP2 note in RankedRuntime.cpp */
    auto const& token = RankedRuntime::get().sessionToken();
    if (!token.empty()) request.header("Authorization", "Bearer " + token);
    request.followRedirects(false);
    request.timeout(std::chrono::seconds(20));
    return request;
}

inline int levelNumber(std::string const& id) {
    int value = 0;
    auto const result = std::from_chars(id.data(), id.data() + id.size(), value);
    return result.ec == std::errc{} && result.ptr == id.data() + id.size() && value > 0 ? value : 0;
}

inline std::string sortLabel(MapPoolSort sort) {
    switch (sort) {
        case MapPoolSort::Difficulty: return i18n::text("map-pool-sort-difficulty", "DIFFICULTY");
        case MapPoolSort::Share: return i18n::text("map-pool-sort-share", "PLAYED");
        case MapPoolSort::Draw: return i18n::text("map-pool-sort-draw", "DRAWS");
        case MapPoolSort::Ban: return i18n::text("map-pool-sort-ban", "BANS");
        case MapPoolSort::Rounds: return i18n::text("map-pool-sort-rounds", "ROUNDS");
    }
    return {};
}
inline char const* sortKey(MapPoolSort sort) {
    switch (sort) {
        case MapPoolSort::Difficulty: return "difficulty";
        case MapPoolSort::Share: return "share";
        case MapPoolSort::Draw: return "draw";
        case MapPoolSort::Ban: return "ban";
        case MapPoolSort::Rounds: return "rounds";
    }
    return "difficulty";
}

} // namespace corum::ranked::ui::mappool
