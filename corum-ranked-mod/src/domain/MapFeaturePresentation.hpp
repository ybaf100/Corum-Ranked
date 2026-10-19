#pragma once
#include "MapMetadata.hpp"
#include <string_view>
namespace corum::ranked {
inline std::string_view mapFeatureGrade(MapMetadata const& metadata){
    if(metadata.epic.value_or(0)>=3)return "MYTHIC";
    if(metadata.epic.value_or(0)==2)return "LEGENDARY";
    if(metadata.epic.value_or(0)==1)return "EPIC";
    if(metadata.featureScore.value_or(0)>0)return "FEATURED";
    if(metadata.kind=="UNKNOWN" && !metadata.stars && !metadata.featureScore && !metadata.epic)return "RATING PENDING";
    return metadata.stars.value_or(0)>0?"RATED":"UNRATED";
}
}
