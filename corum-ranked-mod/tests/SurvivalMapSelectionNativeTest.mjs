import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
function run(body){const dir=mkdtempSync(join(tmpdir(),'survival-selection-'));try{
 writeFileSync(join(dir,'test.cpp'),`#include "domain/SurvivalMapSelection.hpp"\n#include "domain/SurvivalSelectionLayout.hpp"\n#include "domain/SurvivalOptionsPolicy.hpp"\n#include <cassert>\n#include <set>\nusing namespace corum::ranked;\n${body}`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')]);
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('four-second wall-clock reveals preserve 8/9/10 digits, left-to-right locks and exactly one sound event per column at every frame rate',()=>{
 run(`int main(){for(auto id:{84719623ULL,148827436ULL,2147483647ULL})for(auto step:{8,16,33,137}){
 SurvivalMapSelectionTimeline t;SurvivalSelectionContext c{"match","R1",2,id,1};assert(t.observe(c,100));
 auto expected=std::to_string(id);std::vector<std::size_t> sounds;
 for(int ms=0;ms<4000;ms+=step){assert(!t.observe(c,100+ms));auto f=t.frame(100+ms);assert(!f.settled);
  auto locks=t.takeLocks(f,true);sounds.insert(sounds.end(),locks.begin(),locks.end());
  for(std::size_t i=0;i<expected.size();++i){auto reel=survivalReel(t,f,i);if(reel.locked)assert(reel.digit==expected[i]-'0');if(i&&reel.locked)assert(survivalReel(t,f,i-1).locked);}
  assert(t.takeLocks(f,true).empty());
 }
 auto f=t.frame(4100);auto tail=t.takeLocks(f,true);sounds.insert(sounds.end(),tail.begin(),tail.end());
 assert(f.settled&&f.details==1&&f.landing==1&&f.locked==expected.size());assert(sounds.size()==expected.size());
 for(std::size_t i=0;i<sounds.size();++i){assert(sounds[i]==i);assert(survivalReel(t,f,i).digit==expected[i]-'0');}
 assert(t.frame(100000).elapsed==4);assert(!t.frame(4099).settled);
}}`);
});
test('pending server selection, fallback, background elapsed time and server entry skips cannot reselect or extend the timeline',()=>{
 run(`int main(){SurvivalMapSelectionTimeline t;SurvivalSelectionContext c{"m","R1",1,0,1};t.observe(c,0);
 assert(t.frame(9000).stage==SurvivalRevealStage::PendingSelection);c.levelId=123456789;t.observe(c,9000);assert(t.frame(9000).elapsed==0);
 auto before=t.frame(11100);t.takeLocks(before,false);auto numeric=t.frame(11200);assert(numeric.elapsed==2.2);assert(t.digits()=="123456789");
 assert(t.frame(13000).settled);assert(!t.observe(c,13001));assert(t.frame(13001).settled);
 c.mapGeneration++;assert(t.observe(c,14000));assert(!t.frame(15000).settled);
 t.followServer("COUNTDOWN",2000,15000);assert(t.frame(15000).skipped&&t.takeLocks(t.frame(15000),true).empty());
 c.roundKey="FINAL";t.observe(c,18000);t.followServer("COUNTDOWN",std::nullopt,18000);assert(!t.frame(18000).settled);
 t.followServer("PLAYING",{},18000);assert(t.frame(18000).settled);
 c.levelId=99;t.observe(c,20000);t.followServer("CANCELLED",{},20001);assert(t.frame(20002).stage==SurvivalRevealStage::Cancelled);
}`);
});
test('one reveal keeps its selected mode across late images, a permanent fallback and scene recreation',()=>{
 run(`int main(){SurvivalRevealModeChoice cold;assert(cold.choose(true,false));
 assert(cold.choose(true,true));assert(cold.choose(false,false));
 SurvivalRevealModeChoice warm;assert(warm.choose(true,true));assert(warm.choose(true,false));
 warm.fallback();assert(!warm.choose(true,true));auto reopened=warm;assert(!reopened.choose(true,true));
 SurvivalRevealModeChoice nextRound;assert(nextRound.choose(true,true));
 SurvivalRevealModeChoice disabled;assert(!disabled.choose(false,true));assert(!disabled.choose(true,true));
}`);
});
test('thumbnail/numeric layouts keep all text, metadata and touch rows within their panels at phone, 4:3 and wide sizes',()=>{
 run(`int main(){for(auto viewport:{std::pair{400.f,280.f},std::pair{400.f,320.f},std::pair{512.f,384.f},std::pair{520.f,320.f},std::pair{568.f,320.f},std::pair{568.f,426.f},std::pair{640.f,360.f},std::pair{854.f,320.f},std::pair{1024.f,576.f}})for(bool thumb:{false,true})for(bool ldm:{false,true}){
 auto r=survivalSelectionLayout(viewport.first,viewport.second,thumb,ldm);
 assert(r.safe.contains(r.panel));if(thumb){assert(r.panel.contains(r.thumbnail));assert(std::abs(r.thumbnail.w/r.thumbnail.h-16.f/9)<.001f);}
 std::vector<SurvivalRect> rows{r.title,r.creator,r.difficulty,r.metadata,r.ldm,r.shake,r.jukebox,r.ready,r.readiness};
 for(auto const& a:rows){assert(r.panel.contains(a));for(auto const& b:rows)if(&a!=&b)assert(!a.overlaps(b));}
 if(thumb)for(auto const& a:rows)assert(!a.overlaps(r.thumbnail));
 for(auto a:{r.shake,r.jukebox,r.ready})assert(a.h>=28&&a.w>=48);
 assert(r.ldm.h>=28);
 if(!thumb){assert(!r.hint.overlaps(r.panel));assert(!r.numberFinal.overlaps(r.panel));assert(!r.numberFinal.overlaps(r.subtitle));assert(!r.numberFinal.overlaps(r.header));}
}}`);
});
test('all 8/9/10 reels keep fixed horizontal columns while only their digits move vertically',()=>{
 run(`int main(){for(auto viewport:{std::pair{400.f,280.f},std::pair{568.f,320.f},std::pair{520.f,320.f},std::pair{568.f,426.f},std::pair{1024.f,576.f}})for(std::size_t n:{8,9,10}){
 auto layout=survivalNumberLayout(viewport.first,viewport.second,n);assert(layout.columns.size()==n);assert(layout.lineStep>0);
 SurvivalRect screen{0,0,viewport.first,viewport.second};assert(screen.contains(layout.ribbon));
 for(std::size_t i=0;i<n;++i){auto r=layout.columns[i];assert(layout.ribbon.contains(r));assert(r.cy()==layout.columns[0].cy());
  if(i){assert(!r.overlaps(layout.columns[i-1]));assert(r.x>layout.columns[i-1].right());}
 }
}assert(survivalNumberLayout(568,320,0).columns.empty());assert(survivalNumberLayout(568,320,11).columns.empty());}`);
});
test('global defaults and same-round overrides stay separate across object replacement, board navigation and new generations',()=>{
 run(`int main(){SurvivalRoundOptions r;SurvivalNativeOptions defaults{true,false};assert(r.enter("m:R1:1",defaults)==defaults);
 r.setLowDetail(false);r.setDisableShake(true);assert((r.enter("m:R1:1",{false,false})==SurvivalNativeOptions{false,true}));
 defaults={true,true};assert((r.enter("m:R1:1",defaults)==SurvivalNativeOptions{false,true}));assert(r.enter("m:R1:2",defaults)==defaults);
 assert((r.enter("m:R2:1",{false,false})==SurvivalNativeOptions{false,false}));
}`);
});
test('packaged fonts cover the unavailable LDM control and all-ready countdown in Korean',()=>{
 const captions=readFileSync(join(root,'src/ui/RankedTranslations.hpp'),'utf8');
 const fonts=JSON.parse(readFileSync(join(root,'mod.json'),'utf8')).resources.fonts;
 const keys=['survival-no-ldm','survival-entry-wait'];
 for(const key of keys){
  const caption=captions.match(new RegExp(`Translation\\{"${key}", "([^"]*)"\\}`))?.[1];assert(caption,key);
  for(const weight of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
   const charset=new Set(fonts[weight].charset.split(',').flatMap(part=>{const [first,last=first]=part.split('-').map(Number);return Array.from({length:last-first+1},(_,i)=>first+i);}));
   for(const glyph of caption)assert(charset.has(glyph.codePointAt(0)),`${weight} missing ${glyph}`);
  }
 }
});
