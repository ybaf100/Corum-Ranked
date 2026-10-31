#pragma once
#include "SurvivalReplayCompatibility.hpp"
#include <matjson.hpp>
#include <string>
namespace corum::ranked::replay {
/** History-screen replay state. Derived only from stored evidence: the
 * manifest receipt (officialCompletion), the sealed footer
 * (captureCompleteness), the pending journal and the physics environment
 * check. Never upgrades a partial or unconfirmed capture to a full replay. */
enum class ReplayQuality{CompleteVerified,PartialRecovered,OfficialPrefix,WaitingReceipt,Unavailable,Damaged,Incompatible,NotOnDevice};
/** P2-B compatibility class shown next to INCOMPATIBLE / UNAVAILABLE. */
enum class ReplayCompatibility{Supported,UnsupportedSetting,VersionMismatch,MissingNativeEvidence,HookUnavailable};
inline char const* replayCompatibilityName(ReplayCompatibility c){
    switch(c){case ReplayCompatibility::Supported:return "SUPPORTED";case ReplayCompatibility::UnsupportedSetting:return "UNSUPPORTED_SETTING";
      case ReplayCompatibility::VersionMismatch:return "VERSION_MISMATCH";case ReplayCompatibility::MissingNativeEvidence:return "MISSING_NATIVE_EVIDENCE";
      case ReplayCompatibility::HookUnavailable:return "HOOK_UNAVAILABLE";}return "MISSING_NATIVE_EVIDENCE";
}
/** Maps the existing exact problem codes; unknown codes fail closed as missing evidence. */
inline ReplayCompatibility replayCompatibilityClass(std::string const& problem){
    if(problem.empty())return ReplayCompatibility::Supported;
    if(problem=="CBF_PHYSICS_BYPASS_UNSUPPORTED"||problem=="CBF_COS_PIPELINE_UNSUPPORTED"||problem=="CBF_ACCURACY_MODE_MISMATCH"||problem=="CBF_SETTINGS_MISMATCH")
        return ReplayCompatibility::UnsupportedSetting;
    if(problem=="CBF_VERSION_UNVERIFIED"||problem=="CBF_NATIVE_DRIVER_MISMATCH"||problem=="GAME_VERSION_MISMATCH"||problem=="GEODE_VERSION_MISMATCH")
        return ReplayCompatibility::VersionMismatch;
    if(problem.starts_with("HOOK_")||problem=="CBF_REPLAY_HOOKS_UNAVAILABLE")return ReplayCompatibility::HookUnavailable;
    return ReplayCompatibility::MissingNativeEvidence;
}
struct ReplayQualityView{ReplayQuality quality=ReplayQuality::NotOnDevice;bool playable=false;std::string reason;ReplayCompatibility compatibility=ReplayCompatibility::Supported;};
inline char const* replayQualityName(ReplayQuality q){
    switch(q){case ReplayQuality::CompleteVerified:return "COMPLETE_VERIFIED";case ReplayQuality::PartialRecovered:return "PARTIAL_RECOVERED";
      case ReplayQuality::OfficialPrefix:return "OFFICIAL_PREFIX";case ReplayQuality::WaitingReceipt:return "WAITING_RECEIPT";
      case ReplayQuality::Unavailable:return "UNAVAILABLE";case ReplayQuality::Damaged:return "DAMAGED";
      case ReplayQuality::Incompatible:return "INCOMPATIBLE";case ReplayQuality::NotOnDevice:return "NOT_ON_THIS_DEVICE";}return "UNAVAILABLE";
}
/**
 * state: {"damaged":string?, "best":metadata?, "pending":{captureCompleteness,replayValidationStatus}?,
 *         "initialProblem":string?}. identity: the official history row.
 * environmentProblem: replayPhysicsEnvironmentMismatch(stored, current) evaluated on this device.
 */
inline ReplayQualityView classifyReplayQuality(matjson::Value const& state,matjson::Value const& identity,std::string const& environmentProblem){
    ReplayQualityView v;
    if(auto damaged=state["damaged"].asString().unwrapOr("");!damaged.empty()){v.quality=ReplayQuality::Damaged;v.reason=damaged;return v;}
    auto const& best=state["best"];
    if(!best.isObject()){
        auto const& pending=state["pending"];
        if(pending.isObject()){
            auto status=pending["replayValidationStatus"].asString().unwrapOr("AWAITING_OFFICIAL_RECEIPT");
            // Only a genuinely unconfirmed capture is "waiting"; a final mismatch is unavailable.
            v.quality=status=="AWAITING_OFFICIAL_RECEIPT"?ReplayQuality::WaitingReceipt:ReplayQuality::Unavailable;v.reason=status;return v;}
        v.quality=ReplayQuality::NotOnDevice;return v;
    }
    for(auto key:{"attemptId","mapGeometrySha256","mapVersion","levelId"})if(best[key]!=identity[key]){v.quality=ReplayQuality::Unavailable;v.reason="NOT_THE_OFFICIAL_BEST_ATTEMPT";return v;}
    if(auto p=state["initialProblem"].asString().unwrapOr("");!p.empty()){v.quality=ReplayQuality::Unavailable;v.reason=p;v.compatibility=replayCompatibilityClass(p);return v;}
    if(!environmentProblem.empty()){v.quality=ReplayQuality::Incompatible;v.reason=environmentProblem;v.compatibility=replayCompatibilityClass(environmentProblem);return v;}
    v.playable=true;
    auto completion=best["officialCompletion"].asString().unwrapOr("");
    if(best["captureCompleteness"].asString().unwrapOr("")=="PARTIAL_RECOVERED")v.quality=ReplayQuality::PartialRecovered;
    else if(completion=="OFFICIAL_PREFIX")v.quality=ReplayQuality::OfficialPrefix;
    else if(completion=="FULL_CAPTURE")v.quality=ReplayQuality::CompleteVerified;
    // Legacy manifest without a completion label: playable, but not claimed verified-complete.
    else {v.quality=ReplayQuality::OfficialPrefix;v.reason="LEGACY_RECEIPT_WITHOUT_COMPLETION";}
    return v;
}
}
