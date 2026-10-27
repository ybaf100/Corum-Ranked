import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';import {execFileSync} from 'node:child_process';
const root=resolve(new URL('..',import.meta.url).pathname);
test('actual recorder and filesystem wait for official Start, defer terminal through final native pose, reconcile lost ACK and preserve best on invalid capture',()=>{
 const dir=mkdtempSync(join(tmpdir(),'replay-recorder-'));
 try{
  const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned SDK JSON required');
  const strip=s=>s.replace(/^#include.*$|^#pragma once$/gm,'');
  const header=strip(readFileSync(join(root,'src/replay/SurvivalReplayRecorder.hpp'),'utf8')).replace('private:','public:');
  const source=strip(readFileSync(join(root,'src/replay/SurvivalReplayRecorder.cpp'),'utf8'));
  const cpp=`
#include "${root}/src/replay/SurvivalReplayStorage.hpp"
#include "${root}/src/replay/SurvivalReplayPlayback.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <thread>
namespace geode {
struct Mod{static Mod*get(){static Mod m;return &m;}std::filesystem::path getSaveDir(){return "${dir}";}};
namespace utils::web {struct WebResponse{matjson::Value value;int status=200;int code()const{return status;}struct Result{matjson::Value value;matjson::Value unwrapOr(matjson::Value)const{return value;}};Result json()const{return {value};}};
struct WebRequest{template<class...T>void header(T...){}template<class T>void timeout(T){}void followRedirects(bool){}int get(std::string const&){return 1;}};}
namespace async{template<class T>struct TaskHolder{std::function<void(T)>cb;template<class F>void spawn(int,F f){cb=f;}void reply(T v){auto f=std::exchange(cb,{});assert(f);f(v);}};}
namespace prelude{using geode::Mod;namespace web=geode::utils::web;}}
namespace corum::ranked {struct RankedRuntime{static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken(){return "test-session-only";}};}
${header}
${source}
using namespace corum::ranked::replay;
Json metadata(std::string id){Json m;m["formatVersion"]=1;m["gameVersion"]="2.2081";m["matchId"]="match";m["roundKey"]="r1";m["attemptId"]=id;m["participantId"]="p";m["mapGeneration"]=1;m["levelId"]=123;m["mapVersion"]=1;m["mapGeometrySha256"]=std::string(64,'a');m["phase"]="ROUND_1";return m;}
void initial(Recorder&r){Json i;i["kind"]="initial";i["randomSeed"]="1";i["replayRandSeed"]="2";r.add(i);}
void frame(Recorder&r,double progress){r.nativeFrameBegin();Json f;f["kind"]="frame";f["dt"]=.01;r.add(f);r.nextTick();r.elapsed(.01);auto pose=corum::ranked::encodeSpectatorPose(corum::ranked::SpectatorGhostPose{});pose["kind"]="pose";pose["progress"]=progress;r.add(pose);}
Receipt receipt(std::string id,double p){return {"match","r1",id,"p",std::string(64,'a'),1,123,1,p,p,true,p==100,true};}
void drain(Recorder&r){for(auto&f:r.m_writes)f.get();r.m_writes.clear();}
int main(){Recorder r;r.m_recovery.get();r.begin(metadata("first"));initial(r);frame(r,35);assert(std::filesystem::is_empty("${dir}/replays/pending"));
 r.finish("DEATH",35);assert(r.active());r.nativeFrameEnd();assert(!r.active());r.accepted("first",true);drain(r);assert(r.storage().reconcile(receipt("first",35)).get());
 r.begin(metadata("timeout"));initial(r);r.accepted("timeout",true);frame(r,74);r.finish("ROUND_TIMEOUT",74);r.finish("EXIT",0);r.nativeFrameEnd();drain(r);assert(r.storage().reconcile(receipt("timeout",74)).get());
 auto best=r.storage().best(metadata("x")).get();assert(best&&str(best->footer,"terminalReason")=="ROUND_TIMEOUT");assert(number(best->records.back(),"progress")==74);
 // A Start response can be ambiguous (lost ACK/retry conflict). Keep RAM until an owner-only receipt proves it.
 r.begin(metadata("lost-ack"));initial(r);r.accepted("lost-ack",false);frame(r,85);r.finish("DEATH",85);r.nativeFrameEnd();assert(!r.m_captures.at("lost-ack").accepted);
 r.accepted("lost-ack",true);drain(r);assert(r.storage().reconcile(receipt("lost-ack",85)).get());
 r.begin(metadata("damaged"));initial(r);r.accepted("damaged",true);frame(r,99);Json bad;bad["kind"]="frame";bad["dt"]=-1.;r.add(bad);r.finish("DEATH",99);r.nativeFrameEnd();drain(r);
 assert(!r.error().empty());assert(!r.storage().reconcile(receipt("damaged",99)).get());assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="lost-ack");
}
`;
  writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-pthread','-I',json+'/include','-I',result,join(dir,'test.cpp'),join(root,'src/replay/SurvivalReplayStorage.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
