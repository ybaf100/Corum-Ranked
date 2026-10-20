#pragma once
#include "MapMetadata.hpp"
#include <matjson.hpp>
#include <array>
namespace corum::ranked {
// Same exact-ID/rating bounds and GDDL reform palette used by the server's
// existing map-metadata resolver. Display only; never a map/score certificate.
inline bool applyGddlMetadata(matjson::Value const& body,int levelId,MapMetadata& metadata){
    if(levelId<=0 || !body["ID"].isNumber() || body["ID"].asInt().unwrapOr(0)!=levelId)return false;
    auto raw=body["Rating"].asDouble().unwrapOr(0);
    if(!std::isfinite(raw) || raw<=0)raw=body["DefaultRating"].asDouble().unwrapOr(0);
    if(!std::isfinite(raw) || raw<.5 || raw>=40.5)return false;
    auto const tier=static_cast<int>(std::round(raw));
    constexpr std::array<char const*,40> colors{
        "#dddfee","#d5d3e9","#d3cbe7","#d3c3e4","#d4bbe2","#d5b0de","#dba7dc","#db9fd1",
        "#d991c1","#da86b0","#db7698","#dc6a7d","#dd5a5a","#dc514c","#da493e","#da4534",
        "#d93e26","#dc3f23","#c03a1b","#b23415","#a23010","#9b2b0c","#932b0b","#892608",
        "#832607","#752105","#6c1e04","#601a02","#5a1802","#521700","#4f0a01","#4a0207",
        "#470313","#42041e","#400427","#3d0630","#380635","#300636","#250630","#1c072e"};
    metadata.gddlTier=tier;metadata.gddlColor=colors[tier-1];return true;
}
}
