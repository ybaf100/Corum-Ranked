#pragma once
#include "RoundResourcePolicy.hpp"
#include <string_view>
namespace corum::ranked {
enum class RoundAudioCategory { Permanent, Transient, Timeout, Metadata, Cache, Storage, Unknown };
struct RoundAudioFailure {
    RoundResourceKey resource{RoundResourceKind::Song,0};
    RoundAudioCategory category=RoundAudioCategory::Unknown;
    int nativeCode=0;
};
// Pinned Geode 5.8.2 GJSongError: FailedToFetch=1, NotAllowed=2,
// DownloadCancelled=3. Generic fetch failures do NOT prove a removed song,
// unavailable storage or a permanent NG restriction.
inline RoundAudioCategory classifyRoundAudioFailure(RoundResourceKind kind,int code,bool successWithoutFile=false){
    if(successWithoutFile)return RoundAudioCategory::Cache;
    if(code==2)return RoundAudioCategory::Permanent;
    if(code==1)return kind==RoundResourceKind::SongInfo?RoundAudioCategory::Metadata:RoundAudioCategory::Transient;
    if(code==3)return RoundAudioCategory::Transient;
    return RoundAudioCategory::Unknown;
}
inline std::string_view roundAudioCategoryName(RoundAudioCategory category){
    switch(category){
        case RoundAudioCategory::Permanent:return "AUDIO_UNAVAILABLE_PERMANENT";
        case RoundAudioCategory::Transient:return "AUDIO_DOWNLOAD_TRANSIENT";
        case RoundAudioCategory::Timeout:return "AUDIO_DOWNLOAD_TIMEOUT";
        case RoundAudioCategory::Metadata:return "AUDIO_METADATA_UNAVAILABLE";
        case RoundAudioCategory::Cache:return "AUDIO_CACHE_INVALID";
        case RoundAudioCategory::Storage:return "AUDIO_STORAGE_FAILURE";
        default:return "AUDIO_UNKNOWN_ERROR";
    }
}
}
