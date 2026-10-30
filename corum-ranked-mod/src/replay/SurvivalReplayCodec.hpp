#pragma once
#include "SurvivalReplayPolicy.hpp"
#include "SurvivalReplayPlayerState.hpp"
#include "SurvivalReplayCompatibility.hpp"
#include "SurvivalReplayCbfTrace.hpp"
#include "../domain/Sha256.hpp"
#include <matjson.hpp>
#include <fstream>
#include <vector>
#include <filesystem>
#include <optional>
#include <charconv>
#include "../spectator/SpectatorPoseCodec.hpp"
#include <stdexcept>
namespace corum::ranked::replay {
using Json=matjson::Value;
inline std::string str(Json const& j,char const* key){return j[key].asString().unwrapOr("");}
inline std::int64_t integer(Json const& j,char const* key){return j[key].asInt().unwrapOr(-1);}
inline double number(Json const& j,char const* key){return j[key].asDouble().unwrapOr(-1);}
inline bool hashValid(std::string const& h){return h.size()==64&&std::all_of(h.begin(),h.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
struct Document {
    Json metadata,footer=nullptr;
    std::vector<Json> records;
    std::string chain;
    std::uintmax_t validBytes=0;
    bool complete=false;
};
inline std::string metadataProblem(Json const& j){
    if(integer(j,"formatVersion")!=FormatVersion)return "formatVersion";
    if(str(j,"gameVersion")!="2.2081")return "gameVersion";
    for(auto key:{"matchId","roundKey","participantId","attemptId"})if(str(j,key).empty()||str(j,key).size()>256)return key;
    for(auto key:{"mapGeneration","levelId","mapVersion"})if(integer(j,key)<=0)return key;
    if(!hashValid(str(j,"mapGeometrySha256")))return "mapGeometrySha256";
    if(!j["nativeInputVersion"].isNull()){
        auto version=integer(j,"nativeInputVersion");
        if(version<1||version>3)return "nativeInputVersion";
        if(version==3&&(str(j,"accuracyMode")!="CBF_NATIVE_BOUNDARIES_V1"||!replayCbfActive(j["physicsEnvironment"])||!replayPhysicsEnvironmentProblem(j["physicsEnvironment"]).empty()))return "CBF_ACCURACY_PROFILE";
    }
    return {};
}
inline bool metadataValid(Json const& j){return metadataProblem(j).empty();}
inline bool replayButtonCommandValid(Json const& r){
    return integer(r,"button")>=1&&integer(r,"button")<=3&&r["down"].isBool()&&r["player2"].isBool()&&
        integer(r,"nativeStep")>=0&&integer(r,"nativeStep")<=std::numeric_limits<int>::max()&&r["timestamp"].isNumber()&&std::isfinite(number(r,"timestamp"));
}
inline bool replayButtonQueueValid(Json const& value){
    auto values=value.asArray();return values.isOk()&&values.unwrap().size()<=4096&&std::all_of(values.unwrap().begin(),values.unwrap().end(),replayButtonCommandValid);
}
template<class Commands> Json encodeReplayButtons(Commands const& commands){
    auto values=Json::array();for(auto const& c:commands){Json v;v["button"]=static_cast<int>(c.m_button);v["down"]=c.m_isPush;v["player2"]=c.m_isPlayer2;v["nativeStep"]=c.m_step;v["timestamp"]=c.m_timestamp;values.push(std::move(v));}return values;
}
template<class Command> Command decodeReplayButton(Json const& input){
    if(!replayButtonCommandValid(input))throw std::runtime_error("Invalid native replay command");
    Command c;c.m_button=static_cast<decltype(c.m_button)>(integer(input,"button"));c.m_isPush=input["down"].asBool().unwrap();c.m_isPlayer2=input["player2"].asBool().unwrap();c.m_step=static_cast<int>(integer(input,"nativeStep"));c.m_timestamp=number(input,"timestamp");return c;
}
inline bool replayDispatchValid(Json const& value){
    return replayButtonCommandValid(value)&&(value["before"].isArray()||value["before"].isNull())&&(value["after"].isArray()||value["after"].isNull());
}
inline std::string replayInitialDataProblem(Json const& initial){
    if(integer(initial,"physicsSchema")!=ReplayPhysicsSchema)return "LEGACY_INITIAL_PHYSICS_UNAVAILABLE";
    if(auto problem=replayPlayerDataProblem(initial["player1"]);!problem.empty())return problem;
    if(!initial["player2"].isNull())if(auto problem=replayPlayerDataProblem(initial["player2"]);!problem.empty())return problem;
    if(!replayButtonQueueValid(initial["queued"]))return "INITIAL_QUEUE_UNAVAILABLE";
    for(auto key:{"nativeStep","resumeTimer"})if(!replayPhysicsFits<int>(initial[key]))return "INITIAL_CLOCK_UNAVAILABLE";
    for(auto key:{"started","inResetDelay","dual","clickBetweenSteps","clickOnSteps","isBetweenSteps"})if(!initial[key].isBool())return "INITIAL_CLOCK_UNAVAILABLE";
    if(initial["dual"].asBool().unwrap()&&initial["player2"].isNull())return "INITIAL_DUAL_PLAYER_UNAVAILABLE";
    for(auto key:{"attemptTime","levelTime","extraDelta","timestamp","timeWarp"})if(!replayPhysicsFits<double>(initial[key]))return "INITIAL_CLOCK_UNAVAILABLE";
    if(number(initial,"timeWarp")<=0||number(initial,"attemptTime")<0||number(initial,"levelTime")<0)return "INITIAL_CLOCK_INVALID";
    return {};
}
inline bool cbfInitialDataValid(Json const& value){
    if(integer(value,"schema")!=CbfBoundarySchema||!value["players"].isArray()||value["players"].size()!=2)return false;
    for(auto const& player:value["players"]){
        if(!replayPhysicsPointValid(player["lastPosition"])||!player["collisionLogs"].isArray()||player["collisionLogs"].size()!=4)return false;
        for(auto const& count:player["collisionLogs"])if(count.asInt().unwrapOr(-1)!=0)return false;
    }
    return true;
}
inline bool recordValid(Json const& r){
    auto kind=str(r,"kind");if(integer(r,"tick")<0||!std::isfinite(number(r,"elapsed"))||number(r,"elapsed")<0)return false;
    if(kind=="cbf")return cbfFrameValid(r);
    if(kind=="buttons"){
        if(!r["clear"].isBool()||integer(r,"nativeStep")<0||!std::isfinite(number(r,"dt"))||number(r,"dt")<0||number(r,"dt")>1)return false;
        for(auto key:{"queued","remaining"}){auto values=r[key].asArray();if(values.isErr()||values.unwrap().size()>4096)return false;
            for(auto const& input:values.unwrap())if(!replayButtonCommandValid(input))return false;}
        return true;
    }
    if(kind=="input")return replayButtonCommandValid(r);
    if(kind=="dispatch")return replayDispatchValid(r);
    if(kind=="frame"||kind=="step"){
        if(!std::isfinite(number(r,"dt"))||(kind=="frame"?number(r,"dt")<=0:number(r,"dt")<0)||number(r,"dt")>1)return false;
        if(kind=="step")return r["half"].isBool()&&r["last"].isBool()&&integer(r,"nativeStep")>=0;
        if(!r["queued"].isNull()&&!replayButtonQueueValid(r["queued"]))return false;
        if(!r["preInputs"].isNull()){auto inputs=r["preInputs"].asArray();if(inputs.isErr()||inputs.unwrap().size()>4096||!std::all_of(inputs.unwrap().begin(),inputs.unwrap().end(),replayDispatchValid))return false;}
        return true;
    }
    if(kind=="pose")return decodeSpectatorPose(r).has_value()&&number(r,"progress")>=0&&number(r,"progress")<=100;
    if(kind=="initial"){
        for(auto key:{"randomSeed","replayRandSeed"}){auto text=str(r,key);std::uint64_t n=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return false;}
        return (r["physicsSchema"].isNull()||replayInitialDataProblem(r).empty())&&(r["cbfInitial"].isNull()||cbfInitialDataValid(r["cbfInitial"]));
    }
    if(kind=="random")return decodeSpectatorRandomEvent(r["event"]).has_value();
    if(kind=="anchor")return (str(r,"event")=="TELEPORT"||str(r,"event")=="SPIDER_SNAP")&&r["player2"].isBool()&&
      std::isfinite(number(r,"fromX"))&&std::isfinite(number(r,"fromY"))&&std::isfinite(number(r,"toX"))&&std::isfinite(number(r,"toY"));
    if(kind=="terminal")return !str(r,"reason").empty()&&number(r,"progress")>=0&&number(r,"progress")<=100;
    return false;
}
inline Json block(std::vector<Json> const& records,std::string const& previous,std::size_t sequence){
    Json j;j["sequence"]=static_cast<std::int64_t>(sequence);j["previous"]=previous;j["records"]=Json::array();
    for(auto const&r:records){if(!recordValid(r))throw std::runtime_error("Invalid replay record");j["records"].push(r);}
    j["sha256"]=sha256Text(previous+j["records"].dump(matjson::NO_INDENTATION));return j;
}
inline void refuseReplaySymlinks(std::filesystem::path const& path){
    std::filesystem::path at;for(auto const& part:path){at/=part;std::error_code ec;if(std::filesystem::is_symlink(std::filesystem::symlink_status(at,ec)))throw std::runtime_error("Replay symlink refused");}
}
inline Document decode(std::filesystem::path const& path,bool recover=false){
    refuseReplaySymlinks(path);
    std::error_code ec;auto bytes=std::filesystem::file_size(path,ec);if(ec||bytes>MaxFileBytes)throw std::runtime_error("Replay missing or too large");
    std::ifstream in(path,std::ios::binary);Document d;std::string line;std::size_t sequence=0;bool first=true;
    while(std::getline(in,line)){
        if(in.eof()||line.size()>2*1024*1024){if(recover)break;throw std::runtime_error("Replay block boundary damaged");}
        auto parsed=Json::parse(line);if(!parsed){if(recover)break;throw std::runtime_error("Replay JSON damaged");}auto j=parsed.unwrap();
        if(first){first=false;d.metadata=j;if(!metadataValid(j))throw std::runtime_error("Replay identity damaged");d.chain=sha256Text(j.dump(matjson::NO_INDENTATION));}
        else if(j["footer"].asBool().unwrapOr(false)){
            if(d.complete||str(j,"payloadSha256")!=d.chain||integer(j,"recordCount")!=static_cast<std::int64_t>(d.records.size())||
              d.records.empty()||str(d.records.back(),"kind")!="terminal"){if(recover)break;throw std::runtime_error("Replay footer damaged");}
            auto footerDigest=str(j,"footerSha256");auto summary=j;summary.erase("footerSha256");
            if(footerDigest!=sha256Text(summary.dump(matjson::NO_INDENTATION))||integer(j,"payloadBytes")!=static_cast<std::int64_t>(d.validBytes)||
              (str(j,"captureCompleteness")!="COMPLETE"&&str(j,"captureCompleteness")!="PARTIAL_RECOVERED")){if(recover)break;throw std::runtime_error("Replay summary damaged");}
            d.footer=j;d.complete=true;
        }else{
            if(d.complete||integer(j,"sequence")!=static_cast<std::int64_t>(sequence)||str(j,"previous")!=d.chain||!j["records"].isArray()){
                if(recover){break;}throw std::runtime_error("Replay chain damaged");}
            auto records=j["records"].asArray().unwrap();
            if(records.empty()||records.size()>MaxBlockRecords||d.records.size()+records.size()>MaxRecords||str(j,"sha256")!=sha256Text(d.chain+j["records"].dump(matjson::NO_INDENTATION))||
                !std::all_of(records.begin(),records.end(),recordValid)){if(recover)break;throw std::runtime_error("Replay block damaged");}
            auto lastTick=d.records.empty()?0:integer(d.records.back(),"tick");auto lastTime=d.records.empty()?0:number(d.records.back(),"elapsed");
            bool invalid=false,terminal=!d.records.empty()&&str(d.records.back(),"kind")=="terminal";
            auto offset=d.records.size();
            for(auto const& rec:records){
              auto kind=str(rec,"kind");if(terminal||(offset==0&&kind!="initial")||(offset>0&&kind=="initial")||integer(rec,"tick")<lastTick||number(rec,"elapsed")<lastTime){invalid=true;break;}
              terminal=kind=="terminal";lastTick=integer(rec,"tick");lastTime=number(rec,"elapsed");++offset;
            }
            if(invalid){if(recover)break;throw std::runtime_error("Replay timeline damaged");}
            d.chain=str(j,"sha256");d.records.insert(d.records.end(),records.begin(),records.end());++sequence;
        }
        d.validBytes+=line.size()+1;
    }
    if(first||(!recover&&!d.complete))throw std::runtime_error("Replay incomplete");
    return d;
}
inline std::string roundStorageKey(Json const& metadata){
    return sha256Text(str(metadata,"matchId")+"\n"+str(metadata,"roundKey")+"\n"+std::to_string(integer(metadata,"mapGeneration"))+"\n"+str(metadata,"participantId")+"\n"+(metadata["debug"].asBool().unwrapOr(false)?"debug":"online"));
}
inline bool cbfDocumentComplete(Document const& d){
    if(integer(d.metadata,"nativeInputVersion")!=3)return true;
    if(d.records.empty()||!cbfInitialDataValid(d.records.front()["cbfInitial"]))return false;
    bool frame=false,proof=false;
    for(auto const& record:d.records){auto kind=str(record,"kind");
        if(kind=="frame"){if(frame&&!proof)return false;frame=true;proof=false;}
        if(kind=="cbf"){if(!frame||proof||!cbfFrameValid(record))return false;proof=true;}
        if(kind=="pose"&&frame&&!proof)return false;
    }
    return frame&&proof;
}
inline bool receiptMatches(Document const& d,Receipt const& r){auto const& m=d.metadata;
    return cbfDocumentComplete(d)&&(integer(m,"nativeInputVersion")<2||(!d.records.empty()&&replayInitialDataProblem(d.records.front()).empty()&&replayPhysicsEnvironmentProblem(m["physicsEnvironment"]).empty()))&&
      r.accepted&&r.ended&&d.complete&&!d.records.empty()&&str(d.records.front(),"kind")=="initial"&&
      std::any_of(d.records.begin(),d.records.end(),[](auto const& item){return str(item,"kind")=="frame";})&&
      std::any_of(d.records.begin(),d.records.end(),[](auto const& item){return str(item,"kind")=="pose";})&&str(m,"matchId")==r.matchId&&str(m,"roundKey")==r.roundKey&&str(m,"attemptId")==r.attemptId&&
      str(m,"participantId")==r.participantId&&integer(m,"mapGeneration")==r.mapGeneration&&integer(m,"levelId")==r.levelId&&
      integer(m,"mapVersion")==r.mapVersion&&str(m,"mapGeometrySha256")==r.geometryHash&&
      score(number(d.records.back(),"progress"),str(m,"phase")=="TIEBREAK")==score(r.progress,str(m,"phase")=="TIEBREAK")&&
      r.cleared==(r.progress==100)&&score(r.progress,str(m,"phase")=="TIEBREAK")<=score(r.roundBest,str(m,"phase")=="TIEBREAK");
}
}
