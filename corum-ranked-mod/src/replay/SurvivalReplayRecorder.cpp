#include "SurvivalReplayRecorder.hpp"
#include "../RankedRuntime.hpp"
using namespace geode::prelude;
namespace corum::ranked::replay {
namespace {template<class T>bool ready(std::future<T>const& f){return f.valid()&&f.wait_for(std::chrono::seconds(0))==std::future_status::ready;}}
Recorder& Recorder::get(){static auto* recorder=new Recorder;return *recorder;}
Recorder::Recorder():m_storage(Mod::get()->getSaveDir()/"replays"){m_recovery=m_storage.recover();}
void Recorder::begin(Json metadata){
    if(!metadataValid(metadata)){m_error="Replay capture identity unavailable";return;}
    if(m_captures.contains(str(metadata,"attemptId")))return;
    if(!m_current.empty())finishFromLast("RESET");
    if(m_captures.size()>=8){m_error="Replay approval backlog; existing best preserved";return;}
    m_error.clear();m_terminal.reset();m_current=str(metadata,"attemptId");m_tick=0;m_elapsed=m_bestProgress=0;m_captures[m_current]={std::move(metadata)};m_flushAt={};
}
void Recorder::accepted(std::string const& attempt,bool value){
    auto found=m_captures.find(attempt);if(found==m_captures.end())return;
    if(!value){found->second.uncertain=true;return;}
    auto& c=found->second;if(c.accepted)return;c.accepted=true;
    try{m_writes.push_back(m_storage.begin(c.metadata));flush(c);}catch(std::exception const&e){m_error=e.what();}
}
void Recorder::flush(Capture& c){
    if(!c.accepted||c.buffer.empty())return;
    while(!c.buffer.empty()){
        auto count=std::min(MaxBlockRecords,c.buffer.size());std::vector<Json> block(c.buffer.begin(),c.buffer.begin()+count);
        bool final=c.ended&&count==c.buffer.size()&&!c.damaged;m_writes.push_back(m_storage.append(str(c.metadata,"attemptId"),std::move(block),final));
        c.buffer.erase(c.buffer.begin(),c.buffer.begin()+count);
    }c.bytes=0;
    if(c.ended&&!c.damaged)m_receipts.push_back(c.metadata);
}
void Recorder::add(Json record){
    auto found=m_captures.find(m_current);if(found==m_captures.end()||found->second.ended)return;
    record["tick"]=static_cast<std::int64_t>(m_tick);record["elapsed"]=m_elapsed;
    if(!recordValid(record)){found->second.damaged=true;m_error="Replay native sample invalid";return;}
    if(str(record,"kind")=="pose")m_bestProgress=std::max(m_bestProgress,number(record,"progress"));
    auto& c=found->second;c.bytes+=record.dump(matjson::NO_INDENTATION).size();
    if(c.bytes>1024*1024){c.damaged=true;c.buffer.clear();c.ended=true;m_error="Replay Start ACK buffer exceeded";m_current.clear();return;}
    c.buffer.push_back(std::move(record));
    auto now=std::chrono::steady_clock::now();if(c.accepted&&str(c.buffer.back(),"kind")!="terminal"&&(c.buffer.size()>=MaxBlockRecords||now>=m_flushAt)){
        try{flush(c);}catch(std::exception const&e){m_error=e.what();m_current.clear();}
        m_flushAt=now+std::chrono::milliseconds(250);
    }
}
void Recorder::finish(std::string reason,double progress){
    if(m_nativeFrame){if(!m_terminal)m_terminal=std::make_pair(std::move(reason),progress);return;}
    auto found=m_captures.find(m_current);if(found==m_captures.end())return;
    Json t;t["kind"]="terminal";t["reason"]=std::move(reason);t["progress"]=std::clamp(progress,0.,100.);add(t);
    auto& c=found->second;c.ended=true;
    if(!c.accepted&&!c.damaged)m_receipts.push_back(c.metadata);
    try{flush(c);if(c.damaged&&c.accepted)m_writes.push_back(m_storage.discard(m_current));}catch(std::exception const&e){m_error=e.what();}m_current.clear();
}
void Recorder::nativeFrameEnd(){m_nativeFrame=false;if(m_terminal){auto terminal=std::exchange(m_terminal,{});finish(std::move(terminal->first),terminal->second);}}
void Recorder::pump(){
    if(ready(m_recovery))try{for(auto const&m:m_recovery.get())m_receipts.push_back(m);}catch(std::exception const&e){m_error=e.what();}
    for(auto it=m_writes.begin();it!=m_writes.end();)if(ready(*it)){try{it->get();}catch(std::exception const&e){m_error=e.what();}it=m_writes.erase(it);}else ++it;
    for(auto it=m_promotions.begin();it!=m_promotions.end();)if(ready(*it)){try{(void)it->get();}catch(std::exception const&e){m_error=e.what();}it=m_promotions.erase(it);}else ++it;
    for(auto it=m_captures.begin();it!=m_captures.end();)if(it->second.ended&&it->second.accepted&&it->second.buffer.empty())it=m_captures.erase(it);else ++it;
    auto now=std::chrono::steady_clock::now();if(m_receiptBusy||m_receipts.empty()||now<m_nextReceipt)return;
    auto token=RankedRuntime::get().spectatorLiveSessionToken();if(token.empty())return;
    auto metadata=m_receipts.front();m_receipts.pop_front();m_receiptBusy=true;m_nextReceipt=now+std::chrono::seconds(2);
    web::WebRequest req;req.header("Authorization","Bearer "+token);req.timeout(std::chrono::seconds(3));req.followRedirects(false);
    auto url="https://api.52-74-37-130.sslip.io/api/ranked/survival/replays/matches/"+str(metadata,"matchId")+"/attempts/"+str(metadata,"attemptId");
    m_request.spawn(req.get(url),[this,metadata,token](web::WebResponse response)mutable{
        m_receiptBusy=false;if(token!=RankedRuntime::get().spectatorLiveSessionToken()){m_receipts.push_back(metadata);return;}
        auto j=response.json().unwrapOr(Json());auto tries=integer(metadata,"receiptRetries")+1;metadata["receiptRetries"]=tries;
        if(response.code()==200&&!j["accepted"].asBool().unwrapOr(false)){m_captures.erase(str(metadata,"attemptId"));return;}
        if(response.code()!=200||!j["ended"].asBool().unwrapOr(false)){
            if(tries<8)m_receipts.push_back(metadata);return;
        }
        if(auto found=m_captures.find(str(metadata,"attemptId"));found!=m_captures.end()){
            if(found->second.damaged){m_captures.erase(found);return;}
            accepted(str(metadata,"attemptId"),true);
        }
        Receipt r{str(j,"matchId"),str(j,"roundKey"),str(j,"attemptId"),str(j,"participantId"),str(j,"geometryHash"),
          static_cast<int>(integer(j,"mapGeneration")),static_cast<int>(integer(j,"levelId")),static_cast<int>(integer(j,"mapVersion")),
          number(j,"progress"),number(j,"roundBest"),true,j["cleared"].asBool().unwrapOr(false),j["accepted"].asBool().unwrapOr(false)};
        try{m_promotions.push_back(m_storage.reconcile(r));}catch(std::exception const&e){m_error=e.what();}
    });
}
}
