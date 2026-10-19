import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('Survival HUD has rank text and clock but creates no in-game halo or board button',()=>{
 const hud=read('src/ui/SurvivalHud.cpp')+read('src/ui/SurvivalHud.hpp');
 assert.doesNotMatch(hud,/m_rankHalo|m_haloOpacity|survivalRankHaloEligible|SurvivalHud::onBoard|survival-view-board|showSurvivalStandings/);
 assert.match(hud,/m_rank\.text\(survivalOrdinal/);
 assert.match(read('src/ui/SurvivalOutcomePanel.hpp'),/showSurvivalStandings/);
});
test('every Survival card uses the custom popup, and the podium reuses Gold ambient with authoritative LP',()=>{
 assert.doesNotMatch(read('src/ui/SurvivalProfile.hpp'),/ProfilePage|create\(player.accountId,false\)/);
 assert.match(read('src/ui/SurvivalProfile.hpp'),/SurvivalPlayerInfoPopup::create/);
 const podium=read('src/ui/SurvivalOutcomePanel.hpp');
 assert.match(podium,/createRankAmbientFx\("GOLD"/);assert.doesNotMatch(podium,/drawCircle/);assert.match(podium,/lpAfter/);
 const info=read('src/ui/SurvivalPlayerInfoPopup.hpp');
 for(const s of ['UserInfoDelegate','m_stars','m_moons','m_demons','extremeDemonCount','GDKF','POINTERCRATE'])assert(info.includes(s),s);
});
test('featured grades distinguish unknown, unrated, rated and every native feature state',()=>{
 const dir=mkdtempSync(join(tmpdir(),'survival-map-grade-'));try{
  writeFileSync(join(dir,'test.cpp'),String.raw`
#include "domain/MapFeaturePresentation.hpp"
#include <cassert>
int main(){using namespace corum::ranked;MapMetadata m;assert(mapFeatureGrade(m)=="RATING PENDING");
 m.kind="NORMAL";m.stars=0;assert(mapFeatureGrade(m)=="UNRATED");m.stars=5;assert(mapFeatureGrade(m)=="RATED");
 m.featureScore=1;assert(mapFeatureGrade(m)=="FEATURED");m.epic=1;assert(mapFeatureGrade(m)=="EPIC");m.epic=2;assert(mapFeatureGrade(m)=="LEGENDARY");m.epic=3;assert(mapFeatureGrade(m)=="MYTHIC");}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});
test('a revealed selected panel gets a full five seconds without replacing the server hold or native GO',()=>{
 const dir=mkdtempSync(join(tmpdir(),'survival-entry-hold-'));try{
  writeFileSync(join(dir,'test.cpp'),String.raw`
#include "domain/SurvivalEntryHold.hpp"
#include <cassert>
int main(){using namespace corum::ranked;SurvivalEntryHold hold;
 assert(!hold.remaining("m:r:1",1000,5000,1000,true,false)); // no server authorization
 auto first=hold.remaining("m:r:1",1000,6000,1250,true,true);assert(first&&*first==5000);
 for(int n=0;n<5;++n){auto left=hold.remaining("m:r:1",1000,6000,1250+n*1000,true,true);assert(left&&(*left+999)/1000==5-n);}
 assert(hold.remaining("m:r:1",1000,6000,6249,true,true)==1);
 assert(hold.remaining("m:r:1",1000,6000,6250,true,true)==0);
 assert(hold.remaining("m:r:1",1000,6000,7000,true,true)==0); // recreated UI does not restart
 assert(!hold.remaining("m:r2:1",0,0,7000,true,false)); // new round retires old numbers
 assert(hold.remaining("m:r2:1",7000,12000,7050,false,true)==std::nullopt); // hidden panel cannot start local gate
 assert(hold.remaining("m:r2:1",7000,12000,7350,true,true)==5000);
 assert(hold.remaining("m:r2:1",7000,12500,7400,true,true)==5100); // server can delay; local UI cannot advance it
 SurvivalEntryHold synced;
 assert(synced.remaining("m:r:2",1000,6000,1250,true,true,10000)==5000);
 assert(synced.remaining("m:r:2",1000,6000,9000,true,true,10001)==4999); // forward clock correction cannot consume visible seconds
 assert(synced.remaining("m:r:2",1000,6000,10000,true,true,15000)==0);
}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('production ASSETS_READY waits for the settled selected panel, exact generation and successful preparation once',()=>{
 const runtime=read('src/SurvivalRuntime.cpp');
 const ready=method(runtime,'void SurvivalRuntime::selectedPanelReady(').replace('SurvivalRuntime::','Runtime::');
 const assets=method(runtime,'void SurvivalRuntime::assetsReady(').replace('SurvivalRuntime::','Runtime::');
 const dir=mkdtempSync(join(tmpdir(),'survival-ready-barrier-'));try{
  writeFileSync(join(dir,'test.cpp'),String.raw`
#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>
struct Body{std::map<std::string,std::string> values;std::string&operator[](std::string const& key){return values[key];}};
struct GJGameLevel{int generation=1;};
struct RankedRoundPreparation{bool complete=true;static auto&get(){static RankedRoundPreparation p;return p;}bool ready(){return complete;}};
struct RankedRuntime{bool allowed=true;static auto&get(){static RankedRuntime p;return p;}bool prepareSurvivalEnvironment(Body&){return allowed;}};
std::string preparationContextText(std::string const& s){return s;}
struct Runtime{
 struct View{bool ownEliminated=false,online=true,ownAssetsReady=false;std::string state="PREPARING",certificate="verified";}m_view;
 bool observer=false,free=false;int generation=1;std::string m_selectedPanelContext,m_readyContext;std::vector<Body> queue;
 bool observing(){return observer;}bool freePlaying(){return free;}
 std::string optionsContext(){return "match:round:"+std::to_string(generation);}std::string preparationContext(){return optionsContext();}
 bool validateNative(GJGameLevel* level){return level&&level->generation==generation;}
 Body event(std::string type){Body b;b["type"]=type;return b;}void enqueue(Body b){queue.push_back(std::move(b));}
 void selectedPanelReady(bool);void assetsReady(GJGameLevel*);
};
`+ready+'\n'+assets+String.raw`
int main(){Runtime r;GJGameLevel level;
 r.assetsReady(&level);assert(r.queue.empty()); // map/audio ready during reel must not start server hold
 r.selectedPanelReady(false);r.assetsReady(&level);assert(r.queue.empty());
 r.selectedPanelReady(true);r.assetsReady(&level);r.assetsReady(&level);
 assert(r.queue.size()==1&&r.queue[0]["type"]=="ASSETS_READY"&&r.queue[0]["certificate"]=="verified");
 ++r.generation;r.assetsReady(&level);assert(r.queue.size()==1); // old panel cannot authorize a new round
 r.selectedPanelReady(true);r.assetsReady(&level);assert(r.queue.size()==1); // exact native generation still required
 level.generation=r.generation;RankedRoundPreparation::get().complete=false;r.assetsReady(&level);assert(r.queue.size()==1);
 RankedRoundPreparation::get().complete=true;RankedRuntime::get().allowed=false;r.assetsReady(&level);assert(r.queue.size()==1);
 RankedRuntime::get().allowed=true;r.observer=true;r.assetsReady(&level);assert(r.queue.size()==1);
 r.observer=false;r.m_view.ownEliminated=true;r.assetsReady(&level);assert(r.queue.size()==1);
 r.m_view.ownEliminated=false;r.assetsReady(&level);assert(r.queue.size()==2);
}`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
