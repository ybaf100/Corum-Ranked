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
    value["visible"] = pose.visible;
    if (pose.camera && validSpectatorCamera(*pose.camera)) {
        value["cameraX"] = static_cast<double>(pose.camera->x);
        value["cameraY"] = static_cast<double>(pose.camera->y);
        value["cameraSnap"] = pose.cameraSnap;
        if(pose.camera->transform && validSpectatorCameraTransform(*pose.camera->transform)){
            auto const& t=*pose.camera->transform;value["cameraZoom"]=static_cast<double>(t.zoom);value["cameraAngle"]=static_cast<double>(t.angle);
            auto layers=matjson::Value::array();
            for(auto const& l:t.layers){matjson::Value item;item["present"]=l.present;item["scaleX"]=static_cast<double>(l.scaleX);item["scaleY"]=static_cast<double>(l.scaleY);item["rotation"]=static_cast<double>(l.rotation);layers.push(item);}
            value["cameraLayers"]=layers;
        }
    }
    if (pose.discontinuity && finiteSpectatorSnap(*pose.discontinuity)) {
        auto const& snap = *pose.discontinuity;
        value["discontinuity"]["type"] = std::string(spectatorDiscontinuityName(snap.type));
        value["discontinuity"]["fromX"] = static_cast<double>(snap.fromX);
        value["discontinuity"]["fromY"] = static_cast<double>(snap.fromY);
        value["discontinuity"]["toX"] = static_cast<double>(snap.toX);
        value["discontinuity"]["toY"] = static_cast<double>(snap.toY);
        if (snap.fromCamera && validSpectatorCamera(*snap.fromCamera)) {
            value["discontinuity"]["cameraFromX"] = static_cast<double>(snap.fromCamera->x);
            value["discontinuity"]["cameraFromY"] = static_cast<double>(snap.fromCamera->y);
        }
    }
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
    pose.visible = value["visible"].asBool().unwrapOr(true);
    SpectatorCameraOrigin camera{
        static_cast<float>(value["cameraX"].asDouble().unwrapOr(nan)),
        static_cast<float>(value["cameraY"].asDouble().unwrapOr(nan))};
    if (validSpectatorCamera(camera)) pose.camera = camera;
    if(value.contains("cameraZoom") || value.contains("cameraAngle") || value.contains("cameraLayers")){
        SpectatorCameraTransform t;t.zoom=static_cast<float>(value["cameraZoom"].asDouble().unwrapOr(nan));t.angle=static_cast<float>(value["cameraAngle"].asDouble().unwrapOr(nan));
        auto layers=value["cameraLayers"].asArray();if(layers.isErr() || layers.unwrap().size()!=3)return std::nullopt;
        for(std::size_t i=0;i<3;++i){auto const& l=layers.unwrap()[i];if(l["present"].asBool().isErr())return std::nullopt;
            t.layers[i]={l["present"].asBool().unwrapOr(false),static_cast<float>(l["scaleX"].asDouble().unwrapOr(nan)),static_cast<float>(l["scaleY"].asDouble().unwrapOr(nan)),static_cast<float>(l["rotation"].asDouble().unwrapOr(nan))};}
        if(!pose.camera || !validSpectatorCameraTransform(t))return std::nullopt;
        pose.camera->transform=t;
    }
    pose.cameraInvalid = !pose.camera && (value.contains("cameraX") || value.contains("cameraY"));
    pose.cameraSnap = pose.camera.has_value() && value["cameraSnap"].asBool().unwrapOr(false);
    auto const evidence = value["discontinuity"];
    if (evidence.isObject()) {
        auto const type = evidence["type"].asString().unwrapOr("");
        if (type != "TELEPORT" && type != "SPIDER_SNAP") return std::nullopt;
        SpectatorSnapEvidence snap{
            type == "TELEPORT" ? SpectatorDiscontinuity::Teleport : SpectatorDiscontinuity::SpiderSnap,
            static_cast<float>(evidence["fromX"].asDouble().unwrapOr(nan)),
            static_cast<float>(evidence["fromY"].asDouble().unwrapOr(nan)),
            static_cast<float>(evidence["toX"].asDouble().unwrapOr(nan)),
            static_cast<float>(evidence["toY"].asDouble().unwrapOr(nan))};
        if (!finiteSpectatorSnap(snap)) return std::nullopt;
        SpectatorCameraOrigin sourceCamera {
            static_cast<float>(evidence["cameraFromX"].asDouble().unwrapOr(nan)),
            static_cast<float>(evidence["cameraFromY"].asDouble().unwrapOr(nan))};
        if (validSpectatorCamera(sourceCamera)) snap.fromCamera = sourceCamera;
        pose.discontinuity = snap;
    } else if (evidence.asBool().unwrapOr(false) || !evidence.asString().unwrapOr("").empty()) return std::nullopt;
    return finiteSpectatorPose(pose) ? std::optional(pose) : std::nullopt;
}
}
