import test from 'node:test';
import {writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
test('production input timeline keeps P1/P2 tick order, detects native divergence and bounds forward seek without pose writes',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-replay-playback-'));
 try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned Geode JSON dependencies required');
 const cpp=String.raw`
#include "${join(root,'src/replay/SurvivalReplayPlayback.hpp')}"
#include <cassert>
using namespace corum::ranked::replay;
Document source(){Document d;d.complete=true;Json initial;initial["kind"]="initial";d.records.push_back(initial);
 for(int i=0;i<200;++i){Json f;f["kind"]="frame";f["dt"]=.01;d.records.push_back(f);Json s;s["kind"]="step";s["nativeStep"]=i;s["dt"]=.01;s["half"]=false;s["last"]=true;d.records.push_back(s);
 Json input;input["kind"]="input";input["button"]=1;input["down"]=(i%2==0);input["player2"]=(i%3==0);input["nativeStep"]=i;input["timestamp"]=i*.01;d.records.push_back(input);
 Json p;p["kind"]="pose";p["x"]=i*2.;p["y"]=40.;p["mode"]="CUBE";p["mini"]=false;p["upsideDown"]=false;p["flipped"]=false;p["dual"]=false;p["dead"]=false;p["finished"]=false;p["progress"]=i*.5;d.records.push_back(p);}
 return d;
}
void advance(Playback& p,int& tick){auto* frame=p.current();assert(frame);assert(frame->inputs.size()==1);assert(integer(frame->inputs[0],"nativeStep")==tick);p.nativeStep(.01,false,true,tick++);p.completeFrame(frame->poses[0]);}
int main(){Playback p(source());assert(!p.due());p.accumulate(.01);assert(p.due());int tick=0;advance(p,tick);assert(!p.desync());
 p.pause(true);p.accumulate(1);assert(!p.due());p.speed(4);assert(p.speed()==4);p.speed(3);assert(p.speed()==4);
 p.seek(1);assert(p.seeking()&&p.due());int frames=0;while(p.due()&&frames<SeekPolicy::MaxFramesPerPump){advance(p,tick);++frames;}assert(frames==96&&p.seeking());
 while(p.due())advance(p,tick);assert(!p.seeking()&&!p.desync());p.seek(.1);assert(p.takeReset());p.restart();tick=0;while(p.due())advance(p,tick);assert(p.time()>=.1&&p.time()<.12);
 {// P1-D: rapid slider moves coalesce; newest target wins and a backward target replays from reset.
  auto generation=p.seekGeneration();p.seek(1.5);p.seek(.6);p.seek(.4);assert(p.seekGeneration()==generation+3&&p.seekTarget()==.4);
  if(p.takeReset()){p.restart();tick=0;}assert(p.seeking()&&p.seekProgress()<1);while(p.due())advance(p,tick);
  assert(!p.seeking()&&p.time()>=.4&&p.time()<.42&&p.seekProgress()==1&&!p.desync());
  // Markers come only from recorded evidence: this tape reaches 99.5%, no death/clear/random/anchor.
  for(auto const& m:p.markers())assert(m.kind=='C'||m.kind=='D'||m.kind=='R'||m.kind=='T');assert(p.markers().size()<=Playback::kMaxMarkers);}
 auto frame=p.current();p.nativeStep(.02,false,true,tick);p.completeFrame(frame->poses[0]);assert(p.desync()&&!p.due());
 Playback bad(source());bad.accumulate(.01);bad.nativeStep(.01,false,true,0);auto pose=bad.current()->poses[0];pose["x"]=400.;bad.completeFrame(pose);assert(bad.desync());
 Playback follow(source());follow.followRemote("attempt1",1,50);assert(follow.takeReset()&&follow.seeking());follow.restart();tick=0;while(follow.due())advance(follow,tick);auto before=follow.time();
 follow.followRemote("attempt1",1.001,49.99);assert(!follow.takeReset());follow.followRemote("attempt2",0,0);assert(follow.takeReset());
 bool refused=false;try{auto d=source();d.complete=false;Playback corrupt(std::move(d));}catch(...){refused=true;}assert(refused);
}
`;
 const file=join(dir,'test.cpp'),bin=join(dir,'test');writeFileSync(file,cpp);
 execFileSync('g++',['-std=c++23','-I',json+'/include','-I',result,file,...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',bin],{stdio:'pipe'});execFileSync(bin,[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
