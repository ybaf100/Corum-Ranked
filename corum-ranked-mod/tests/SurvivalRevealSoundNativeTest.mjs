// Execute the production lock dispatcher against native FMOD and clock seams.
// The real clickChange asset/mixer are deliberately not emulated here.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const source=readFileSync(join(root,'src/ui/SurvivalMapSelectionLayer.cpp'),'utf8');
function method(name){const begin=source.indexOf(name);assert(begin>=0);let end=source.indexOf('{',begin)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(begin,end);}
const dispatch=method('void SurvivalMapSelectionLayer::playLocks(').replace('SurvivalMapSelectionLayer::','');
function run(main){const dir=mkdtempSync(join(tmpdir(),'survival-reveal-audio-'));try{
 writeFileSync(join(dir,'test.cpp'),String.raw`
#include "domain/SurvivalMapSelection.hpp"
#include <cassert>
#include <deque>
#include <map>
#include <string>
using namespace corum::ranked;
std::int64_t fakeNow=0;auto monotonicMs(){return fakeNow;}
struct RevealRecord {std::int64_t began=0;std::size_t consumed=0;bool complete=false,centerPlayed=false;};std::map<std::string,RevealRecord> reveals;
struct RankedVolumeControl {float gain=1;static RankedVolumeControl&get(){static RankedVolumeControl v;return v;}float volume()const{return gain;}};
struct FMODAudioEngine {static inline std::vector<std::pair<std::int64_t,float>> calls;static FMODAudioEngine*sharedEngine(){static FMODAudioEngine e;return &e;}
 int playEffect(std::string const& name,float speed,float unknown,float volume){assert(name=="clickChange.ogg"&&speed==1&&unknown==0&&volume>=0&&volume<=.22f);calls.emplace_back(fakeNow,volume);return 0;}};
struct Layer {SurvivalMapSelectionTimeline m_timeline;bool m_thumbnail=false,m_needsResume=false;std::deque<std::size_t> m_sounds;std::int64_t m_lastSound=0;
`+dispatch+String.raw`
 void tick(int ms){fakeNow=ms;playLocks(m_timeline.frame(ms));m_needsResume=false;}
};
`+main);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('actual numeric dispatcher plays once per column across frame rates, duplicate polls and a recreated scene',()=>run(String.raw`
int main(){for(auto id:{84719623ULL,148827436ULL,2147483647ULL})for(int step:{8,16,33,137}){
 reveals.clear();FMODAudioEngine::calls.clear();Layer layer;SurvivalSelectionContext c{"m","R1",1,id,1};layer.m_timeline.observe(c,0);
 for(int ms=0;ms<=4000;ms+=step){layer.tick(ms);layer.tick(ms);}
 layer.tick(4000);assert(FMODAudioEngine::calls.size()==std::to_string(id).size());
 for(std::size_t i=1;i<FMODAudioEngine::calls.size();++i)assert(FMODAudioEngine::calls[i].first-FMODAudioEngine::calls[i-1].first>=65);
 // Recreate the UI in the same context: the registry retires prior lock events.
 Layer restored;restored.m_timeline.observe(c,0);restored.tick(4000);assert(restored.m_sounds.empty());assert(FMODAudioEngine::calls.size()==std::to_string(id).size());
}
}`));
test('thumbnail center confirms once; a mid-reveal numeric fallback never replays already elapsed columns',()=>run(String.raw`
int main(){Layer layer;SurvivalSelectionContext c{"m","R1",1,2147483647,1};layer.m_timeline.observe(c,0);layer.m_thumbnail=true;
 for(int ms=0;ms<4000;ms+=16){layer.tick(ms);}layer.tick(4000);assert(FMODAudioEngine::calls.size()==1);
 assert(FMODAudioEngine::calls[0].first>=2750);layer.tick(4500);assert(FMODAudioEngine::calls.size()==1);
 reveals.clear();FMODAudioEngine::calls.clear();Layer fallback;fallback.m_thumbnail=true;fallback.m_timeline.observe(c,0);
 fallback.tick(2000);auto retired=fallback.m_timeline.frame(2000).locked;fallback.m_thumbnail=false;
 for(int ms=2016;ms<=4000;ms+=16){fallback.tick(ms);}assert(FMODAudioEngine::calls.size()==10-retired);
}`));
test('muting consumes events, server skips clear queued sounds and background recovery suppresses bursts',()=>run(String.raw`
int main(){SurvivalSelectionContext c{"m","R1",1,2147483647,1};Layer layer;layer.m_timeline.observe(c,0);RankedVolumeControl::get().gain=0;
 for(int ms=0;ms<=4000;ms+=16){layer.tick(ms);}assert(FMODAudioEngine::calls.empty());RankedVolumeControl::get().gain=1;layer.tick(4500);assert(FMODAudioEngine::calls.empty());
 reveals.clear();Layer skipped;skipped.m_timeline.observe(c,0);skipped.m_timeline.skip();skipped.tick(100);assert(FMODAudioEngine::calls.empty()&&skipped.m_sounds.empty());
 reveals.clear();Layer resumed;resumed.m_timeline.observe(c,0);resumed.m_needsResume=true;resumed.tick(3900);resumed.tick(4000);assert(FMODAudioEngine::calls.empty()&&resumed.m_sounds.empty());
}`));
