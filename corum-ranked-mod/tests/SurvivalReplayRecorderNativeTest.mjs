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
#include "${root}/tests/helpers/ReplayPhysicsFixture.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <thread>
namespace geode {
namespace log {template<class... T>void info(T const&...) {}}
struct Mod{static Mod*get(){static Mod m;return &m;}std::filesystem::path getSaveDir(){return "${dir}";}};
namespace utils::web {struct WebResponse{matjson::Value value;int status=200;int code()const{return status;}struct Result{matjson::Value value;matjson::Value unwrapOr(matjson::Value)const{return value;}};Result json()const{return {value};}};
struct WebRequest{template<class...T>void header(T...){}template<class T>void timeout(T){}void followRedirects(bool){}int get(std::string const&){return 1;}};}
namespace async{template<class T>struct TaskHolder{std::function<void(T)>cb;template<class F>void spawn(int,F f){cb=f;}void reply(T v){auto f=std::exchange(cb,{});assert(f);f(v);}};}
namespace prelude{using geode::Mod;namespace web=geode::utils::web;}}
namespace corum::ranked {struct RankedRuntime{static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken(){return "test-session-only";}};}
namespace corum::ranked::replay {matjson::Value testEnvironment=[]{matjson::Value e;e["schema"]=2;e["cbf"]["loaded"]=false;return e;}();matjson::Value captureReplayPhysicsEnvironment(){return testEnvironment;}}
${header}
${source}
using namespace corum::ranked::replay;
Json metadata(std::string id){Json m;m["formatVersion"]=1;m["gameVersion"]="2.2081";m["matchId"]="match";m["roundKey"]="r1";m["attemptId"]=id;m["participantId"]="p";m["mapGeneration"]=1;m["levelId"]=123;m["mapVersion"]=1;m["mapGeometrySha256"]=std::string(64,'a');m["phase"]="ROUND_1";return m;}
void initial(Recorder&r){r.add(initialReplayPhysicsFixture());}
void frame(Recorder&r,double progress){r.nativeFrameBegin();Json f;f["kind"]="frame";f["dt"]=.01;r.add(f);r.nextTick();r.elapsed(.01);auto pose=corum::ranked::encodeSpectatorPose(corum::ranked::SpectatorGhostPose{});pose["kind"]="pose";pose["progress"]=progress;r.add(pose);}
Receipt receipt(std::string id,double p){return {"match","r1",id,"p",std::string(64,'a'),1,123,1,p,p,true,p==100,true};}
void drain(Recorder&r){for(auto&f:r.m_writes)f.get();r.m_writes.clear();}
int main(){Recorder r;r.m_recovery.get();testEnvironment["cbf"]["loaded"]=true;testEnvironment["cbf"]["version"]="v1.5.0";testEnvironment["cbf"]["disabled"]=false;testEnvironment["cbf"]["clickOnSteps"]=false;testEnvironment["cbf"]["physicsBypass"]=false;
 r.begin(metadata("unsupported-cbf"));assert(!r.active()&&r.notice()==ReplayNotice::Unsupported&&r.error()=="CBF_NATIVE_BOUNDARIES_UNAVAILABLE");testEnvironment["cbf"]["loaded"]=false;r.begin(metadata("bad-initial"));r.unsupported("INITIAL_PHYSICS_UNAVAILABLE");r.accepted("bad-initial",true);assert(!r.active()&&r.m_writes.empty());r.begin(metadata("first"));initial(r);frame(r,35);assert(std::filesystem::is_empty("${dir}/replays/pending"));
 // Exercise production Recorder -> validation -> disk for a real queued
 // command. Nested commands inherit the enclosing record's tick/time context.
 Json command;command["button"]=1;command["down"]=true;command["player2"]=false;command["nativeStep"]=7;command["timestamp"]=.003;
 Json queue;queue["kind"]="buttons";queue["dt"]=.005;queue["clear"]=false;queue["nativeStep"]=7;queue["queued"]=Json::array();queue["queued"].push(command);queue["remaining"]=Json::array();r.add(queue);assert(r.active()&&r.error().empty());
 auto valid=queue;valid["tick"]=7;valid["elapsed"]=.02;assert(recordValid(valid));valid["queued"][0]["timestamp"]=Json();assert(!recordValid(valid));
 r.finish("DEATH",35);assert(r.active());r.nativeFrameEnd();assert(!r.active());r.accepted("first",true);drain(r);assert(r.storage().reconcile(receipt("first",35)).get());
 r.begin(metadata("timeout"));initial(r);r.accepted("timeout",true);frame(r,74);r.finish("ROUND_TIMEOUT",74);r.finish("EXIT",0);r.nativeFrameEnd();drain(r);assert(r.storage().reconcile(receipt("timeout",74)).get());
 auto best=r.storage().best(metadata("x")).get();assert(best&&str(best->footer,"terminalReason")=="ROUND_TIMEOUT");assert(number(best->records.back(),"progress")==74);
 // A Start response can be ambiguous (lost ACK/retry conflict). Keep RAM until an owner-only receipt proves it.
 r.begin(metadata("lost-ack"));initial(r);r.accepted("lost-ack",false);frame(r,85);r.finish("DEATH",85);r.nativeFrameEnd();assert(!r.m_captures.at("lost-ack").accepted);
 r.accepted("lost-ack",true);drain(r);assert(r.storage().reconcile(receipt("lost-ack",85)).get());
 r.begin(metadata("damaged"));initial(r);r.accepted("damaged",true);frame(r,99);Json bad;bad["kind"]="frame";bad["dt"]=-1.;r.add(bad);r.finish("DEATH",99);r.nativeFrameEnd();drain(r);
 assert(!r.error().empty());assert(!r.storage().reconcile(receipt("damaged",99)).get());assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="lost-ack");
 // A scoped preparation error is actionable and a new Attempt clears it.
 auto invalid=metadata("bad-metadata");invalid["mapVersion"]=0;r.begin(invalid);assert(r.notice()==ReplayNotice::Preparation&&r.error().find("mapVersion")!=std::string::npos);
 r.begin(metadata("new-good"));initial(r);assert(r.active()&&r.notice()==ReplayNotice::None&&r.error().empty());
 // An older asynchronous filesystem error must not poison the new capture UI.
 std::promise<void> failed;failed.set_exception(std::make_exception_ptr(std::runtime_error("disk full")));
 r.m_writes.push_back({failed.get_future(),"STORAGE_APPEND","old-attempt","r0"});r.m_receipts.clear();r.pump();assert(r.notice()==ReplayNotice::None);
 r.accepted("new-good",true);frame(r,90);r.finish("DEATH",90);r.nativeFrameEnd();drain(r);r.m_receipts.clear();
 // Exercise the real request completion path for all receipt failure classes.
 for(int code:{0,401,403,404,429,500,200}){
   r.m_receipts.clear();r.m_deferredReceipts.clear();r.m_nextReceipt={};r.queueReceipt(metadata("new-good"));r.pump();assert(r.m_receiptBusy);
   geode::utils::web::WebResponse reply;reply.status=code;r.m_request.reply(reply);assert(!r.m_receiptBusy&&r.notice()==ReplayNotice::Receipt);
   assert(std::filesystem::exists("${dir}/replays/pending/"+corum::ranked::sha256Text("new-good")+".tmp"));
   assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="lost-ack");
 }
 // Force Clear cannot promote nonexistent 100% native inputs over a real best.
 r.m_receipts.clear();auto forced=metadata("new-good");forced["capturedProgress"]=90.;forced["terminalReason"]="DEBUG_FORCE_CLEAR";r.m_nextReceipt={};r.queueReceipt(forced);r.pump();
 geode::utils::web::WebResponse mismatch;mismatch.value["accepted"]=true;mismatch.value["ended"]=true;mismatch.value["progress"]=100.;r.m_request.reply(mismatch);
 assert(r.notice()==ReplayNotice::Unsupported&&r.error()=="FORCE_CLEAR_HAS_NO_NATIVE_CLEAR_REPLAY");
 assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="lost-ack");
 // A transient API failure retains the file and a subsequent real receipt promotes it.
 auto retry=metadata("new-good");retry["capturedProgress"]=90.;r.m_receipts.clear();r.m_nextReceipt={};r.queueReceipt(retry);r.pump();
 geode::utils::web::WebResponse ok;ok.value=metadata("new-good");
 ok.value["geometryHash"]=std::string(64,'a');ok.value["accepted"]=true;ok.value["ended"]=true;ok.value["progress"]=90.;ok.value["roundBest"]=90.;ok.value["cleared"]=false;r.m_request.reply(ok);
 assert(r.m_promotions.size()==1);assert(r.m_promotions.front().result.get());r.m_promotions.clear();
 assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="new-good");
 // Version 3 uses the same approved recorder/storage/receipt path. These are
 // format/transaction tests; actual CBF physics is exercised separately.
 testEnvironment["cbf"]["loaded"]=true;testEnvironment["cbf"]["precisionFix"]=false;
 auto& capability=testEnvironment["cbf"]["nativeBoundary"];capability["driver"]=1;capability["verified"]=true;capability["binarySha256"]=std::string(64,'b');capability["platform"]="ios";
 auto cbfMetadata=metadata("cbf-complete");cbfMetadata["roundKey"]="cbf-round";
 r.begin(cbfMetadata);assert(r.active()&&r.cbfCapture());
 auto init=initialReplayPhysicsFixture();init["cbfInitial"]["schema"]=1;init["cbfInitial"]["players"]=Json::array();
 for(int i=0;i<2;++i){Json player;player["lastPosition"]=Json::array();player["lastPosition"].push(0.);player["lastPosition"].push(105.);player["collisionLogs"]=Json::array();for(int j=0;j<4;++j)player["collisionLogs"].push(0);init["cbfInitial"]["players"].push(player);}
 r.add(init);r.accepted("cbf-complete",true);r.nativeFrameBegin();
 Json f;f["kind"]="frame";f["dt"]=.01;f["preInputs"]=Json::array();auto applied=command;applied["before"]=Json::array();applied["after"]=Json::array();f["preInputs"].push(applied);r.add(f);r.nativeInput(applied);
 Json proof;proof["kind"]="cbf";proof["schema"]=1;proof["roots"]=Json::array();proof["clocks"]=Json::array();r.add(proof);
 auto pose=corum::ranked::encodeSpectatorPose(corum::ranked::SpectatorGhostPose{});pose["kind"]="pose";pose["progress"]=50.;r.add(pose);r.finish("DEATH",50);r.nativeFrameEnd();drain(r);
 auto cr=receipt("cbf-complete",50);cr.roundKey="cbf-round";assert(r.storage().reconcile(cr).get());auto cbfBest=r.storage().best(cbfMetadata).get();assert(cbfBest&&integer(cbfBest->metadata,"nativeInputVersion")==3&&integer(cbfBest->footer,"inputEventCount")==2);
 auto incomplete=*cbfBest;std::erase_if(incomplete.records,[](auto const& v){return str(v,"kind")=="cbf";});assert(!receiptMatches(incomplete,cr));
 auto unknown=cbfBest->metadata;unknown["nativeInputVersion"]=4;assert(!metadataValid(unknown));unknown["nativeInputVersion"]=3;unknown["accuracyMode"]="GD_NATIVE_FRAME_V2";assert(!metadataValid(unknown));
 capability["leaseHeld"]=true;r.begin(metadata("leased-source"));assert(!r.active()&&r.error()=="CBF_SOURCE_HOOKS_LEASED");capability["leaseHeld"]=false;
 r.begin(metadata("changed-environment"));r.add(init);assert(r.physicsEnvironmentUnchanged());testEnvironment["cbf"]["disabled"]=true;assert(!r.physicsEnvironmentUnchanged()&&!r.active());
 assert(str(r.storage().best(metadata("x")).get()->metadata,"attemptId")=="new-good");
}

`;
  writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-pthread','-I',json+'/include','-I',result,join(dir,'test.cpp'),join(root,'src/replay/SurvivalReplayStorage.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
