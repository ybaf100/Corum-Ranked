#include "SurvivalReplayRecorder.hpp"
#include "../RankedRuntime.hpp"
#include "SurvivalReplayEnvironment.hpp"
using namespace geode::prelude;
namespace corum::ranked::replay {
namespace {template<class T>bool ready(std::future<T>const& f){return f.valid()&&f.wait_for(std::chrono::seconds(0))==std::future_status::ready;}}
Recorder& Recorder::get(){static auto* recorder=new Recorder;return *recorder;}
Recorder::Recorder():m_storage(Mod::get()->getSaveDir()/"replays"){m_recovery=m_storage.recover();}
void Recorder::note(ReplayNotice kind,std::string stage,std::string reason,Json const& metadata){
    auto id=str(metadata,"attemptId");
    geode::log::info("[SurvivalReplay] stage={} result={} reason={} attemptId={} roundKey={}",stage,
        kind==ReplayNotice::None?"OK":kind==ReplayNotice::Receipt?"WAITING":kind==ReplayNotice::Unsupported?"UNAVAILABLE":"REJECTED",reason,id,str(metadata,"roundKey"));
    if(!m_displayAttempt.empty()&&id!=m_displayAttempt)return;
    m_notice=kind;m_error=(kind==ReplayNotice::None||kind==ReplayNotice::Receipt)?"":std::move(reason);
}
void Recorder::queueReceipt(Json metadata){
    auto id=str(metadata,"attemptId");if(id.empty())return;
    if(std::none_of(m_receipts.begin(),m_receipts.end(),[&](auto const&m){return str(m,"attemptId")==id;}))m_receipts.push_back(std::move(metadata));
}
void Recorder::retryPending(){for(auto const& [_,m]:m_deferredReceipts){auto retry=m;retry["receiptRetries"]=0;queueReceipt(std::move(retry));}m_deferredReceipts.clear();m_nextReceipt={};}
void Recorder::begin(Json metadata){
    auto id=str(metadata,"attemptId");if(id!=m_current&&!m_current.empty())finishFromLast("RESET");m_displayAttempt=id;
    if(auto problem=metadataProblem(metadata);!problem.empty()){note(ReplayNotice::Preparation,"METADATA","INVALID_FIELD:"+problem,metadata);return;}
    metadata["physicsEnvironment"]=captureReplayPhysicsEnvironment();m_environment=metadata["physicsEnvironment"];m_cbf=replayCbfActive(m_environment);metadata["nativeInputVersion"]=m_cbf?3:2;metadata["accuracyMode"]=m_cbf?"CBF_NATIVE_BOUNDARIES_V1":"GD_NATIVE_FRAME_V2";
    if(m_cbf&&m_environment["cbf"]["nativeBoundary"]["leaseHeld"].asBool().unwrapOr(false)){note(ReplayNotice::Preparation,"PHYSICS_ENVIRONMENT","CBF_SOURCE_HOOKS_LEASED",metadata);return;}
    if(auto problem=replayPhysicsEnvironmentProblem(metadata["physicsEnvironment"]);!problem.empty()){
        note(ReplayNotice::Unsupported,"PHYSICS_ENVIRONMENT",problem,metadata);return;
    }
    if(m_captures.contains(str(metadata,"attemptId")))return;
    if(!m_current.empty())finishFromLast("RESET");
    if(m_captures.size()>=8){note(ReplayNotice::Receipt,"START_APPROVAL","APPROVAL_BACKLOG",metadata);return;}
    note(ReplayNotice::None,"CAPTURE_BEGIN","VALID_IDENTITY",metadata);m_terminal.reset();m_preInputs=Json::array();m_current=str(metadata,"attemptId");m_tick=0;m_elapsed=m_bestProgress=0;m_captures[m_current]={std::move(metadata)};m_flushAt={};
}
bool Recorder::physicsEnvironmentUnchanged(){
    if(!active())return false;
    auto current=captureReplayPhysicsEnvironment();
    auto problem=replayPhysicsEnvironmentMismatch(m_environment,current);
    if(!problem.empty()){unsupported("PHYSICS_ENVIRONMENT_CHANGED:"+problem);return false;}
    return true;
}
void Recorder::unsupported(std::string reason){
    auto found=m_captures.find(m_current);if(found==m_captures.end())return;
    found->second.damaged=true;note(ReplayNotice::Unsupported,"NATIVE_BOUNDARY",std::move(reason),found->second.metadata);
    finishFromLast("UNSUPPORTED_PHYSICS");
}
void Recorder::nativeInput(Json value){
    if(!active())return;
    if(m_nativeFrame){value["kind"]="dispatch";add(std::move(value));return;}
    if(m_preInputs.asArray().unwrap().size()>=4096){unsupported("PRE_FRAME_INPUT_LIMIT");return;}
    m_preInputs.push(std::move(value));
}
void Recorder::accepted(std::string const& attempt,bool value){
    auto found=m_captures.find(attempt);if(found==m_captures.end())return;
    if(!value){found->second.uncertain=true;note(ReplayNotice::Receipt,"ATTEMPT_START_ACK","PENDING_OR_REJECTED_START",found->second.metadata);return;}
    auto& c=found->second;if(c.accepted)return;c.accepted=true;
    if(c.damaged)return; // A late Start ACK cannot create an unsupported pending file.
    try{m_writes.push_back({m_storage.begin(c.metadata),"STORAGE_BEGIN",attempt,str(c.metadata,"roundKey")});flush(c);}catch(std::exception const&e){c.damaged=true;note(ReplayNotice::File,"STORAGE_BEGIN",e.what(),c.metadata);}
}
void Recorder::flush(Capture& c){
    if(!c.accepted||c.buffer.empty())return;
    while(!c.buffer.empty()){
        auto count=std::min(MaxBlockRecords,c.buffer.size());std::vector<Json> block(c.buffer.begin(),c.buffer.begin()+count);
        bool final=c.ended&&count==c.buffer.size()&&!c.damaged;m_writes.push_back({m_storage.append(str(c.metadata,"attemptId"),std::move(block),final),final?"STORAGE_FINALIZE":"STORAGE_APPEND",str(c.metadata,"attemptId"),str(c.metadata,"roundKey")});
        c.buffer.erase(c.buffer.begin(),c.buffer.begin()+count);
    }c.bytes=0;
    if(c.ended&&!c.damaged)queueReceipt(c.metadata);
}
void Recorder::add(Json record){
    auto found=m_captures.find(m_current);if(found==m_captures.end()||found->second.ended||found->second.damaged)return;
    record["tick"]=static_cast<std::int64_t>(m_tick);record["elapsed"]=m_elapsed;
    if(!recordValid(record)){found->second.damaged=true;note(ReplayNotice::Data,"CAPTURE_"+str(record,"kind"),"INVALID_NATIVE_SAMPLE:"+record.dump(matjson::NO_INDENTATION).substr(0,512),found->second.metadata);return;}
    if(str(record,"kind")=="pose")m_bestProgress=std::max(m_bestProgress,number(record,"progress"));
    auto& c=found->second;c.bytes+=record.dump(matjson::NO_INDENTATION).size();
    if(c.bytes>1024*1024){c.damaged=true;c.buffer.clear();c.ended=true;note(ReplayNotice::Data,"CAPTURE_BUFFER","START_ACK_BUFFER_EXCEEDED",c.metadata);m_current.clear();return;}
    c.buffer.push_back(std::move(record));
    auto now=std::chrono::steady_clock::now();if(c.accepted&&str(c.buffer.back(),"kind")!="terminal"&&(c.buffer.size()>=MaxBlockRecords||now>=m_flushAt)){
        try{flush(c);}catch(std::exception const&e){c.damaged=true;note(ReplayNotice::File,"STORAGE_APPEND",e.what(),c.metadata);m_current.clear();}
        m_flushAt=now+std::chrono::milliseconds(250);
    }
}
void Recorder::finish(std::string reason,double progress){
    if(m_nativeFrame){if(!m_terminal)m_terminal=std::make_pair(std::move(reason),progress);return;}
    auto found=m_captures.find(m_current);if(found==m_captures.end())return;
    Json t;t["kind"]="terminal";t["reason"]=std::move(reason);t["progress"]=std::clamp(progress,0.,100.);add(t);
    auto& c=found->second;c.ended=true;c.metadata["capturedProgress"]=m_bestProgress;c.metadata["terminalReason"]=t["reason"];
    if(!c.accepted&&!c.damaged)queueReceipt(c.metadata);
    try{flush(c);if(c.damaged&&c.accepted)m_writes.push_back({m_storage.discard(m_current),"STORAGE_DISCARD_DAMAGED",m_current,str(c.metadata,"roundKey")});}catch(std::exception const&e){note(ReplayNotice::File,"STORAGE_FINALIZE",e.what(),c.metadata);}m_current.clear();
}
void Recorder::nativeFrameEnd(){m_nativeFrame=false;if(m_terminal){auto terminal=std::exchange(m_terminal,{});finish(std::move(terminal->first),terminal->second);}}
void Recorder::pump(){
    if(ready(m_recovery))try{for(auto const&m:m_recovery.get())queueReceipt(m);}catch(std::exception const&e){note(ReplayNotice::File,"STORAGE_RECOVERY",e.what(),Json());}
    for(auto it=m_writes.begin();it!=m_writes.end();)if(ready(it->result)){
        Json identity;identity["attemptId"]=it->attempt;identity["roundKey"]=it->round;
        try{it->get();if(it->stage=="STORAGE_FINALIZE")note(ReplayNotice::Receipt,it->stage,"AWAITING_OFFICIAL_RECEIPT",identity);}
        catch(std::exception const&e){auto message=std::string(e.what());note(message.find("full")!=std::string::npos||message.find("space")!=std::string::npos||message.find("capacity")!=std::string::npos?ReplayNotice::Space:ReplayNotice::File,it->stage,message,identity);
            if(auto capture=m_captures.find(it->attempt);capture!=m_captures.end())capture->second.damaged=true;}
        it=m_writes.erase(it);
    }else ++it;
    for(auto it=m_promotions.begin();it!=m_promotions.end();)if(ready(it->result)){
        try{bool promoted=it->result.get();note(ReplayNotice::None,"STORAGE_RECONCILE",promoted?"BEST_PROMOTED":"EXISTING_BEST_PRESERVED",it->metadata);}
        catch(std::exception const&e){note(ReplayNotice::File,"STORAGE_RECONCILE",e.what(),it->metadata);}it=m_promotions.erase(it);
    }else ++it;
    for(auto it=m_captures.begin();it!=m_captures.end();)if(it->second.ended&&it->second.accepted&&it->second.buffer.empty())it=m_captures.erase(it);else ++it;
    auto token=RankedRuntime::get().spectatorLiveSessionToken();
    if(token!=m_receiptToken){m_receiptToken=token;if(!token.empty())retryPending();}
    auto now=std::chrono::steady_clock::now();if(token.empty()||m_receiptBusy||m_receipts.empty()||now<m_nextReceipt)return;
    auto metadata=m_receipts.front();m_receipts.pop_front();m_receiptBusy=true;m_nextReceipt=now+std::chrono::seconds(2);
    web::WebRequest req;req.header("Authorization","Bearer "+token);req.timeout(std::chrono::seconds(3));req.followRedirects(false);
    auto url="https://api.52-74-37-130.sslip.io/api/ranked/survival/replays/matches/"+str(metadata,"matchId")+"/attempts/"+str(metadata,"attemptId");
    m_request.spawn(req.get(url),[this,metadata,token](web::WebResponse response)mutable{
        m_receiptBusy=false;if(token!=RankedRuntime::get().spectatorLiveSessionToken()){queueReceipt(metadata);return;}
        auto j=response.json().unwrapOr(Json());auto tries=integer(metadata,"receiptRetries")+1;metadata["receiptRetries"]=tries;
        auto code=response.code();
        if(code!=200||!j["accepted"].asBool().unwrapOr(false)||!j["ended"].asBool().unwrapOr(false)){
            auto reason=code==0?"NETWORK_TIMEOUT_OR_DISCONNECT":code==401||code==403?"SESSION_OR_OWNER_DENIED":code==404?"RECEIPT_API_OR_RECORD_MISSING":code==429?"RATE_LIMITED":code>=500?"SERVER_ERROR":code==200?"OFFICIAL_ATTEMPT_PENDING":"HTTP_ERROR";
            note(ReplayNotice::Receipt,"OFFICIAL_RECEIPT",std::string(reason)+":"+std::to_string(code),metadata);
            if(code==429)m_nextReceipt=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            if(tries<8&&code!=401&&code!=403)queueReceipt(metadata);
            else if(m_deferredReceipts.size()<MaxPending)m_deferredReceipts[str(metadata,"attemptId")]=metadata;
            return; // Durable pending bytes and known best are preserved.
        }
        if(metadata["capturedProgress"].isNumber()&&score(number(metadata,"capturedProgress"),str(metadata,"phase")=="TIEBREAK")!=score(number(j,"progress"),str(metadata,"phase")=="TIEBREAK")){
            note(ReplayNotice::Unsupported,"OFFICIAL_RECEIPT",str(metadata,"terminalReason")=="DEBUG_FORCE_CLEAR"?"FORCE_CLEAR_HAS_NO_NATIVE_CLEAR_REPLAY":"OFFICIAL_AND_NATIVE_PROGRESS_DIFFER",metadata);return;
        }
        if(auto found=m_captures.find(str(metadata,"attemptId"));found!=m_captures.end()){
            if(found->second.damaged){m_captures.erase(found);return;}accepted(str(metadata,"attemptId"),true);
        }
        Receipt r{str(j,"matchId"),str(j,"roundKey"),str(j,"attemptId"),str(j,"participantId"),str(j,"geometryHash"),
          static_cast<int>(integer(j,"mapGeneration")),static_cast<int>(integer(j,"levelId")),static_cast<int>(integer(j,"mapVersion")),
          number(j,"progress"),number(j,"roundBest"),true,j["cleared"].asBool().unwrapOr(false),j["accepted"].asBool().unwrapOr(false)};
        try{m_promotions.push_back({m_storage.reconcile(r),metadata});}catch(std::exception const&e){note(ReplayNotice::File,"STORAGE_RECONCILE",e.what(),metadata);}
    });
}
}
