#pragma once
#include "../domain/SpectatorWorldEvents.hpp"
#include <limits>
#include <matjson.hpp>
#include <optional>
namespace corum::ranked {
inline matjson::Value encodeSpectatorRandomEvent(SpectatorRandomEvent const &e) {
    matjson::Value j;
    j["ordinal"] = static_cast<std::int64_t>(e.ordinal);
    j["objectIndex"] = e.objectIndex;
    j["objectId"] = e.objectId;
    j["originX"] = e.originX;
    j["originY"] = e.originY;
    j["tick"] = e.tick;
    j["elapsedMs"] = e.elapsedMs;
    j["outputs"] = matjson::Value::array();
    for (auto const &s : e.outputs) {
        matjson::Value o;
        o["group"] = s.group;
        if(s.triggerId)o["triggerId"]=*s.triggerId;
        if(s.controlId)o["controlId"]=*s.controlId;
        o["ordered"] = s.ordered;
        o["delay"] = s.delay;
        o["remap"] = matjson::Value::array();
        for (auto n : s.remap)
            o["remap"].push(n);
        j["outputs"].push(o);
    }
    return j;
}
inline std::optional<SpectatorRandomEvent> decodeSpectatorRandomEvent(matjson::Value const &j) {
    if (!j.isObject())
        return {};
    SpectatorRandomEvent e;
    auto integer = [&](char const *k) { return j[k].asInt().unwrapOr(-1); };
    auto number = [&](char const *k) {
        return j[k].asDouble().unwrapOr(std::numeric_limits<double>::quiet_NaN());
    };
    auto ordinal = integer("ordinal");
    if (ordinal < 1 || ordinal > 1000000 || integer("objectIndex") < 0 || integer("objectIndex") > 1000000 ||
        integer("objectId") < 1 || integer("objectId") > 1000000)
        return {};
    e.ordinal = ordinal;
    e.objectIndex = integer("objectIndex");
    e.objectId = integer("objectId");
    e.originX = number("originX");
    e.originY = number("originY");
    e.tick = integer("tick");
    e.elapsedMs = number("elapsedMs");
    auto outputs = j["outputs"].asArray();
    if (outputs.isErr() || outputs.unwrap().size() > 4)
        return {};
    for (auto const &o : outputs.unwrap()) {
        if (o["ordered"].asBool().isErr())
            return {};
        auto group = o["group"].asInt().unwrapOr(-1);
        if (group < 0 || group > 1000000)
            return {};
        SpectatorRandomSpawn s;
        s.group = group;
        for(auto key:{"triggerId","controlId"})if(o.contains(key)){
            auto value=o[key].asInt();if(value.isErr()||value.unwrap()<std::numeric_limits<int>::min()||value.unwrap()>std::numeric_limits<int>::max())return {};
            (std::string_view(key)=="triggerId"?s.triggerId:s.controlId)=static_cast<int>(value.unwrap());
        }
        s.ordered = o["ordered"].asBool().unwrapOr(false);
        s.delay = o["delay"].asDouble().unwrapOr(-1);
        auto remap = o["remap"].asArray();
        if (remap.isErr() || remap.unwrap().size() > 32)
            return {};
        for (auto const &n : remap.unwrap()) {
            auto value = n.asInt().unwrapOr(-1);
            if (value < 0 || value > 1000000)
                return {};
            s.remap.push_back(value);
        }
        e.outputs.push_back(std::move(s));
    }
    return validSpectatorRandomEvent(e) ? std::optional(e) : std::nullopt;
}
} // namespace corum::ranked
