import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
function run(source){const dir=mkdtempSync(join(tmpdir(),'history-sdk-'));try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;
 assert(json&&result,'Set CORUM_MATJSON_SOURCE and CORUM_RESULT_INCLUDE to the pinned SDK dependencies');
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),'-isystem',join(json,'include'),'-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('SDK JSON history projection preserves real rounds, precision, missing data, safe navigation and responsive layout',()=>run(`
#include "domain/HistoryPresentation.hpp"
#include "domain/ScoreDisplay.hpp"
#include "domain/MapDifficulty.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 auto record=matjson::parse(R"({"rounds":[{"key":"r1","ordinal":1,"phase":"ROUND_1","completed":true,"map":{"levelId":123,"title":"Exact Map"},"standings":[{"participantId":"a","rank":1,"highestProgress":82.345,"own":true,"tiedPlayers":2,"qualified":true,"attemptCount":3}]},{"key":"final","ordinal":2,"phase":"FINAL","completed":false,"standings":[{"rank":1,"highestProgress":99}]},{"key":"tb","ordinal":3,"phase":"TIEBREAK","completed":true}]})").unwrap();
 record["rounds"][0]["standings"][0]["gdAccountId"]="123";record["rounds"][0]["standings"][0]["displayedTier"]="SILVER";
 record["rounds"][0]["map"]["epic"]=2;record["rounds"][0]["map"]["featureScore"]=1;
 auto rounds=historyRounds(record,true);assert(rounds[0].standings[0].accountId==123&&rounds[0].standings[0].tier=="SILVER");assert(rounds[0].epic==2&&rounds[0].grade=="LEGENDARY");
 record["rounds"][0]["standings"][0]["isBot"]=true;assert(historyRounds(record,true)[0].standings[0].accountId==0);
 for(bool page:{false,true})for(bool error:{false,true})for(bool session:{false,true})for(bool conflict:{false,true})assert(historyReconnectNeeded(page,error,session,conflict)==(page&&error&&!session&&!conflict));
 assert(recordedMapDifficulty("EASY_DEMON")->nativeCode==7);assert(recordedMapDifficulty("MEDIUM DEMON")->nativeCode==8);
 assert(recordedMapDifficulty("HARD DEMON")->nativeCode==6);assert(recordedMapDifficulty("INSANE DEMON")->nativeCode==9);assert(recordedMapDifficulty("EXTREME DEMON")->nativeCode==10);
 assert(recordedMapDifficulty("Auto")->nativeCode==-1);assert(!recordedMapDifficulty("unknown"));
 assert(historyMapPanelHeight(180,false)<historyMapPanelHeight(180,true));assert(historyMapPanelHeight(390,false)<historyMapPanelHeight(390,true));assert(rounds.size()==3&&rounds[0].levelId==123&&rounds[0].standings[0].progress==82.345);
 assert(rounds[0].standings[0].own&&rounds[0].standings[0].tiedPlayers==2&&rounds[0].standings[0].attempts==3);
 assert(rounds[1].label=="FINAL"&&!rounds[1].completed&&rounds[1].standings.empty());assert(rounds[2].label=="TB 1");
 record["rounds"][0]["phase"]="ROUND_3";assert(historyRounds(record,true)[0].label=="R3");
 record["rounds"][0]["phase"]="ROUND_4";assert(historyRounds(record,true)[0].label=="R4");
 auto solo=matjson::parse(R"({"rounds":[{"roundNumber":2,"completed":true,"levelId":456,"mapTitle":"Solo","scoreA":400,"scoreB":200,"bestProgressA":82,"bestProgressB":100,"clearedB":true}],"deathmatches":[{"sequence":1,"completed":true,"levelId":789,"bestProgressA":82.345,"bestProgressB":82.345,"nextDeathmatchSequence":2},{"sequence":2,"completed":true}]})").unwrap();
 auto duel=historyRounds(solo,false);assert(duel.size()==3&&duel[0].label=="R2"&&duel[0].bestA==82&&duel[0].clearedB);
 assert(duel[1].deathmatch&&duel[1].bestA==82.345&&duel[1].nextDeathmatch==2&&rankedScoreText(*duel[1].bestA,true)=="82.345");
 assert(duel[2].levelId==0&&!duel[2].bestA);assert(historyRounds(matjson::Value{},false).empty());
 for(auto const& value:{matjson::Value{},matjson::Value(nullptr),matjson::Value(-1),matjson::Value(1.5),matjson::Value(2147483648.)})assert(historyLevelId(value)==0);
 for(float width:{320,480,568,800,1366})for(float height:{270,320,768}){auto l=historyLayout(width,height);assert(l.width<=width-48&&l.height<=height-132);if(l.split)assert(l.mapWidth+l.boardWidth+l.ownWidth+16<=l.width+.01f);else assert(l.boardWidth==l.width);}
 assert(historyResultFilters(true,false)==std::vector<std::string>({"ALL","WIN","LOSS"}));
 for(bool soloBusy:{false,true})for(bool survivalBusy:{false,true})for(bool nativeBusy:{false,true})assert(historyLevelNavigationAllowed(soloBusy,survivalBusy,nativeBusy)==(!soloBusy&&!survivalBusy&&!nativeBusy));
}
`));
test('actual history transport lazily caches completed detail and fences rapid selection, refresh, closure and session replacement',()=>{
 const runtime=read('src/RankedRuntime.cpp'),header=read('src/RankedRuntime.hpp');
 run(`
#include "domain/RankedMode.hpp"
#include <matjson.hpp>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace web {
struct WebResponse{int code=200;matjson::Value body;struct Json{matjson::Value value;matjson::Value unwrapOr(matjson::Value)const{return value;}};Json json()const{return {body};}};
struct Request{std::string url;};struct WebRequest{Request get(std::string url){return {url};}};
struct Job{Request request;std::function<void(WebResponse)> callback;bool cancelled=false;};inline std::vector<std::shared_ptr<Job>>jobs;
}
namespace geode::async{template<class T>struct TaskHolder{std::shared_ptr<web::Job>job;void cancel(){if(job)job->cancelled=true;}template<class F>void spawn(web::Request req,F f){job=std::make_shared<web::Job>(web::Job{req,f});web::jobs.push_back(job);}};}
web::WebRequest baseRequest(std::string const&){return {};}bool successful(web::WebResponse const&r){return r.code==200;}std::string responseError(web::WebResponse const&){return "failed";}
namespace corum::ranked{
using RoundSummaryView=int;using DeathmatchSummaryView=int;
${method(header,'struct HistoryMatchView')};
int parseRoundSummary(matjson::Value const&){return 1;}int parseDeathmatchSummary(matjson::Value const&){return 1;}
struct RankedRuntime{
 std::string m_sessionToken="token",m_historyDetailMatch,m_historyOrder="NEWEST",m_historyResult="ALL";bool m_rankedOpen=true;
 std::uint64_t m_historyGeneration=1,m_historyDetailGeneration=0,m_sessionGeneration=1;
 geode::async::TaskHolder<web::WebResponse>m_historyRequest,m_historyDetailRequest;
 struct{bool historyLoading=false,historyDetailLoading=false;std::string historyError,historyDetailError;std::uint64_t historyRevision=0,revision=0;HistoryScope historyScope=HistoryScope::All;std::vector<HistoryMatchView>history;}m_view;
 std::string endpoint(std::string const&s){return s;}void observeServerNow(matjson::Value const&){}
 void fetchHistoryScope(HistoryScope);void fetchHistoryDetail(RankedMode,std::string const&,bool refresh=false);void cancelHistoryReads();void setHistoryFilters(std::string,std::string);
};
${method(runtime,'static corum::ranked::HistoryMatchView parseHistoryMatch(')}
${method(runtime,'void RankedRuntime::fetchHistoryScope(')}
${method(runtime,'void RankedRuntime::fetchHistoryDetail(')}
${method(runtime,'void RankedRuntime::cancelHistoryReads(')}
${method(runtime,'void RankedRuntime::setHistoryFilters(')}
}
int main(){using namespace corum::ranked;RankedRuntime r;
 r.fetchHistoryScope(HistoryScope::Survival);auto list=web::jobs.back();assert(list->request.url.find("summary=true")!=std::string::npos);
 auto a=matjson::parse(R"({"mode":"SURVIVAL","matchId":"a","inProgress":true,"detailsLoaded":false,"result":null})").unwrap();auto b=a;b["matchId"]="b";
 matjson::Value body;body["scope"]="SURVIVAL";body["matches"]=std::vector<matjson::Value>{a,b};list->callback({200,body});
 assert(r.m_view.history.size()==2&&!r.m_view.history[0].ownRankedScoreDelta);
 r.fetchHistoryDetail(RankedMode::Survival,"a");auto first=web::jobs.back();auto count=web::jobs.size();r.fetchHistoryDetail(RankedMode::Survival,"a",true);assert(web::jobs.size()==count);
 r.fetchHistoryDetail(RankedMode::Survival,"b");auto second=web::jobs.back();assert(first->cancelled);
 a["detailsLoaded"]=true;body["match"]=a;first->callback({200,body});assert(!r.m_view.history[0].detailsLoaded&&r.m_view.historyDetailLoading);
 b["detailsLoaded"]=true;b["inProgress"]=false;b["result"]=matjson::Value();b["result"]["actualDelta"]=25;body["match"]=b;second->callback({200,body});assert(r.m_view.history[1].detailsLoaded&&r.m_view.history[1].ownRankedScoreDelta==25);
 count=web::jobs.size();r.fetchHistoryDetail(RankedMode::Survival,"b");assert(web::jobs.size()==count);
 r.fetchHistoryDetail(RankedMode::Survival,"a");auto closed=web::jobs.back();r.cancelHistoryReads();body["match"]=a;closed->callback({200,body});assert(!r.m_view.historyDetailLoading&&!r.m_view.history[0].detailsLoaded);
 r.fetchHistoryDetail(RankedMode::Survival,"a");auto retired=web::jobs.back();++r.m_sessionGeneration;retired->callback({200,body});assert(!r.m_view.history[0].detailsLoaded);r.cancelHistoryReads();
 r.setHistoryFilters("OLDEST","invalid");assert(r.m_historyOrder=="OLDEST"&&r.m_historyResult=="ALL");
}
`);
});


test('the authoritative result roster keeps all 64 places including an eliminated own player at rank 64',()=>{
 const roster=method(read('src/ui/SurvivalOutcomePanel.hpp'),'inline std::vector<SurvivalStanding> survivalRoster(');
 run(`
#include "domain/SurvivalHudPresentation.hpp"
#include <matjson.hpp>
#include <cassert>
#include <charconv>
namespace geode::utils {template<class T>struct Number{T value;T unwrapOr(T)const{return value;}};template<class T>Number<T> numFromString(std::string const& s){T n=0;std::from_chars(s.data(),s.data()+s.size(),n);return {n};}}
namespace corum::ranked {
struct SurvivalMatchView{int matchPlayerCount=64;matjson::Value participants,result;std::vector<SurvivalStanding> roundPlayers;};
${roster}
}
int main(){using namespace corum::ranked;SurvivalMatchView view;std::vector<matjson::Value> participants;
 for(int i=1;i<=64;i++){matjson::Value p;p["participantId"]="p"+std::to_string(i);p["name"]="Player "+std::to_string(i);p["kind"]="HUMAN";p["gdAccountId"]=std::to_string(i);p["finalRank"]=i;p["eliminationStage"]=i==1?matjson::Value():matjson::Value(1);participants.push_back(p);}
 view.participants=participants;auto rows=survivalRoster(view);assert(rows.size()==64&&rows.back().rank==64&&rows.back().id=="p64"&&rows.back().eliminated);
 view.matchPlayerCount=16;view.participants=std::vector<matjson::Value>(participants.begin(),participants.begin()+16);rows=survivalRoster(view);assert(rows.size()==16&&rows.back().rank==16);
}
`);
});
