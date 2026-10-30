#pragma once
#include "SurvivalBadgePresentation.hpp"
#include <matjson.hpp>
#include <cstdint>
#include <vector>
namespace corum::ranked {
inline std::vector<matjson::Value> survivalBadgeCollectionRows(matjson::Value const& data){
    return data["difficulties"].asArray().unwrapOr(std::vector<matjson::Value>{});
}
inline std::int64_t survivalBadgeCollectionTotal(matjson::Value const& counts){
    std::int64_t total=0;
    for(auto grade:SurvivalBadgeGrades)if(grade!="ELIMINATED")total+=counts[std::string(grade)].asInt().unwrapOr(0);
    return total;
}
inline bool survivalBadgeCountsValid(matjson::Value const& counts){
    if(!counts.isObject())return false;
    for(auto grade:SurvivalBadgeGrades){auto n=counts[std::string(grade)].asInt();if(n.isErr()||n.unwrap()<0)return false;}
    return true;
}
inline bool survivalBadgeCollectionValid(matjson::Value const& data){
    if(!data["difficulties"].isArray()||!data["eliminatedByDifficulty"].isArray()||!survivalBadgeCountsValid(data["totals"]))return false;
    for(auto const& d:survivalBadgeCollectionRows(data))if(!d["id"].isString()||!d["name"].isString()||!d["nativeCode"].isNumber()||!survivalBadgeCountsValid(d["counts"]))return false;
    return true;
}
}
