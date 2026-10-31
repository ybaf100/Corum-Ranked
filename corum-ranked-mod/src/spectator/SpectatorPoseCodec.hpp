#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
#include "SpectatorWorldEventsCodec.hpp"
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
        if(!snap.nativeEvents.empty()){value["discontinuity"]["nativeEvents"]=matjson::Value::array();for(auto const& e:snap.nativeEvents){matjson::Value j;j["type"]=std::string(spectatorDiscontinuityName(e.type));j["fromX"]=e.fromX;j["fromY"]=e.fromY;j["toX"]=e.toX;j["toY"]=e.toY;j["beforeMode"]=std::string(spectatorModeName(e.beforeMode));j["afterMode"]=std::string(spectatorModeName(e.afterMode));j["beforeGravity"]=e.beforeGravity;j["afterGravity"]=e.afterGravity;if(e.trace){auto const&t=*e.trace;j["trace"]["event"]=t.event;j["trace"]["tick"]=t.tick;j["trace"]["step"]=t.step;j["trace"]["elapsedMs"]=t.elapsedMs;j["trace"]["callback"]=t.callback;}value["discontinuity"]["nativeEvents"].push(j);}}
        if (snap.fromCamera && validSpectatorCamera(*snap.fromCamera)) {
            value["discontinuity"]["cameraFromX"] = static_cast<double>(snap.fromCamera->x);
            value["discontinuity"]["cameraFromY"] = static_cast<double>(snap.fromCamera->y);
        }
    }
    value["effects"]=static_cast<int>(pose.effects);
    if(pose.audio){value["audio"]["trackHash"]=pose.audio->trackHash;value["audio"]["positionMs"]=pose.audio->positionMs;}
    if(pose.simulation){auto const& c=*pose.simulation;value["simulation"]["elapsedMs"]=c.elapsedMs;value["simulation"]["tick"]=c.tick;value["simulation"]["started"]=c.started;value["simulation"]["resetDelay"]=c.resetDelay;}
    if(pose.randomEvents&&!pose.randomEvents->empty()){value["randomEvents"]=matjson::Value::array();for(auto const& e:*pose.randomEvents)value["randomEvents"].push(encodeSpectatorRandomEvent(e));}
    if(pose.followHistory){auto const& h=*pose.followHistory;value["followHistory"]["index"]=h.index;value["followHistory"]["remainder"]=h.remainder;value["followHistory"]["y"]=matjson::Value::array();for(auto y:h.y)value["followHistory"]["y"].push(y);}
    if(pose.progress)value["progress"]=static_cast<double>(*pose.progress);
    if(pose.dual && pose.player2)value["player2"]=encodeSpectatorPose(spectatorSecondaryAsPose(*pose.player2));
    return value;
}
/** Diagnostic read of a wire keyframe. Mutable matjson operator[] inserts a
 * missing key as an empty object, so evidence logging must never index a
 * payload that is still going to be sent; that turned an absent
 * discontinuity/player2 into `{}` and made the server reject the batch. */
inline std::string spectatorEvidenceDiscontinuity(matjson::Value const& keyframe,bool player2=false){
    if(!keyframe.isObject())return "none";
    if(player2){
        if(!keyframe.contains("player2"))return "none";
        return spectatorEvidenceDiscontinuity(keyframe["player2"]);
    }
    if(!keyframe.contains("discontinuity"))return "none";
    return keyframe["discontinuity"].dump(matjson::NO_INDENTATION);
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
    if(value.contains("followHistory")){
        auto const& h=value["followHistory"];auto y=h["y"].asArray();auto data=std::make_shared<SpectatorFollowHistory>();data->index=h["index"].asInt().unwrapOr(-1);data->remainder=h["remainder"].asDouble().unwrapOr(nan);
        if(y.isErr()||y.unwrap().size()!=200)return std::nullopt;
        for(std::size_t i=0;i<200;++i)data->y[i]=static_cast<float>(y.unwrap()[i].asDouble().unwrapOr(nan));
        pose.followHistory=std::move(data);
    }
    if(value.contains("randomEvents")){
        auto list=value["randomEvents"].asArray();if(list.isErr()||list.unwrap().size()>16)return std::nullopt;
        auto events=std::make_shared<std::vector<SpectatorRandomEvent>>();std::uint64_t last=0;
        for(auto const& item:list.unwrap()){auto e=decodeSpectatorRandomEvent(item);if(!e||e->ordinal<=last)return std::nullopt;last=e->ordinal;events->push_back(std::move(*e));}pose.randomEvents=std::move(events);
    }
    if(value.contains("simulation")){
        auto const& c=value["simulation"];auto time=c["elapsedMs"].asDouble().unwrapOr(nan);auto tick=c["tick"].asInt().unwrapOr(-1);
        if(!std::isfinite(time)||time<0||time>86400000||tick<0||tick>1000000000||c["started"].asBool().isErr()||c["resetDelay"].asBool().isErr())return std::nullopt;
        pose.simulation=SpectatorSimulationClock{time,tick,c["started"].asBool().unwrapOr(false),c["resetDelay"].asBool().unwrapOr(false)};
    }
    if(value.contains("audio")){
        auto const& a=value["audio"];auto hash=a["trackHash"].asString().unwrapOr("");auto time=a["positionMs"].asDouble().unwrapOr(nan);
        if(hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||!std::isfinite(time)||time<0||time>86400000)return std::nullopt;
        pose.audio=SpectatorAudioSample{hash,time};
    }
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
    if(value.contains("visible") && value["visible"].asBool().isErr())return std::nullopt;
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
        if(evidence.contains("nativeEvents")){auto events=evidence["nativeEvents"].asArray();if(events.isErr()||events.unwrap().empty()||events.unwrap().size()>8)return std::nullopt;
            for(auto const& e:events.unwrap()){auto type=e["type"].asString().unwrapOr("");auto before=parseSpectatorMode(e["beforeMode"].asString().unwrapOr("")),after=parseSpectatorMode(e["afterMode"].asString().unwrapOr(""));if((type!="TELEPORT"&&type!="SPIDER_SNAP")||!before||!after||e["beforeGravity"].asBool().isErr()||e["afterGravity"].asBool().isErr())return std::nullopt;
                snap.nativeEvents.push_back({type=="TELEPORT"?SpectatorDiscontinuity::Teleport:SpectatorDiscontinuity::SpiderSnap,static_cast<float>(e["fromX"].asDouble().unwrapOr(nan)),static_cast<float>(e["fromY"].asDouble().unwrapOr(nan)),static_cast<float>(e["toX"].asDouble().unwrapOr(nan)),static_cast<float>(e["toY"].asDouble().unwrapOr(nan)),*before,*after,e["beforeGravity"].asBool().unwrapOr(false),e["afterGravity"].asBool().unwrapOr(false)});
                if(e.contains("trace")){auto const&t=e["trace"];SpectatorSnapTrace trace{t["event"].asInt().unwrapOr(-1),t["tick"].asInt().unwrapOr(-1),t["step"].asInt().unwrapOr(-1),t["elapsedMs"].asDouble().unwrapOr(nan),t["callback"].asString().unwrapOr("")};if(!validSpectatorSnapTrace(trace))return std::nullopt;snap.nativeEvents.back().trace=std::move(trace);}
            }
        }
        if (!finiteSpectatorSnap(snap)) return std::nullopt;
        SpectatorCameraOrigin sourceCamera {
            static_cast<float>(evidence["cameraFromX"].asDouble().unwrapOr(nan)),
            static_cast<float>(evidence["cameraFromY"].asDouble().unwrapOr(nan))};
        if (validSpectatorCamera(sourceCamera)) snap.fromCamera = sourceCamera;
        pose.discontinuity = snap;
    } else if (evidence.asBool().unwrapOr(false) || !evidence.asString().unwrapOr("").empty()) return std::nullopt;
    if(value.contains("effects")){
        auto mask=value["effects"].asInt().unwrapOr(-1);if(mask<0||mask>32767)return std::nullopt;pose.effects=static_cast<unsigned>(mask);
    }
    if(value.contains("progress"))pose.progress=static_cast<float>(value["progress"].asDouble().unwrapOr(nan));
    if(pose.dual){
        auto p2=value["player2"];if(!p2.isObject() || p2["dual"].asBool().unwrapOr(true) || p2.contains("player2") || p2["visible"].asBool().isErr())return std::nullopt;
        auto decoded=decodeSpectatorPose(p2);if(!decoded)return std::nullopt;
        pose.player2=spectatorAsSecondary(*decoded);
    }else if(value.contains("player2"))return std::nullopt;
    return finiteSpectatorPose(pose) ? std::optional(pose) : std::nullopt;
}
inline matjson::Value encodeSpectatorCosmetics(SpectatorCosmetics const& cosmetics){
    matjson::Value v;v["revision"]=static_cast<std::int64_t>(cosmetics.revision);auto players=matjson::Value::array();
    auto color=[](SpectatorColor const& c){auto a=matjson::Value::array();a.push(c.r);a.push(c.g);a.push(c.b);return a;};
    for(auto const& p:cosmetics.players){matjson::Value a;auto icons=matjson::Value::array();for(auto id:p.icons)icons.push(id);a["icons"]=icons;
        a["primary"]=color(p.primary);a["secondary"]=color(p.secondary);a["glowColor"]=color(p.glowColor);a["glow"]=p.glow;a["trail"]=p.trail;players.push(a);}
    v["players"]=players;return v;
}
inline std::shared_ptr<SpectatorCosmetics const> decodeSpectatorCosmetics(matjson::Value const& v){
    if(!v.isObject())return {};
    SpectatorCosmetics c;auto revision=v["revision"].asInt().unwrapOr(0);if(revision<=0||revision>1000000)return {};c.revision=static_cast<std::uint64_t>(revision);
    auto players=v["players"].asArray();if(players.isErr()||players.unwrap().size()!=2)return {};
    auto color=[](matjson::Value const& v,SpectatorColor& c){auto a=v.asArray();if(a.isErr()||a.unwrap().size()!=3)return false;
        std::array<int,3> bytes{};for(std::size_t i=0;i<3;++i){auto n=a.unwrap()[i].asInt().unwrapOr(-1);if(n<0||n>255)return false;bytes[i]=static_cast<int>(n);}
        c={bytes[0],bytes[1],bytes[2]};return true;};
    for(std::size_t i=0;i<2;++i){auto const& a=players.unwrap()[i];auto& p=c.players[i];auto icons=a["icons"].asArray();
        if(icons.isErr()||icons.unwrap().size()!=8||a["glow"].asBool().isErr())return {};
        for(std::size_t j=0;j<8;++j){auto id=icons.unwrap()[j].asInt().unwrapOr(0);if(id<1||id>10000)return {};p.icons[j]=static_cast<int>(id);}
        p.glow=a["glow"].asBool().unwrapOr(false);auto trail=a["trail"].asInt().unwrapOr(0);if(trail<1||trail>1000)return {};p.trail=static_cast<int>(trail);
        if(!color(a["primary"],p.primary)||!color(a["secondary"],p.secondary)||!color(a["glowColor"],p.glowColor))return {};
    }
    return validSpectatorCosmetics(c)?std::make_shared<SpectatorCosmetics const>(c):nullptr;
}
}
