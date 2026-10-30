import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
test('production replay storage atomically promotes official best only, preserves ties and survives failed/corrupt pending capture',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-replay-'));
 try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned Geode JSON dependencies required');
 const code=String.raw`
#include "${join(root,'src/replay/SurvivalReplayStorage.hpp')}"
#include "${join(root,'src/replay/SurvivalReplayPlayback.hpp')}"
#include <cassert>
#include <iostream>
using namespace corum::ranked::replay;
Json metadata(std::string id){Json m;m["formatVersion"]=1;m["gameVersion"]="2.2081";m["geodeVersion"]="5.8.2";m["modVersion"]="beta108";
 m["matchId"]="../../match";m["roundKey"]="R1/../../";m["participantId"]="human";m["attemptId"]=id;m["mapGeneration"]=1;m["levelId"]=123;m["mapVersion"]=1;m["mapGeometrySha256"]=std::string(64,'a');m["phase"]="ROUND_1";return m;}
Receipt receipt(std::string id,double progress){return {"../../match","R1/../../",id,"human",std::string(64,'a'),1,123,1,progress,progress,true,progress==100,true};}
void initial(Storage& s,std::string id,double progress){
 Json r;r["kind"]="initial";r["tick"]=0;r["elapsed"]=0.;r["randomSeed"]="1";r["replayRandSeed"]="2";
 Json frame;frame["kind"]="frame";frame["tick"]=0;frame["elapsed"]=0.;frame["dt"]=.01;
 Json pose;pose["kind"]="pose";pose["tick"]=1;pose["elapsed"]=.01;pose["x"]=0.;pose["y"]=0.;pose["progress"]=progress;pose["rotation"]=0.;pose["mode"]="CUBE";for(auto k:{"mini","upsideDown","flipped","dual","dead","finished"})pose[k]=false;
 s.append(id,{r,frame,pose}).get();
}
void record(Storage& s,std::string id,double progress,std::string reason="DEATH"){
 s.begin(metadata(id)).get();initial(s,id,progress);Json r;r["kind"]="terminal";r["tick"]=1;r["elapsed"]=1.;r["reason"]=reason;r["progress"]=progress;s.append(id,{r},true).get();}
int main(){
 assert(score(1.0005,true)==1001);assert(score(99.9995,true)==100000);assert(score(1.999,false)==1);
 // OS-owned ancestors can be symlinks; the owned replay subtree cannot.
 std::filesystem::create_directories("${dir}/real-save");std::filesystem::create_directory_symlink("${dir}/real-save","${dir}/save-alias");
 {Storage alias("${dir}/save-alias/replays");record(alias,"alias-capture",20);assert(alias.reconcile(receipt("alias-capture",20)).get());assert(alias.best(metadata("x")).get());}
 std::filesystem::create_directories("${dir}/outside");std::filesystem::create_directory_symlink("${dir}/outside","${dir}/real-save/replays/unsafe");
 bool symlinkRefused=false;try{refuseReplaySymlinks("${dir}/real-save/replays/unsafe/best.replay");}catch(...){symlinkRefused=true;}assert(symlinkRefused);
 Storage s("${dir}/replays");record(s,"first",35);auto r=receipt("first",35);r.accepted=false;assert(!s.reconcile(r).get());assert(!s.best(metadata("x")).get());r.accepted=true;assert(s.reconcile(r).get());
 record(s,"lower",20);auto lower=receipt("lower",20);lower.roundBest=35;assert(!s.reconcile(lower).get());assert(!std::filesystem::exists("${dir}/replays/pending/"+corum::ranked::sha256Text("lower")+".tmp"));assert(str(s.best(metadata("x")).get()->metadata,"attemptId")=="first");
 s.begin(metadata("higher")).get();initial(s,"higher",60);assert(str(s.best(metadata("x")).get()->metadata,"attemptId")=="first");
 Json t;t["kind"]="terminal";t["tick"]=1;t["elapsed"]=1.;t["reason"]="ROUND_TIMEOUT";t["progress"]=60.;s.append("higher",{t},true).get();assert(s.reconcile(receipt("higher",60)).get());
 record(s,"tie",60);assert(!s.reconcile(receipt("tie",60)).get());assert(str(s.best(metadata("x")).get()->metadata,"attemptId")=="higher");
 record(s,"clear",100,"CLEAR");assert(s.reconcile(receipt("clear",100)).get());record(s,"clear-tie",100);assert(!s.reconcile(receipt("clear-tie",100)).get());
 record(s,"wrong-map",100);r=receipt("wrong-map",100);r.mapGeneration=2;assert(!s.reconcile(r).get());
 s.begin(metadata("crash")).get();initial(s,"crash",85);Json frame;frame["kind"]="frame";frame["tick"]=1;frame["elapsed"]=.01;frame["dt"]=.01;s.append("crash",{frame}).get();
 {std::ofstream out("${dir}/replays/pending/"+corum::ranked::sha256Text("crash")+".tmp",std::ios::app);out<<"{truncated";}
 auto recovered=s.recover().get();assert(recovered.size()==2);assert(str(s.best(metadata("x")).get()->metadata,"attemptId")=="clear");
 bool failed=false;try{record(s,"clear",99);}catch(...){failed=true;}assert(failed);assert(str(s.best(metadata("x")).get()->metadata,"attemptId")=="clear");
 // A torn journal is promoted only after an exact official closed receipt, with a partial label.
 auto partial=metadata("partial");partial["roundKey"]="round2";s.begin(partial).get();initial(s,"partial",74);
 {std::ofstream out("${dir}/replays/pending/"+corum::ranked::sha256Text("partial")+".tmp",std::ios::app);out<<"{torn";}
 auto proof=receipt("partial",74);proof.roundKey="round2";proof.ended=false;assert(!s.reconcile(proof).get());proof.ended=true;assert(s.reconcile(proof).get());assert(str(s.best(partial).get()->footer,"captureCompleteness")=="PARTIAL_RECOVERED");
 // Tiebreak comparison uses the rules' decimal milli-percent precision.
 auto tb=metadata("tb1");tb["roundKey"]="tb";tb["phase"]="TIEBREAK";s.begin(tb).get();initial(s,"tb1",1.0005);t["progress"]=1.0005;s.append("tb1",{t},true).get();proof=receipt("tb1",1.0005);proof.roundKey="tb";assert(s.reconcile(proof).get());
 auto tb2=tb;tb2["attemptId"]="tb2";s.begin(tb2).get();initial(s,"tb2",1.0006);t["progress"]=1.0006;s.append("tb2",{t},true).get();proof.attemptId="tb2";proof.progress=proof.roundBest=1.0006;assert(!s.reconcile(proof).get());assert(str(s.best(tb).get()->metadata,"attemptId")=="tb1");
 // A complete immutable rename preceding a process crash remains recoverable from the journal.
 auto journal=metadata("rename-window");journal["roundKey"]="round3";s.begin(journal).get();initial(s,"rename-window",40);t["progress"]=40.;s.append("rename-window",{t},true).get();
 auto from=std::filesystem::path("${dir}/replays/pending/"+corum::ranked::sha256Text("rename-window")+".tmp");auto to=std::filesystem::path("${dir}/replays/survival/"+roundStorageKey(journal)+"/"+corum::ranked::sha256Text("rename-window")+".replay");std::filesystem::create_directories(to.parent_path());std::filesystem::rename(from,to);s.recover().get();assert(std::filesystem::exists(from));proof=receipt("rename-window",40);proof.roundKey="round3";assert(s.reconcile(proof).get());
 // A server cutoff authorizes an exact complete Native prefix, while the
 // original longer capture bytes and terminal remain immutable.
 auto cutoff=metadata("cutoff");cutoff["roundKey"]="cutoff-round";s.begin(cutoff).get();initial(s,"cutoff",10);
 for(double value:{20.,30.}){Json f;f["kind"]="frame";f["dt"]=.01;f["tick"]=1;f["elapsed"]=.01;Json p;p["kind"]="pose";p["tick"]=1;p["elapsed"]=.01;p["x"]=0.;p["y"]=0.;p["rotation"]=0.;p["progress"]=value;p["mode"]="CUBE";for(auto key:{"mini","upsideDown","flipped","dual","dead","finished"})p[key]=false;s.append("cutoff",{f,p}).get();}
 t["progress"]=30.;s.append("cutoff",{t},true).get();auto sourceFile=std::filesystem::path("${dir}/replays/pending/"+corum::ranked::sha256Text("cutoff")+".tmp");auto bytes=[](auto const& path){std::ifstream in(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});};auto sourceBytes=bytes(sourceFile);
 auto cutoffReceipt=receipt("cutoff",20);cutoffReceipt.roundKey="cutoff-round";cutoffReceipt.bestAttemptId="cutoff";assert(s.reconcile(cutoffReceipt).get());auto bounded=s.best(cutoff).get();assert(bounded&&number(bounded->records.back(),"progress")==30&&integer(bounded->metadata,"verifiedFrameCount")==2&&str(bounded->metadata,"officialCompletion")=="OFFICIAL_PREFIX");
 auto destination=std::filesystem::path("${dir}/replays/survival/"+roundStorageKey(cutoff)+"/"+corum::ranked::sha256Text("cutoff")+".replay");assert(bytes(destination)==sourceBytes);auto playable=s.playback(cutoff).get();assert(playable&&playable->duration()==.02);
 auto forged=*bounded;cutoffReceipt.progress=cutoffReceipt.roundBest=25;assert(!receiptMatches(forged,cutoffReceipt));cutoffReceipt.progress=cutoffReceipt.roundBest=100;cutoffReceipt.cleared=true;assert(!receiptMatches(forged,cutoffReceipt));
 // Server-best identity, including a tied first attempt, controls promotion.
 auto notBest=metadata("not-official-best");notBest["roundKey"]="tie-identity";s.begin(notBest).get();initial(s,"not-official-best",40);t["progress"]=40.;s.append("not-official-best",{t},true).get();auto tied=receipt("not-official-best",40);tied.roundKey="tie-identity";tied.bestAttemptId="earlier-official-attempt";assert(!s.reconcile(tied).get()&&!s.best(notBest).get());
 // A different generation, map or debug partition cannot read this best.
 auto other=metadata("x");other["mapGeneration"]=2;assert(!s.best(other).get());other=metadata("x");other["debug"]=true;assert(!s.best(other).get());
}
`;
 const file=join(dir,'test.cpp'),bin=join(dir,'test');writeFileSync(file,code);
 execFileSync('g++',['-std=c++23','-pthread','-I',json+'/include','-I',result,file,join(root,'src/replay/SurvivalReplayStorage.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',bin],{stdio:'pipe'});
 execFileSync(bin,[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
