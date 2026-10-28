import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
test('actual reveal timeline preserves progress when server resolves 0 to level ID and scene identity changes',()=>{
const cpp=`#include "domain/SurvivalMapSelection.hpp"
#include "domain/SurvivalSelectionLayout.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){SurvivalMapSelectionTimeline t;SurvivalSelectionContext c{"m","r1",1,0,1};
 assert(t.observe(c,1000));auto pending=t.frame(2500);assert(pending.elapsed==1.5);
 c.levelId=73153577;assert(!t.observe(c,3000));assert(t.frame(3000).elapsed==2.0);
 c.sceneGeneration++;assert(!t.observe(c,3500));assert(t.frame(3500).elapsed==2.5);
 assert(!t.observe(c,4000));assert(t.frame(4000).elapsed==3.0);assert(t.frame(5100).settled);
 SurvivalThumbnailReel reel;auto const count=reel.slots().size();reel.observe(11);reel.observe(22);
 auto before=reel.slots();for(auto id:{33,44,11,55,66,77,88,99})reel.observe(id);
 assert(reel.slots().size()==count&&reel.slots()[0]==before[0]&&reel.slots()[1]==before[1]);
 for(auto size:{std::pair{400.f,280.f},std::pair{568.f,320.f},std::pair{568.f,426.f},std::pair{1024.f,576.f}}){
  auto layout=survivalSelectionLayout(size.first,size.second,true,true);auto w=survivalCarouselCardWidth(size.first,layout);
  assert(!layout.pending.overlaps(layout.carousel)&&!layout.pending.overlaps(layout.subtitle));
  assert(w>std::min(252.f*layout.scale,size.first*.40f));assert(w<layout.carousel.w&&w*9/16<=layout.carousel.h);
 }
 // Braking preserves speed at its boundary and lands the reel winner at center.
 for(auto brake:{1.95,4.,10.}){
  SurvivalRevealFrame f;f.brakeAt=brake;f.stopAt=brake+.8;double last=0;
  for(int i=0;i<=1200;++i){f.elapsed=brake-.1+i*.001;auto motion=survivalThumbnailMotion(f,true);assert(motion.phase>=last);last=motion.phase;
   if(motion.stopped){assert(std::abs(motion.phase-std::round(motion.phase))<.00001);assert(motion.winnerIndex==static_cast<std::size_t>(motion.phase)+2);}
  }
  f.elapsed=brake-.00001;auto before=survivalThumbnailMotion(f,true).phase;
  f.elapsed=brake+.00001;auto after=survivalThumbnailMotion(f,true).phase;assert(std::abs(after-before-2*.00001/.14)<.00001);
 }
 ++c.mapGeneration;c.levelId=12345678;assert(t.observe(c,6000));assert(t.frame(6000).elapsed==0);
 SurvivalMapSelectionTimeline delayed;auto unresolved=c;unresolved.levelId=0;delayed.observe(unresolved,6000);
 auto waiting=delayed.frame(16000);assert(waiting.elapsed==10&&!waiting.settled);
 delayed.observe(c,16000);auto resolved=delayed.frame(16000);assert(resolved.elapsed>=waiting.elapsed&&!resolved.settled);assert(delayed.frame(18100).settled);
}
`;
const dir=mkdtempSync(join(tmpdir(),'survival-reveal-once-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});

test('production selection context/registry path never resets an active reel on ID resolution, polls or coordinator recreation',()=>{
 const source=readFileSync(process.env.CORUM_REVEAL_LAYER_SOURCE ?? join(root,'src/ui/SurvivalMapSelectionLayer.cpp'),'utf8');
 const present=source.slice(source.indexOf('void SurvivalMapSelectionLayer::present('));
 // Compile the production ownership/timeline transaction before the renderer.
 // Only renderer, saved preferences and wall-clock boundaries are doubled.
 const transaction=present.slice(present.indexOf('    auto const now='),present.indexOf('    m_active=true;'));
 const timeline=source.slice(source.indexOf('bool SurvivalMapSelectionLayer::advanceTimeline('),source.indexOf('void SurvivalMapSelectionLayer::advancePreparation(')).replace('SurvivalMapSelectionLayer::','');
 const code=`#include "domain/SurvivalMapSelection.hpp"
#include <cassert>
#include <map>
#include <string>
using namespace corum::ranked;
struct Size{float width=568,height=426;};
long clockMs=1000;auto monotonicMs(){return clockMs;}
struct SurvivalThumbnailProvider{static unsigned applicationEpoch(){return 0;}};
struct Mod{static Mod*get(){static Mod m;return &m;}template<class T>T getSavedValue(char const*){return {};}};
struct RevealRecord{long began=0;std::size_t consumed=0;bool complete=false,centerPlayed=false;SurvivalRevealModeChoice mode{};SurvivalThumbnailReel reel{};long resolvedAt=0;};
std::map<std::string,RevealRecord>reveals;
struct View{std::string id="match",roundKey="r1",state="PREPARING";int mapGeneration=1;struct Map{int levelId=0;}map;};
using SurvivalMatchView=View;
struct Layer{SurvivalMapSelectionTimeline m_timeline;unsigned m_appEpoch=0;unsigned long m_sceneGeneration=1;bool m_needsResume=false;int resets=0;long m_lastFrameMillis=0;
 Size getContentSize(){return {};}void resetVisuals(){++resets;}
 ${timeline}
 void present(View const&view){std::optional<std::int64_t> countdown;${transaction} (void)size;(void)context;(void)changed;}
};
int main(){View view;Layer layer;layer.present(view);assert(layer.resets==1);
 clockMs=2500;view.map.levelId=73153577;layer.present(view);assert(layer.resets==1&&layer.m_timeline.frame(clockMs).elapsed==1.5);
 clockMs=3500;layer.present(view);assert(layer.resets==1&&layer.m_timeline.frame(clockMs).elapsed==2.5);
 Layer recreated;recreated.m_sceneGeneration=2;clockMs=4000;recreated.present(view);
 assert(recreated.resets==1&&recreated.m_timeline.frame(clockMs).elapsed==3);
 clockMs=5000;recreated.present(view);assert(recreated.resets==1&&recreated.m_timeline.frame(clockMs).settled);
 reveals.begin()->second.complete=true;Layer finished;finished.m_sceneGeneration=3;clockMs=5100;finished.present(view);
 assert(finished.m_timeline.frame(clockMs).skipped&&finished.m_timeline.frame(clockMs).settled);
 view.roundKey="r2";++view.mapGeneration;clockMs=6000;finished.present(view);
 assert(finished.resets==2&&finished.m_timeline.frame(clockMs).elapsed==0);
}
`;
 const dir=mkdtempSync(join(tmpdir(),'survival-reveal-registry-'));try{writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
