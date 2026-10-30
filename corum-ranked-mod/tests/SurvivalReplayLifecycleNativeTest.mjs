// Production lifecycle methods, explicit Scene/HTTP boundaries; not GD physics.
import test from 'node:test';import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';
const root=resolve(new URL('..',import.meta.url).pathname);
function method(src,name){let a=src.indexOf(name);assert(a>=0,name);let b=src.indexOf('{',a)+1,d=1;while(d){d+=(src[b]==='{')-(src[b]==='}');b++;}return src.slice(a,b);}
test('official End retains Start ownership across round/session/scene retirement, rejects viewers, and waits for durable recovery before closing an orphan',()=>{
 const dir=mkdtempSync(join(tmpdir(),'replay-lifecycle-'));try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned JSON dependencies required');
 const runtime=readFileSync(join(root,'src/SurvivalRuntime.cpp'),'utf8'),native=readFileSync(join(root,'src/replay/SurvivalReplayNative.cpp'),'utf8');
 writeFileSync(join(dir,'test.cpp'),`
#include <matjson.hpp>
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>
struct PlayLayer{static inline PlayLayer* current=nullptr;static auto*get(){return current;}double progress=45;};
struct CorumSurvivalReplayLayer:PlayLayer{struct Fields{bool replayOwned=false,liveWorldOwned=false,retiring=false;std::string captureAttempt="a";}fields,*m_fields=&fields;};
namespace corum::ranked{
using Json=matjson::Value;
namespace replay{
struct Recorder{static auto&get(){static Recorder r;return r;}int ended=0,finishes=0,exits=0;bool recovery=false,pending=false;std::string id="a";Json owner,body;std::string reason;
 bool recovering()const{return recovery;}bool hasPendingClose(std::string const&)const{return pending;}
 void officialEnd(Json m,Json e,std::string why,double){++ended;owner=m;body=e;reason=why;pending=true;}
 std::string currentAttemptId()const{return id;}void finishFromLast(std::string){++finishes;}
 void sceneExited(std::string const& attempt,std::string){if(attempt==id){++exits;id.clear();}}
};
PlayLayer* retired=nullptr;void retireNativeReplayFrame(PlayLayer*p){retired=p;}
}
struct SurvivalRuntime{static auto&get(){static SurvivalRuntime r;return r;}
 struct View{std::string id="match",roundKey="old",state="PLAYING",error,activeAttemptId="";int mapGeneration=1;double activeAttemptProgress=0;}m_view;
 std::string m_attempt="a",m_enteredContext="old",m_recoveredEndAttempt;double m_attemptBest=40;Json m_attemptOwner,m_attemptMetadata;bool free=false,watch=false;
 std::vector<Json> events;
 SurvivalRuntime(){m_attemptOwner=event("ATTEMPT_END");m_attemptOwner["attemptId"]="a";m_attemptMetadata["matchId"]="match";m_attemptMetadata["attemptId"]="a";}
 bool freePlaying()const{return free;}bool observing()const{return watch;}bool surrenderPending()const{return false;}std::optional<int>roundRemainingMs()const{return 0;}
 std::string preparationContext()const{return m_view.roundKey;}
 Json event(std::string type){Json e;e["type"]=type;e["roundKey"]=m_view.roundKey;e["mapGeneration"]=m_view.mapGeneration;e["levelId"]=123;e["eventId"]="stable";return e;}
 void enqueue(Json e){events.push_back(e);}
 void endAttempt(double,std::string reason="EXIT",std::string expectedAttempt="");void closeRecoveredAttempt();
};
std::string preparationContextText(std::string key){return key;}
double replayPercent(PlayLayer*p){return p->progress;}
${method(runtime,'void SurvivalRuntime::endAttempt(')}
${method(runtime,'void SurvivalRuntime::closeRecoveredAttempt()')}
${method(native,'void finishSurvivalReplayForReturn(')}
}
int main(){using namespace corum::ranked;auto&r=SurvivalRuntime::get();auto&rec=replay::Recorder::get();CorumSurvivalReplayLayer oldScene,newScene;PlayLayer::current=&oldScene;
 r.endAttempt(99,"QUIT","foreign");assert(r.m_attempt=="a"&&rec.ended==0);
 r.free=true;r.endAttempt(100);r.free=false;r.watch=true;r.endAttempt(100);r.watch=false;assert(rec.ended==0);
 r.m_view.roundKey="new";r.m_view.mapGeneration=2;r.m_view.state="ROUND_RESULT";
 finishSurvivalReplayForReturn(&oldScene,"ROUND_TIMEOUT",false);assert(r.m_attempt.empty()&&rec.ended==1&&r.events.empty());
 assert(rec.body["roundKey"].asString().unwrap()=="old"&&rec.body["mapGeneration"].asInt().unwrap()==1&&rec.body["progress"].asDouble().unwrap()==45);
 finishSurvivalReplayForReturn(&oldScene,"QUIT",false);assert(rec.ended==1); // one stable official closure
 // onExit can follow Native's global PlayLayer replacement. It retires its own
 // capture, and cannot end or finalize the replacement scene's Attempt.
 r.m_attempt="new-a";r.m_attemptOwner["attemptId"]="new-a";rec.id="new-a";newScene.fields.captureAttempt="new-a";PlayLayer::current=&newScene;
 auto before=rec.finishes;finishSurvivalReplayForReturn(&oldScene,"SCENE_EXIT",true);assert(r.m_attempt=="new-a"&&rec.id=="new-a"&&rec.finishes==before&&rec.ended==1&&replay::retired==&oldScene);
 newScene.fields.replayOwned=true;finishSurvivalReplayForReturn(&newScene,"SCENE_EXIT",true);assert(r.m_attempt=="new-a"&&rec.ended==1&&replay::retired==&newScene);newScene.fields.replayOwned=false;newScene.fields.liveWorldOwned=true;finishSurvivalReplayForReturn(&newScene,"SCENE_EXIT",true);assert(rec.ended==1);
 r.m_attempt.clear();r.m_view.roundKey="new";r.m_enteredContext="new";r.m_view.activeAttemptId="server-orphan";r.m_view.activeAttemptProgress=22;rec.recovery=true;rec.pending=false;r.closeRecoveredAttempt();assert(r.events.empty());
 rec.recovery=false;rec.pending=true;r.closeRecoveredAttempt();assert(r.events.empty());rec.pending=false;r.closeRecoveredAttempt();r.closeRecoveredAttempt();assert(r.events.size()==1&&r.events[0]["attemptId"].asString().unwrap()=="server-orphan"&&r.events[0]["progress"].asDouble().unwrap()==22);
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-isystem',json+'/include','-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
