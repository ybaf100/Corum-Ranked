#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
#include <matjson.hpp>
#include <limits>

namespace corum::ranked {
inline matjson::Value encodeSpectatorPose(SpectatorGhostPose const& pose) {
    matjson::Value value;
    value["x"] = static_cast<double>(pose.x); value["y"] = static_cast<double>(pose.y);
    value["rotation"] = static_cast<double>(pose.rotation);
    value["mode"] = std::string(spectatorModeName(pose.mode));
    value["mini"] = pose.mini; value["upsideDown"] = pose.upsideDown;
    value["flipped"] = pose.flipped; value["dual"] = pose.dual;
    value["dead"] = pose.dead; value["finished"] = pose.finished;
    return value;
}
inline std::optional<SpectatorGhostPose> decodeSpectatorPose(matjson::Value const& value) {
    if (!value.isObject()) return std::nullopt;
    auto const mode = parseSpectatorMode(value["mode"].asString().unwrapOr(""));
    for (auto key : {"mini", "upsideDown", "flipped", "dual", "dead", "finished"}) {
        if (value[key].asBool().isErr()) return std::nullopt;
    }
    if (!mode) return std::nullopt;
    auto const nan = std::numeric_limits<double>::quiet_NaN();
    SpectatorGhostPose pose;
    pose.x = static_cast<float>(value["x"].asDouble().unwrapOr(nan));
    pose.y = static_cast<float>(value["y"].asDouble().unwrapOr(nan));
    pose.rotation = static_cast<float>(value["rotation"].asDouble().unwrapOr(nan));
    pose.mode = *mode;
    pose.mini = value["mini"].asBool().unwrapOr(false);
    pose.upsideDown = value["upsideDown"].asBool().unwrapOr(false);
    pose.flipped = value["flipped"].asBool().unwrapOr(false);
    pose.dual = value["dual"].asBool().unwrapOr(false);
    pose.dead = value["dead"].asBool().unwrapOr(false);
    pose.finished = value["finished"].asBool().unwrapOr(false);
    return finiteSpectatorPose(pose) ? std::optional(pose) : std::nullopt;
}
}
